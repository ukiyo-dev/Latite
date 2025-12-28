#include "pch.h"
#include "AutoClicker.h"
#include "client/Latite.h"
#include "client/input/Keyboard.h"
#include "client/event/events/UpdateEvent.h"
#include "client/event/events/ClickEvent.h"
#include "client/event/events/AttackEvent.h"
#include "mc/common/world/actor/player/Player.h"
#include "mc/common/client/game/MinecraftGame.h"
#include "mc/common/world/level/Level.h"
#include "mc/common/world/level/HitResult.h"
#include "mc/common/client/game/MouseInputPacket.h"
#include "mc/common/world/ItemStack.h"
#include "mc/Addresses.h"
#include "AutoEat.h"
#include <algorithm>
#include <vector>
#include <Windows.h>

namespace {
	bool contains(std::string const& text, std::string_view needle) {
		return text.find(needle) != std::string::npos;
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

	bool endsWith(std::string const& text, std::string_view suffix) {
		if (suffix.size() > text.size()) return false;
		return std::equal(suffix.rbegin(), suffix.rend(), text.rbegin());
	}

	int tierFromName(std::string const& name) {
		if (contains(name, "netherite")) return 6;
		if (contains(name, "diamond")) return 5;
		if (contains(name, "iron")) return 4;
		if (contains(name, "gold")) return 3;
		if (contains(name, "stone")) return 2;
		if (contains(name, "wooden") || contains(name, "wood")) return 1;
		return 0;
	}

	bool isSword(std::string const& name) {
		return contains(name, "sword");
	}

	bool isAxe(std::string const& name) {
		return contains(name, "axe") && !contains(name, "pickaxe");
	}

	bool isPickaxe(std::string const& name) {
		return contains(name, "pickaxe");
	}

	bool isTrident(std::string const& name) {
		return contains(name, "trident");
	}

	int scoreForWeapon(std::string const& name) {
		int tier = tierFromName(name);
		if (isSword(name)) return 100 + tier;
		if (isTrident(name)) return 104; // between iron sword (103) and diamond sword (105)
		if (isAxe(name)) return 90 + tier;
		if (isPickaxe(name)) return 80 + tier;
		return 0;
	}

	int scoreForPickaxe(std::string const& name) {
		int tier = tierFromName(name);
		return isPickaxe(name) ? 100 + tier : 0;
	}

	int blockPriority(std::string const& name) {
		if (endsWith(name, "terracotta")) return 0;
		if (endsWith(name, "wool")) return 1;
		if (endsWith(name, "stone")) return 2;
		if (endsWith(name, "_planks")) return 3;
		return -1;
	}

	bool isRightClickWhitelist(std::string const& name) {
		// Treat patterns like ".*sword$" as simple suffix matches.
		return endsWith(name, "sword") ||
			endsWith(name, "pickaxe") ||
			endsWith(name, "diamond") ||
			endsWith(name, "emerald") ||
			endsWith(name, "ingot");
	}

	bool isPreferredBlock(SDK::ItemStack* stack) {
		if (!stack || !stack->valid || stack->itemCount == 0) return false;
		auto item = stack->getItem();
		if (!item) return false;
		return blockPriority(getMatchName(item)) >= 0;
	}

	int findBestBlockSlot(SDK::PlayerInventory* inv) {
		if (!inv || !inv->inventory) return -1;
		int bestSlot = -1;
		int bestPriority = 999;
		int bestCount = 0;

		for (int i = 0; i < 9; ++i) {
			auto stack = inv->inventory->getItem(i);
			if (!stack || !stack->valid || stack->itemCount == 0) continue;
			auto item = stack->getItem();
			if (!item) continue;
			auto name = getMatchName(item);
			int prio = blockPriority(name);
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

	int findBestWeaponSlot(SDK::PlayerInventory* inv) {
		if (!inv || !inv->inventory) return -1;
		int bestSlot = -1;
		int bestScore = 0;
		for (int i = 0; i < 9; ++i) {
			auto stack = inv->inventory->getItem(i);
			if (!stack) continue;
			auto item = stack->getItem();
			if (!item) continue;
			auto name = getMatchName(item);
			int score = scoreForWeapon(name);
			if (score > bestScore) {
				bestScore = score;
				bestSlot = i;
			}
		}
		return bestSlot;
	}

	int findBestPickaxeSlot(SDK::PlayerInventory* inv) {
		if (!inv || !inv->inventory) return -1;
		int bestSlot = -1;
		int bestScore = 0;
		for (int i = 0; i < 9; ++i) {
			auto stack = inv->inventory->getItem(i);
			if (!stack) continue;
			auto item = stack->getItem();
			if (!item) continue;
			auto name = getMatchName(item);
			int score = scoreForPickaxe(name);
			if (score > bestScore) {
				bestScore = score;
				bestSlot = i;
			}
		}
		return bestSlot;
	}

	void pushMouseButton(bool down) {
		auto vec = reinterpret_cast<std::vector<MouseInputPacket>*>(Signatures::MouseInputVector.result);
		if (!vec) return;
		MouseInputPacket pkt{};
		pkt.type = 1;
		pkt.state = down ? 1 : 0;
		vec->push_back(pkt);
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

	float distSq(Vec3 const& a, Vec3 const& b) {
		float dx = a.x - b.x;
		float dy = a.y - b.y;
		float dz = a.z - b.z;
		return dx * dx + dy * dy + dz * dz;
	}

}

AutoClicker::AutoClicker() : Module("AutoClicker", LocalizeString::get("client.module.autoClicker.name"),
                                    LocalizeString::get("client.module.autoClicker.desc"), GAME, nokeybind) {
	addSetting("triggerKey", LocalizeString::get("client.module.autoClicker.key.name"),
	           LocalizeString::get("client.module.autoClicker.key.desc"), this->triggerKey);
	addSliderSetting("cps", LocalizeString::get("client.module.autoClicker.cps.name"),
	                 LocalizeString::get("client.module.autoClicker.cps.desc"), this->cps, FloatValue(1.f),
	                 FloatValue(20.f), FloatValue(1.f));
	addSetting("forceClick", LocalizeString::get("client.module.autoClicker.force.name"),
	           LocalizeString::get("client.module.autoClicker.force.desc"), this->restrictMode);
	addSetting("critMode", LocalizeString::get("client.module.autoClicker.crit.name"),
	           LocalizeString::get("client.module.autoClicker.crit.desc"), this->critMode);
	addSetting("proMode", LocalizeString::get("client.module.autoClicker.pro.name"),
	           LocalizeString::get("client.module.autoClicker.pro.desc"), this->proMode);
	addSliderSetting("graceMs", LocalizeString::get("client.module.autoClicker.grace.name"),
	                 LocalizeString::get("client.module.autoClicker.grace.desc"), this->graceMs, FloatValue(0.f),
	                 FloatValue(1000.f), FloatValue(100.f), "forceClick"_istrue);

	listen<UpdateEvent>((EventListenerFunc)&AutoClicker::onTick);
	listen<ClickEvent>((EventListenerFunc)&AutoClicker::onClick);
	listen<AttackEvent>((EventListenerFunc)&AutoClicker::onAttack);
}

void AutoClicker::onClick(Event& evG) {
	auto& ev = reinterpret_cast<ClickEvent&>(evG);
	if (ev.getMouseButton() == 2) {
		rightHeld = ev.isDown();
	}
}

void AutoClicker::onAttack(Event& evG) {
	auto& ev = reinterpret_cast<AttackEvent&>(evG);
	lastAttackTarget = ev.getActor();
	lastAttackAt = std::chrono::steady_clock::now();
}

void AutoClicker::onTick(Event&) {
	auto mcGame = SDK::ClientInstance::get()->minecraftGame;
	if (!mcGame) {
		if (holdingBlock) {
			pushMouseButton(false);
			holdingBlock = false;
		}
		shouldClick = false;
		cursorWasGrabbed = false;
		return;
	}
	bool grabbed = mcGame->isCursorGrabbed();
	if (grabbed && !cursorWasGrabbed) {
		cursorGrabbedAt = std::chrono::steady_clock::now();
	}
	cursorWasGrabbed = grabbed;
	if (!grabbed) {
		if (holdingBlock) {
			pushMouseButton(false);
			holdingBlock = false;
		}
		shouldClick = false;
		return;
	}
	if (std::chrono::steady_clock::now() - cursorGrabbedAt < std::chrono::milliseconds(100)) {
		return;
	}

	int triggerKey = std::get<KeyValue>(this->triggerKey);
	bool leftDown = false;
	if (triggerKey == 0) {
		leftDown = Latite::getKeyboard().isKeyDown(VK_LBUTTON);
		if (!leftDown) {
			leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
		}
	} else {
		leftDown = Latite::getKeyboard().isKeyDown(triggerKey);
		if (!leftDown) {
			leftDown = (GetAsyncKeyState(triggerKey) & 0x8000) != 0;
		}
	}
	bool rightDown = rightHeld || Latite::getKeyboard().isKeyDown(VK_RBUTTON);
	if (!rightDown) {
		rightDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
	}
	shouldClick = leftDown;
	bool justPressed = leftDown && !wasKeyDown;
	auto now = std::chrono::steady_clock::now();
	if (justPressed) {
		nextClick = now;
		lastKeyDown = now;
		weaponToolApplied = false;
	}
	wasKeyDown = leftDown;
	if (!leftDown && !rightDown) {
		if (holdingBlock) {
			pushMouseButton(false);
			holdingBlock = false;
		}
		blockToolApplied = false;
		weaponToolApplied = false;
		activeMode = 0;
		return;
	}

	auto lp = SDK::ClientInstance::get()->getLocalPlayer();
	if (!lp || !lp->gameMode) {
		return;
	}

	bool skipRightSwitch = false;
	auto eatBase = Latite::getModuleManager().find("AutoEat");
	auto eatMod = eatBase ? static_cast<AutoEat*>(eatBase.get()) : nullptr;
	if (eatMod) {
		if (eatMod->isUsingItem()) {
			skipRightSwitch = true;
		} else {
			int eatKey = eatMod->getTriggerKey();
			if (eatKey != 0) {
				bool eatDown = Latite::getKeyboard().isKeyDown(eatKey);
				if (!eatDown) {
					eatDown = (GetAsyncKeyState(eatKey) & 0x8000) != 0;
				}
				if (eatDown) {
					skipRightSwitch = true;
				}
			}
		}
	}

	if (rightDown && !skipRightSwitch) {
		auto inv = lp->supplies;
		if (inv && inv->inventory) {
			auto selected = inv->inventory->getItem(inv->selectedSlot);
			auto selectedName = selected && selected->valid ? getMatchName(selected->getItem()) : std::string{};
			bool keepCurrent = isPreferredBlock(selected);
			bool allowSwitch = keepCurrent || isRightClickWhitelist(selectedName) || selectedName.empty();
			if (!allowSwitch) {
				return;
			}
			int bestSlot = keepCurrent ? inv->selectedSlot : findBestBlockSlot(inv);
			if (!keepCurrent) {
				if (bestSlot != -1 && bestSlot != inv->selectedSlot) {
					sendHotbarKey(bestSlot);
				}
			}
		}
	}

	if (!leftDown) {
		if (holdingBlock) {
			pushMouseButton(false);
			holdingBlock = false;
		}
		blockToolApplied = false;
		weaponToolApplied = false;
		activeMode = 0;
		return;
	}

	auto level = SDK::ClientInstance::get()->minecraft->getLevel();
	if (!level) {
		return;
	}

	auto hr = level->getHitResult();
	bool hitBlock = hr && hr->hitType == SDK::HitType::BLOCK;
	bool hitEntity = hr && hr->hitType == SDK::HitType::ENTITY;

	if (hitEntity && !wasHitEntity) {
		nextClick = now;
	}
	wasHitEntity = hitEntity;

	if (hitEntity) {
		lastEntityHit = now;
	}

	enum class AimMode { None = 0, Entity = 1, Block = 2 };
	int prevMode = activeMode;
	AimMode mode = static_cast<AimMode>(activeMode);
	if (justPressed) {
		mode = hitBlock ? AimMode::Block : AimMode::Entity;
		activeMode = static_cast<int>(mode);
	}
	if (mode == AimMode::Block && hitEntity) {
		mode = AimMode::Entity;
		activeMode = static_cast<int>(mode);
	}
	if (mode == AimMode::None) {
		mode = AimMode::Entity;
		activeMode = static_cast<int>(mode);
	}
	int modeInt = static_cast<int>(mode);
	bool modeChanged = (modeInt != prevMode);
	if (mode != AimMode::Entity) {
		weaponToolApplied = false;
	}
	if (mode == AimMode::Entity && (!weaponToolApplied || modeChanged)) {
		if (auto inv = lp->supplies) {
			int weaponSlot = findBestWeaponSlot(inv);
			if (weaponSlot != -1) {
				if (weaponSlot != inv->selectedSlot) {
					sendHotbarKey(weaponSlot);
				}
			}
			weaponToolApplied = true;
			nextWeaponCheck = now + std::chrono::milliseconds(100);
		}
	}
	if (mode == AimMode::Entity && now >= nextWeaponCheck) {
		if (auto inv = lp->supplies) {
			int weaponSlot = findBestWeaponSlot(inv);
			if (weaponSlot != -1 && weaponSlot != inv->selectedSlot) {
				sendHotbarKey(weaponSlot);
			}
		}
		nextWeaponCheck = now + std::chrono::milliseconds(100);
	}

	if (mode != AimMode::Block) {
		blockToolApplied = false;
	}

	if (mode == AimMode::Block && (!blockToolApplied || modeChanged)) {
		if (auto inv = lp->supplies) {
			int pickSlot = findBestPickaxeSlot(inv);
			if (pickSlot != -1) {
				if (pickSlot != inv->selectedSlot) {
					sendHotbarKey(pickSlot);
				}
			}
			blockToolApplied = true;
		}
	}

	if (mode == AimMode::Block) {
		if (!holdingBlock) {
			pushMouseButton(true);
			holdingBlock = true;
		}
		return;
	}

	if (holdingBlock) {
		pushMouseButton(false);
		holdingBlock = false;
	}

	bool proOnly = std::get<BoolValue>(proMode);
	if (proOnly) {
		if (hitEntity && lastAttackTarget && (now - lastAttackAt) < std::chrono::milliseconds(2000)) {
			if (lastAttackTarget->invulnerableTime > 0) {
				wasInvulBlocked = true;
				if (now < nextInvulSwing) {
					return;
				}
				nextInvulSwing = now + std::chrono::milliseconds(250);
			}
			if (wasInvulBlocked) {
				nextClick = now;
				wasInvulBlocked = false;
			}
		} else {
			wasInvulBlocked = false;
			nextInvulSwing = now;
		}
	}

	bool critOnly = std::get<BoolValue>(critMode);
	if (critOnly) {
		auto sv = lp->stateVector;
		if (!sv) {
			return;
		}
		if (sv->velocity.y > 0.05f) {
			if (now < nextCritSwing) {
				return;
			}
			nextCritSwing = now + std::chrono::milliseconds(250);
		} else {
			nextCritSwing = now;
		}
	}

	if (now < nextClick) return;
	bool restrict = std::get<BoolValue>(restrictMode);
	float grace = std::clamp(std::get<FloatValue>(graceMs).value, 0.f, 500.f);
	auto graceWindow = std::chrono::milliseconds(static_cast<int>(grace));
	bool inGrace = (now - lastEntityHit <= graceWindow) || (now - lastKeyDown <= graceWindow);
	if (restrict && !hitEntity && !inGrace) {
		return;
	}

	auto sv = lp->stateVector;
	bool falling = critOnly && sv && sv->velocity.y < -0.05f && (sv->pos.y + 0.001f < sv->posOld.y);
	auto interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
		std::chrono::milliseconds(50));
	if (!falling) {
		float cpsVal = std::clamp(std::get<FloatValue>(cps).value, 1.f, 20.f);
		interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
			std::chrono::duration<double>(1.0 / cpsVal));
	}
	nextClick = now + interval;
	pushMouseButton(true);
	pushMouseButton(false);
}

void AutoClicker::onEnable() {
	shouldClick = false;
	holdingBlock = false;
	wasKeyDown = false;
	rightHeld = false;
	blockToolApplied = false;
	weaponToolApplied = false;
	activeMode = 0;
	wasInvulBlocked = false;
	wasHitEntity = false;
	nextInvulSwing = std::chrono::steady_clock::now();
	lastAttackTarget = nullptr;
	lastAttackAt = {};
	nextCritSwing = std::chrono::steady_clock::now();
}

void AutoClicker::onDisable() {
	if (holdingBlock) {
		pushMouseButton(false);
		holdingBlock = false;
	}
	shouldClick = false;
	wasKeyDown = false;
	rightHeld = false;
	blockToolApplied = false;
	weaponToolApplied = false;
	activeMode = 0;
	wasInvulBlocked = false;
	wasHitEntity = false;
	nextInvulSwing = std::chrono::steady_clock::now();
	lastAttackTarget = nullptr;
	lastAttackAt = {};
	nextCritSwing = std::chrono::steady_clock::now();
}
