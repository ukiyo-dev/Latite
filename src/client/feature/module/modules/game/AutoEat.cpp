#include "pch.h"
#include "AutoEat.h"
#include "AutoClicker.h"
#include "client/Latite.h"
#include "client/input/Keyboard.h"
#include "client/event/events/UpdateEvent.h"
#include "mc/common/world/actor/player/Player.h"
#include "mc/common/client/game/MinecraftGame.h"
#include "mc/common/client/game/MouseInputPacket.h"
#include "mc/common/world/ItemStack.h"
#include "mc/Addresses.h"
#include <algorithm>
#include <vector>
#include <Windows.h>

namespace {
	constexpr auto kAutoEatPriorityWindow = std::chrono::milliseconds(100);

	bool endsWith(std::string const& text, std::string_view suffix) {
		if (suffix.size() > text.size()) return false;
		return std::equal(suffix.rbegin(), suffix.rend(), text.rbegin());
	}

	std::string normalizeItemName(std::string name) {
		auto colon = name.find(':');
		if (colon != std::string::npos && colon + 1 < name.size()) {
			name = name.substr(colon + 1);
		}
		return name;
	}

	std::string getMatchName(SDK::Item* item) {
		if (!item) return {};
		std::string name = item->namespacedId.getString();
		if (name.empty()) {
			name = item->id.getString();
		}
		if (name.empty()) {
			name = item->translateName;
		}
		return normalizeItemName(name);
	}

	int foodPriority(std::string const& name) {
		if (endsWith(name, "enchanted_golden_apple")) return 4;
		if (endsWith(name, "golden_apple")) return 0;
		if (endsWith(name, "cooked_beef")) return 1;
		if (endsWith(name, "bread")) return 2;
		if (endsWith(name, "apple")) return 3;
		return -1;
	}

	bool isPreferredFood(SDK::ItemStack* stack) {
		if (!stack || !stack->valid || stack->itemCount == 0) return false;
		auto item = stack->getItem();
		if (!item) return false;
		return foodPriority(getMatchName(item)) >= 0;
	}

	int findBestFoodSlot(SDK::PlayerInventory* inv) {
		if (!inv || !inv->inventory) return -1;
		int bestSlot = -1;
		int bestPriority = 999;
		int bestCount = 0;

		for (int i = 0; i < 9; ++i) {
			auto stack = inv->inventory->getItem(i);
			if (!stack || !stack->valid || stack->itemCount == 0) continue;
			auto item = stack->getItem();
			if (!item) continue;
			int prio = foodPriority(getMatchName(item));
			if (prio < 0) continue;
			int count = static_cast<int>(stack->itemCount);
			if (prio < bestPriority || (prio == bestPriority && count > bestCount)) {
				bestPriority = prio;
				bestCount = count;
				bestSlot = i;
			}
		}

		return bestSlot;
	}

	void sendHotbarKey(int slot) {
		int clamped = std::clamp(slot, 0, 8);
		std::string mapping = "hotbar." + std::to_string(clamped + 1);
		int vk = Latite::getKeyboard().getMappedKey(mapping);
		if (vk <= 0 || vk > 255) return;

		INPUT inputs[2]{};
		inputs[0].type = INPUT_KEYBOARD;
		inputs[0].ki.wVk = static_cast<WORD>(vk);
		inputs[0].ki.wScan = static_cast<WORD>(MapVirtualKey(vk, MAPVK_VK_TO_VSC));
		inputs[1] = inputs[0];
		inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
		SendInput(2, inputs, sizeof(INPUT));
	}

	void pushRightButton(bool down) {
		auto vec = reinterpret_cast<std::vector<MouseInputPacket>*>(Signatures::MouseInputVector.result);
		if (!vec) return;
		MouseInputPacket pkt{};
		pkt.type = 2;
		pkt.state = down ? 1 : 0;
		vec->push_back(pkt);
	}
}

AutoEat::AutoEat()
	: Module("AutoEat", LocalizeString::get("client.module.autoEat.name"),
	         LocalizeString::get("client.module.autoEat.desc"), GAME, nokeybind) {
	addSetting("triggerKey", LocalizeString::get("client.module.props.key.name"),
	           LocalizeString::get("client.module.props.key.desc"), triggerKey);
	listen<UpdateEvent>((EventListenerFunc)&AutoEat::onTick);
}

void AutoEat::onEnable() {
	isUsing = false;
	pendingStart = false;
	wasKeyDown = false;
	usingSlot = -1;
	pendingSlot = -1;
	lastSelectedSlot = -1;
	useActiveSeen = false;
	useStart = {};
	ignoreInterruptUntil = {};
}

void AutoEat::onDisable() {
	if (isUsing) {
		pushRightButton(false);
	}
	isUsing = false;
	pendingStart = false;
	wasKeyDown = false;
	usingSlot = -1;
	pendingSlot = -1;
	lastSelectedSlot = -1;
	useActiveSeen = false;
	useStart = {};
	ignoreInterruptUntil = {};
}

