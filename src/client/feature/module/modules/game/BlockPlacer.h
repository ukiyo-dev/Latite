#pragma once
#include "../../Module.h"
#include <chrono>
#include <deque>

class BlockPlacer : public Module {
public:
	BlockPlacer();
	~BlockPlacer() = default;

	void onTick(Event& ev);
	bool shouldHoldToToggle() override { return true; }
	void onEnable() override;
	void onDisable() override;
	int getPlaceCps() const { return placeCps; }
	const std::deque<std::chrono::steady_clock::time_point>& getPlaceHistory() const { return placeCpsHistory; }

private:
	void updatePlaceCps(std::chrono::steady_clock::time_point now);

	ValueType cps = FloatValue(20.f);
	ValueType rateLimit = BoolValue(true);
	ValueType rateLimitWindowMs = FloatValue(300.f);
	ValueType rateLimitMax = FloatValue(3.f);
	std::chrono::steady_clock::time_point nextClick = std::chrono::steady_clock::now();
	bool wasPlacing = false;
	bool forcePlace = false;
	bool forcePlaceImmediate = false;
	std::deque<std::chrono::steady_clock::time_point> placeHistory;
	std::deque<std::chrono::steady_clock::time_point> placeCpsHistory;
	int placeCps = 0;
	bool pendingPlace = false;
	int pendingSlot = -1;
	uint8_t pendingCount = 0;
	std::chrono::steady_clock::time_point pendingAt = {};
};
