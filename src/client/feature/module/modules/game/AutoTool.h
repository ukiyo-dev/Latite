#pragma once

#include "../../Module.h"
#include <chrono>
#include <string>

class AutoTool : public Module {
public:
    AutoTool();
    ~AutoTool() = default;

    void onTick(Event& ev);
    void onEnable() override;
    void onDisable() override;

	// Used by other modules (e.g. AutoClicker auto mode)
	// to quickly select the best weapon from the hotbar.
	void selectBestWeaponOnce();

    bool shouldHoldToToggle() override { return false; }

private:
    bool cursorWasGrabbed = false;
    std::chrono::steady_clock::time_point cursorGrabbedAt = {};
    bool weaponLocked = false;
    bool wasLeftDown = false;
    int lastSelectedSlot = -1;
    std::string lastSelectedName = {};
    bool lastSelectedHadItem = false;
    bool suppressRightClickBlock = false;
    ValueType rightClickBlock = BoolValue(false);
};
