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

	bool shouldHoldToToggle() override { return true; }
	bool shouldListen() override { return true; }

private:
	bool isUsing = false;
	bool pendingStart = false;
	bool wasKeyDown = false;
	bool blockUntilRelease = false;
	int usingSlot = -1;
	std::chrono::steady_clock::time_point useStart = {};
	std::chrono::milliseconds useDuration = std::chrono::milliseconds(1600);
};
