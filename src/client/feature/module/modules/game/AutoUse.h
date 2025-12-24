#pragma once
#include "../../Module.h"
#include <chrono>

class AutoUse : public Module {
public:
	AutoUse();
	~AutoUse() = default;

	void onTick(Event& ev);
	void onEnable() override;
	void onDisable() override;

	bool shouldHoldToToggle() override { return false; }

private:
	ValueType key1 = KeyValue(0);
	ValueType key2 = KeyValue(0);
	ValueType key3 = KeyValue(0);
	ValueType pattern1 = TextValue(L"");
	ValueType pattern2 = TextValue(L"");
	ValueType pattern3 = TextValue(L"");
	bool isUsing = false;
	bool pendingStart = false;
	int activeKey = -1;
	std::array<bool, 3> blockUntilRelease = { false, false, false };
	std::array<bool, 3> wasKeyDown = { false, false, false };
	int usingSlot = -1;
	uint8_t usingCount = 0;
};
