#include "pch.h"
#include "BlockPlacer.h"
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
	bool endsWith(std::string const& text, std::string_view suffix) {
		if (suffix.size() > text.size()) return false;
		return std::equal(suffix.rbegin(), suffix.rend(), text.rbegin());
	}

	int blockPriority(std::string const& name) {
		// Lower value = higher priority
		if (endsWith(name, "terracotta")) return 0;
		if (endsWith(name, "wool")) return 1;
		if (endsWith(name, "stone")) return 2;
		if (endsWith(name, "_planks")) return 3;
		if (endsWith(name, "obsidian")) return 4;
		if (endsWith(name, "ladder")) return 5;
		if (endsWith(name, "tnt")) return 6;
		return -1;
	}

	bool isPreferredBlock(SDK::ItemStack* stack) {
		if (!stack || !stack->getItem() || !stack->valid || stack->itemCount == 0) return false;
		return blockPriority(stack->getItem()->namespacedId.getString()) >= 0;
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
			auto name = item->namespacedId.getString();
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

	void sendRightClick() {
		auto vec = reinterpret_cast<std::vector<MouseInputPacket>*>(Signatures::MouseInputVector.result);
		if (!vec) return;
		MouseInputPacket down{};
		down.type = 2;
		down.state = 1;
		MouseInputPacket up = down;
		up.state = 0;
		vec->push_back(down);
		vec->push_back(up);
	}
}

BlockPlacer::BlockPlacer()
	: Module("BlockPlacer", LocalizeString::get("client.module.blockPlacer.name"),
	         LocalizeString::get("client.module.blockPlacer.desc"), GAME, 0) {
	addSliderSetting("cps", LocalizeString::get("client.module.blockPlacer.cps.name"),
	                 LocalizeString::get("client.module.blockPlacer.cps.desc"), this->cps, FloatValue(1.f),
	                 FloatValue(100.f), FloatValue(1.f));
	addSetting("rateLimit", LocalizeString::get("client.module.blockPlacer.rateLimit.name"),
	           LocalizeString::get("client.module.blockPlacer.rateLimit.desc"), rateLimit);
	addSliderSetting("rateLimitWindow", LocalizeString::get("client.module.blockPlacer.rateLimitWindow.name"),
	                 LocalizeString::get("client.module.blockPlacer.rateLimitWindow.desc"), rateLimitWindowMs,
	                 FloatValue(10.f), FloatValue(300.f), FloatValue(10.f), "rateLimit"_istrue);
	addSliderSetting("rateLimitMax", LocalizeString::get("client.module.blockPlacer.rateLimitMax.name"),
	                 LocalizeString::get("client.module.blockPlacer.rateLimitMax.desc"), rateLimitMax,
	                 FloatValue(1.f), FloatValue(3.f), FloatValue(1.f), "rateLimit"_istrue);

	listen<UpdateEvent>((EventListenerFunc)&BlockPlacer::onTick);
}

void BlockPlacer::onEnable() {
	nextClick = std::chrono::steady_clock::now();
	wasPlacing = false;
	forcePlace = false;
	forcePlaceImmediate = false;
	placeHistory.clear();
	pendingPlace = false;
	pendingSlot = -1;
	pendingCount = 0;
	pendingAt = {};
}

void BlockPlacer::onDisable() {
	wasPlacing = false;
	forcePlace = false;
	forcePlaceImmediate = false;
	placeHistory.clear();
	pendingPlace = false;
	pendingSlot = -1;
	pendingCount = 0;
	pendingAt = {};
}

void BlockPlacer::updatePlaceCps(std::chrono::steady_clock::time_point now) {
	constexpr auto window = std::chrono::seconds(1);
	while (!placeCpsHistory.empty() && now - placeCpsHistory.front() >= window) {
		placeCpsHistory.pop_front();
	}
	placeCps = static_cast<int>(placeCpsHistory.size());
}

void BlockPlacer::onTick(Event&) {
	auto now = std::chrono::steady_clock::now();
	updatePlaceCps(now);

	auto mcGame = SDK::ClientInstance::get()->minecraftGame;
	if (!mcGame || !mcGame->isCursorGrabbed()) {
		return;
	}

	auto lp = SDK::ClientInstance::get()->getLocalPlayer();
	if (!lp || !lp->gameMode || !lp->supplies || !lp->supplies->inventory) {
		return;
	}

	{
		auto inv = lp->supplies;
		auto selected = inv->inventory->getItem(inv->selectedSlot);
		bool keepCurrent = isPreferredBlock(selected);

		if (!keepCurrent) {
			int bestSlot = findBestBlockSlot(inv);
			if (bestSlot != -1 && bestSlot != inv->selectedSlot) {
				sendHotbarKey(bestSlot);
				forcePlace = true;
				forcePlaceImmediate = true;
			}
		}
	}

	auto inv = lp->supplies;
	if (pendingPlace) {
		auto stack = inv->inventory->getItem(pendingSlot);
		if (!stack || !stack->valid || inv->selectedSlot != pendingSlot) {
			pendingPlace = false;
		} else if (stack->itemCount < pendingCount) {
			placeHistory.push_back(now);
			placeCpsHistory.push_back(now);
			updatePlaceCps(now);
			pendingPlace = false;
		} else if (now - pendingAt > std::chrono::milliseconds(500)) {
			pendingPlace = false;
		}
	}
	auto selected = inv->inventory->getItem(inv->selectedSlot);
	if (!isPreferredBlock(selected)) {
		wasPlacing = false;
		return;
	}

	if (!wasPlacing) {
		nextClick = now;
		wasPlacing = true;
	}
	if (forcePlace) {
		nextClick = now;
		forcePlace = false;
	}

	float cpsVal = std::clamp(std::get<FloatValue>(cps).value, 1.f, 100.f);
	auto interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
		std::chrono::duration<double>(1.0 / cpsVal));
	if (std::get<BoolValue>(rateLimit)) {
		float windowMs = std::get<FloatValue>(rateLimitWindowMs);
		int windowMsInt = std::clamp(static_cast<int>(windowMs), 10, 300);
		int maxPlacements = std::clamp(static_cast<int>(std::get<FloatValue>(rateLimitMax)), 1, 3);
		if (windowMsInt > 0) {
			const auto window = std::chrono::milliseconds(windowMsInt);
			while (!placeHistory.empty() && now - placeHistory.front() >= window) {
				placeHistory.pop_front();
			}
			if (static_cast<int>(placeHistory.size()) >= maxPlacements) {
				return;
			}
		}
	}
	if (forcePlaceImmediate) {
		sendRightClick();
		pendingPlace = true;
		pendingSlot = inv->selectedSlot;
		pendingCount = selected ? selected->itemCount : 0;
		pendingAt = now;
		nextClick = now + interval;
		forcePlaceImmediate = false;
		return;
	}
	if (now < nextClick) return;
	nextClick = now + interval;

	sendRightClick();
	pendingPlace = true;
	pendingSlot = inv->selectedSlot;
	pendingCount = selected ? selected->itemCount : 0;
	pendingAt = now;
}
