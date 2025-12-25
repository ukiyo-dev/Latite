#pragma once
#include "../../Module.h"
#include <chrono>

namespace SDK {
	class Actor;
}

class AutoClicker : public Module {
public:
	AutoClicker();
	~AutoClicker() = default;

	void onTick(Event& ev);
	void onClick(Event& ev);
	void onAttack(Event& ev);
	void onEnable() override;
	void onDisable() override;

private:
	ValueType cps = FloatValue(10.f);
	ValueType forceClick = BoolValue(false);
	ValueType critMode = BoolValue(false);
	ValueType proMode = BoolValue(false);
	ValueType graceMs = FloatValue(500.f);
	int activeMode = 0;
	bool shouldClick = false;
	bool wasKeyDown = false;
	bool rightHeld = false;
	bool holdingBlock = false;
	bool blockToolApplied = false;
	bool weaponToolApplied = false;
	bool cursorWasGrabbed = false;
	bool wasInvulBlocked = false;
	std::chrono::steady_clock::time_point lastKeyDown = {};
	std::chrono::steady_clock::time_point lastEntityHit = {};
	std::chrono::steady_clock::time_point lastAttackAt = {};
	std::chrono::steady_clock::time_point nextWeaponCheck = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point nextClick = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point nextCritSwing = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point nextInvulSwing = std::chrono::steady_clock::now();
	std::chrono::steady_clock::time_point cursorGrabbedAt = {};
	SDK::Actor* lastAttackTarget = nullptr;
};
