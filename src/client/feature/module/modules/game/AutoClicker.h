#pragma once
#include "../../Module.h"
#include <chrono>
#include <array>
#include <cstdint>

#include "util/LMath.h"

namespace SDK {
	class Actor;
	class Level;
}

class AutoClicker : public Module {
public:
	AutoClicker();
	~AutoClicker() = default;

	void onTick(Event& ev);
	void onEnable() override;
	void onDisable() override;

private:
	ValueType cps = FloatValue(10.f);
	ValueType autoMode = BoolValue(false);
	ValueType teamsMode = BoolValue(false);
	ValueType proMode = BoolValue(false);

	ValueType smartCritMode = BoolValue(false);

	bool shouldClick = false;
	bool wasKeyDown = false;
	bool holdingBlock = false;
	bool cursorWasGrabbed = false;
	bool wasHitEntity = false;
	bool hadEntityThisHold = false;
	std::chrono::steady_clock::time_point nextClick = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point cursorGrabbedAt = {};

	// Cache: hitResult entityId -> Actor* (avoids scanning every tick).
	static constexpr size_t kHitEntityCacheSize = 10;
	SDK::Level* cachedHitLevel = nullptr;
	uint8_t cachedHitNext = 0;
	std::array<uint32_t, kHitEntityCacheSize> cachedHitEntityIds{};
	std::array<SDK::Actor*, kHitEntityCacheSize> cachedHitActors{};

	bool cachedIsInLobby = false;
	std::chrono::steady_clock::time_point lobbyCheckNext = {};
	bool cachedHasMultipleTeams = false;
	std::chrono::steady_clock::time_point teamCheckNext = {};

	bool lastSwingWasRising = false;
};
