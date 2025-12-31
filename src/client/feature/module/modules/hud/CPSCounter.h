#pragma once
#include "../../TextModule.h"
#include "client/feature/setting/Setting.h"
#include <chrono>
#include <deque>

class CPSCounter : public TextModule {
public:
	CPSCounter();

	std::wstringstream text(bool isDefault, bool inEditor) override;
	void onClick(Event& ev);

	EnumData mode;
	ValueType effectivePlace = BoolValue(false);

private:
	void updateRightCps(std::chrono::steady_clock::time_point now);
	std::deque<std::chrono::steady_clock::time_point> rightCpsHistory;
	int rightCps = 0;
	bool wasFallback = false;
};
