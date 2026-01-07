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
    void onAttack(Event& ev);
	void onEnable() override;
	void onDisable() override;
    void blockClicksFor(std::chrono::milliseconds duration);

private:
	ValueType cps = FloatValue(10.f);
        ValueType restrictMode = BoolValue(false);
	ValueType critMode = BoolValue(false);
	ValueType proMode = BoolValue(false);
	ValueType graceMs = FloatValue(500.f);
        bool shouldClick = false;
        bool wasKeyDown = false;
        bool holdingBlock = false;
        bool cursorWasGrabbed = false;
        bool wasInvulBlocked = false;
        bool wasHitEntity = false;
        bool hadEntityThisHold = false;
        std::chrono::steady_clock::time_point lastKeyDown = {};
        std::chrono::steady_clock::time_point lastEntityHit = {};
        std::chrono::steady_clock::time_point lastAttackAt = {};
        std::chrono::steady_clock::time_point nextClick = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point nextCritSwing = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point nextInvulSwing = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point cursorGrabbedAt = {};
        std::chrono::steady_clock::time_point blockUntil = {};
        SDK::Actor* lastAttackTarget = nullptr;
};
