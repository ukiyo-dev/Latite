#pragma once
#include "../../Module.h"
#include <chrono>

class AutoEat : public Module {
public:
	AutoEat();
	~AutoEat() = default;

	void onTick(Event& ev);
	void onEnable() override;
	void onDisable() override;

	bool shouldHoldToToggle() override { return false; }
	[[nodiscard]] bool isActive() const { return isUsing || pendingStart; }

private:
	bool isUsing = false;
	bool pendingStart = false;
	bool wasKeyDown = false;
	int usingSlot = -1;
	int pendingSlot = -1;
	int lastSelectedSlot = -1;
	bool useActiveSeen = false;
	std::chrono::steady_clock::time_point useStart = {};
	std::chrono::steady_clock::time_point ignoreInterruptUntil = {};
	std::chrono::milliseconds useDuration = std::chrono::milliseconds(1650);
	ValueType triggerKey = KeyValue(0);
};
