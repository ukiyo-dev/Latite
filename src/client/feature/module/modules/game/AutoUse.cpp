#include "pch.h"
#include "AutoUse.h"
#include "client/Latite.h"
#include "client/input/Keyboard.h"
#include "client/event/events/UpdateEvent.h"
#include "mc/common/world/actor/player/Player.h"
#include "mc/common/client/game/MinecraftGame.h"
#include "mc/common/client/game/MouseInputPacket.h"
#include "mc/common/world/ItemStack.h"
#include "mc/Addresses.h"
#include "util/Util.h"
#include <algorithm>
#include <array>
#include <regex>
#include <vector>
#include <Windows.h>

namespace {
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

	bool matchesPattern(std::string const& name, std::wstring const& pattern) {
		if (pattern.empty()) return false;
		try {
			std::regex re(util::WStrToStr(pattern));
			return std::regex_search(name, re);
		}
		catch (std::regex_error const&) {
			return false;
		}
	}

	int findBestPatternSlot(SDK::PlayerInventory* inv, std::wstring const& pattern) {
		if (!inv || !inv->inventory) return -1;

		for (int i = 0; i < 9; ++i) {
			auto stack = inv->inventory->getItem(i);
			if (!stack || !stack->valid || stack->itemCount == 0) continue;
			auto item = stack->getItem();
			if (!item) continue;
			auto name = getMatchName(item);
			if (matchesPattern(name, pattern)) {
				return i;
			}
		}

		return -1;
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

AutoUse::AutoUse()
	: Module("AutoUse", LocalizeString::get("client.module.autoUse.name"),
	         LocalizeString::get("client.module.autoUse.desc"), GAME, nokeybind) {
	addSetting("key1", LocalizeString::get("client.module.autoUse.key1.name"),
	           LocalizeString::get("client.module.autoUse.key1.desc"), key1);
	addSetting("pattern1", LocalizeString::get("client.module.autoUse.pattern1.name"),
	           LocalizeString::get("client.module.autoUse.pattern1.desc"), pattern1);
	addSetting("key2", LocalizeString::get("client.module.autoUse.key2.name"),
	           LocalizeString::get("client.module.autoUse.key2.desc"), key2);
	addSetting("pattern2", LocalizeString::get("client.module.autoUse.pattern2.name"),
	           LocalizeString::get("client.module.autoUse.pattern2.desc"), pattern2);
	addSetting("key3", LocalizeString::get("client.module.autoUse.key3.name"),
	           LocalizeString::get("client.module.autoUse.key3.desc"), key3);
	addSetting("pattern3", LocalizeString::get("client.module.autoUse.pattern3.name"),
	           LocalizeString::get("client.module.autoUse.pattern3.desc"), pattern3);
	listen<UpdateEvent>((EventListenerFunc)&AutoUse::onTick);
}

void AutoUse::onEnable() {
	isUsing = false;
	pendingStart = false;
	activeKey = -1;
	blockUntilRelease = { false, false, false };
	wasKeyDown = { false, false, false };
	usingSlot = -1;
	usingCount = 0;
	useStart = {};
}

void AutoUse::onDisable() {
	if (isUsing) {
		pushRightButton(false);
	}
	isUsing = false;
	pendingStart = false;
	activeKey = -1;
	blockUntilRelease = { false, false, false };
	wasKeyDown = { false, false, false };
	usingSlot = -1;
	usingCount = 0;
	useStart = {};
}

void AutoUse::onTick(Event&) {
	auto mcGame = SDK::ClientInstance::get()->minecraftGame;
	if (!mcGame || !mcGame->isCursorGrabbed()) {
		if (isUsing) {
			pushRightButton(false);
			isUsing = false;
		}
		pendingStart = false;
		activeKey = -1;
		blockUntilRelease = { false, false, false };
		wasKeyDown = { false, false, false };
		return;
	}

	std::array<int, 3> keys = {
		std::get<KeyValue>(key1),
		std::get<KeyValue>(key2),
		std::get<KeyValue>(key3)
	};
	std::array<std::wstring, 3> patterns = {
		std::get<TextValue>(pattern1).str,
		std::get<TextValue>(pattern2).str,
		std::get<TextValue>(pattern3).str
	};
	std::array<bool, 3> keyDown = {
		keys[0] != 0 && Latite::getKeyboard().isKeyDown(keys[0]),
		keys[1] != 0 && Latite::getKeyboard().isKeyDown(keys[1]),
		keys[2] != 0 && Latite::getKeyboard().isKeyDown(keys[2])
	};
	for (int i = 0; i < 3; ++i) {
		if (!keyDown[i] && keys[i] != 0) {
			keyDown[i] = (GetAsyncKeyState(keys[i]) & 0x8000) != 0;
		}
	}

	std::array<bool, 3> justPressed = {
		keyDown[0] && !wasKeyDown[0],
		keyDown[1] && !wasKeyDown[1],
		keyDown[2] && !wasKeyDown[2]
	};

	wasKeyDown = keyDown;
	bool leftDown = Latite::getKeyboard().isKeyDown(VK_LBUTTON);
	if (!leftDown) {
		leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	}
	if (leftDown) {
		if (isUsing) {
			pushRightButton(false);
			isUsing = false;
		}
		pendingStart = false;
		activeKey = -1;
		for (int i = 0; i < 3; ++i) {
			blockUntilRelease[i] = keyDown[i];
		}
		return;
	}
	for (int i = 0; i < 3; ++i) {
		if (!keyDown[i]) {
			blockUntilRelease[i] = false;
		}
	}

	auto lp = SDK::ClientInstance::get()->getLocalPlayer();
	if (!lp || !lp->gameMode || !lp->supplies || !lp->supplies->inventory) {
		return;
	}

	auto inv = lp->supplies;
	for (int i = 0; i < 3; ++i) {
		if (justPressed[i] && !blockUntilRelease[i]) {
			if (isUsing) {
				pushRightButton(false);
				isUsing = false;
			}
			pendingStart = true;
			activeKey = i;
			useStart = {};
		}
	}
	if (isUsing) {
		if (inv->selectedSlot != usingSlot) {
			pushRightButton(false);
			isUsing = false;
			pendingStart = false;
			return;
		}
		auto stack = inv->inventory->getItem(usingSlot);
		if (!stack || !stack->valid || stack->itemCount < usingCount) {
			pushRightButton(false);
			isUsing = false;
			if (activeKey >= 0 && activeKey < 3 && keyDown[activeKey]) {
				blockUntilRelease[activeKey] = true;
			}
			pendingStart = false;
			useStart = {};
		}
		auto now = std::chrono::steady_clock::now();
		if (useStart != std::chrono::steady_clock::time_point{} && now - useStart > useTimeout) {
			pushRightButton(false);
			isUsing = false;
			pendingStart = false;
			useStart = {};
		}
		return;
	}
	if (!pendingStart) {
		return;
	}

	if (activeKey < 0 || activeKey > 2) {
		pendingStart = false;
		return;
	}
	auto pattern = patterns[activeKey];
	int bestSlot = findBestPatternSlot(inv, pattern);
	if (bestSlot == -1) return;

	if (inv->selectedSlot != bestSlot) {
		sendHotbarKey(bestSlot);
		return;
	}

	auto selected = inv->inventory->getItem(inv->selectedSlot);
	if (!selected || !selected->valid) return;
	auto selectedName = getMatchName(selected->getItem());
	if (!matchesPattern(selectedName, pattern)) return;

	usingSlot = bestSlot;
	usingCount = selected->itemCount;
	pushRightButton(true);
	useStart = std::chrono::steady_clock::now();
	isUsing = true;
	pendingStart = false;
}