void AutoEat::onTick(Event&) {
	auto stopUsing = [&]() {
		if (isUsing) {
			pushRightButton(false);
		}
		isUsing = false;
		usingSlot = -1;
		useActiveSeen = false;
		useStart = {};
		ignoreInterruptUntil = {};
	};
	auto clearPending = [&]() {
		pendingStart = false;
		pendingSlot = -1;
		lastSelectedSlot = -1;
	};

	auto now = std::chrono::steady_clock::now();

	auto mcGame = SDK::ClientInstance::get()->minecraftGame;
	if (!mcGame || !mcGame->isCursorGrabbed()) {
		stopUsing();
		clearPending();
		wasKeyDown = false;
		return;
	}

	int vk = std::get<KeyValue>(triggerKey);
	if (vk == 0) {
		stopUsing();
		clearPending();
		wasKeyDown = false;
		return;
	}

	auto keyState = GetAsyncKeyState(vk);
	bool keyDown = Latite::getKeyboard().isKeyDown(vk) || ((keyState & 0x8000) != 0);
	bool justPressed = (keyDown && !wasKeyDown) || (!keyDown && !wasKeyDown && (keyState & 0x0001) != 0);
	wasKeyDown = keyDown;

	bool leftDown = Latite::getKeyboard().isKeyDown(VK_LBUTTON);
	if (!leftDown) {
		leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	}
	bool rightDown = Latite::getKeyboard().isKeyDown(VK_RBUTTON);
	if (!rightDown) {
		rightDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
	}
    bool autoClickerDown = false;
    AutoClicker* clickMod = nullptr;
    if (auto clickBase = Latite::getModuleManager().find("AutoClicker")) {
            clickMod = static_cast<AutoClicker*>(clickBase.get());
            if (clickMod && clickMod->isEnabled()) {
                    autoClickerDown = leftDown;
            }
    }

	auto lp = SDK::ClientInstance::get()->getLocalPlayer();
	if (!lp || !lp->gameMode || !lp->supplies || !lp->supplies->inventory) {
		return;
	}

	auto inv = lp->supplies;
	if (justPressed) {
		stopUsing();
		clearPending();
		pendingStart = true;
		lastSelectedSlot = inv->selectedSlot;
		ignoreInterruptUntil = now + kAutoEatPriorityWindow;
		if (clickMod) {
			clickMod->blockClicksFor(kAutoEatPriorityWindow);
		}
	}

	if (now >= ignoreInterruptUntil) {
		if (leftDown) {
			stopUsing();
			clearPending();
			return;
		}
		if (autoClickerDown) {
			stopUsing();
			clearPending();
			return;
		}
		if (rightDown) {
			stopUsing();
			clearPending();
			return;
		}
	}
	if (isUsing) {
		if (inv->selectedSlot != usingSlot) {
			if (now >= ignoreInterruptUntil) {
				stopUsing();
				clearPending();
				return;
			}
		}
		auto stack = inv->inventory->getItem(usingSlot);
		if (!stack || !stack->valid || stack->itemCount == 0) {
			stopUsing();
			clearPending();
			return;
		}
		if (!isPreferredFood(stack)) {
			stopUsing();
			clearPending();
			return;
		}
		int useDur = lp->getItemUseDuration();
		if (useDur > 0) {
			useActiveSeen = true;
		} else if (useActiveSeen) {
			stopUsing();
			clearPending();
			return;
		}
		if (now - useStart >= useDuration) {
			stopUsing();
			clearPending();
			return;
		}
		return;
	}

	if (!pendingStart) {
		return;
	}

	if (pendingSlot == -1) {
		auto current = inv->inventory->getItem(inv->selectedSlot);
		bool keepCurrent = isPreferredFood(current);
		int bestSlot = keepCurrent ? inv->selectedSlot : findBestFoodSlot(inv);
		if (bestSlot == -1) {
			clearPending();
			return;
		}
		pendingSlot = bestSlot;
		lastSelectedSlot = inv->selectedSlot;
	}

	if (inv->selectedSlot != pendingSlot) {
		if (lastSelectedSlot != -1 && inv->selectedSlot != lastSelectedSlot && inv->selectedSlot != pendingSlot) {
			clearPending();
			return;
		}
		sendHotbarKey(pendingSlot);
		lastSelectedSlot = inv->selectedSlot;
		return;
	}

	auto selected = inv->inventory->getItem(inv->selectedSlot);
	if (!isPreferredFood(selected)) {
		clearPending();
		return;
	}

	usingSlot = pendingSlot;
	useStart = now;
	useActiveSeen = false;
	pushRightButton(true);
	isUsing = true;
	clearPending();
}
