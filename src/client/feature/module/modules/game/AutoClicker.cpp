#include "pch.h"
#include "AutoClicker.h"
#include "client/Latite.h"
#include "client/input/Keyboard.h"
#include "client/event/events/UpdateEvent.h"
#include "client/event/events/AttackEvent.h"
#include "mc/common/world/actor/player/Player.h"
#include "mc/common/client/game/MinecraftGame.h"
#include "mc/common/world/level/Level.h"
#include "mc/common/world/level/HitResult.h"
#include "mc/common/client/game/MouseInputPacket.h"
#include "mc/Addresses.h"
#include <algorithm>
#include <vector>
#include <Windows.h>

namespace {
        void pushMouseButton(bool down) {
                auto vec = reinterpret_cast<std::vector<MouseInputPacket>*>(Signatures::MouseInputVector.result);
                if (!vec) return;
                MouseInputPacket pkt{};
                pkt.type = 1;
                pkt.state = down ? 1 : 0;
                vec->push_back(pkt);
        }
}
AutoClicker::AutoClicker() : Module("AutoClicker", LocalizeString::get("client.module.autoClicker.name"),
                                    LocalizeString::get("client.module.autoClicker.desc"), GAME, nokeybind) {
        addSliderSetting("cps", LocalizeString::get("client.module.autoClicker.cps.name"),
                         LocalizeString::get("client.module.autoClicker.cps.desc"), this->cps, FloatValue(1.f),
                         FloatValue(20.f), FloatValue(1.f));
	addSetting("forceClick", LocalizeString::get("client.module.autoClicker.force.name"),
	           LocalizeString::get("client.module.autoClicker.force.desc"), this->restrictMode);
	addSetting("critMode", LocalizeString::get("client.module.autoClicker.crit.name"),
	           LocalizeString::get("client.module.autoClicker.crit.desc"), this->critMode);
	addSetting("proMode", LocalizeString::get("client.module.autoClicker.pro.name"),
	           LocalizeString::get("client.module.autoClicker.pro.desc"), this->proMode);
        addSliderSetting("graceMs", LocalizeString::get("client.module.autoClicker.grace.name"),
                         LocalizeString::get("client.module.autoClicker.grace.desc"), this->graceMs, FloatValue(0.f),
                         FloatValue(1000.f), FloatValue(100.f), "forceClick"_istrue);

        listen<UpdateEvent>((EventListenerFunc)&AutoClicker::onTick);
        listen<AttackEvent>((EventListenerFunc)&AutoClicker::onAttack);
}

void AutoClicker::onAttack(Event& evG) {
	auto& ev = reinterpret_cast<AttackEvent&>(evG);
	lastAttackTarget = ev.getActor();
	lastAttackAt = std::chrono::steady_clock::now();
}

void AutoClicker::onTick(Event&) {
        auto mcGame = SDK::ClientInstance::get()->minecraftGame;
        if (!mcGame) {
                shouldClick = false;
                cursorWasGrabbed = false;
                return;
        }
        bool grabbed = mcGame->isCursorGrabbed();
        if (grabbed && !cursorWasGrabbed) {
                cursorGrabbedAt = std::chrono::steady_clock::now();
        }
        cursorWasGrabbed = grabbed;
        if (!grabbed) {
                shouldClick = false;
                return;
        }
        if (std::chrono::steady_clock::now() - cursorGrabbedAt < std::chrono::milliseconds(100)) {
                return;
        }

        auto now = std::chrono::steady_clock::now();
        if (now < blockUntil) {
                shouldClick = false;
                wasKeyDown = false;
                return;
        }

        bool leftDown = false;
        leftDown = Latite::getKeyboard().isKeyDown(VK_LBUTTON);
        if (!leftDown) {
                leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
        }

        shouldClick = leftDown;
        bool justPressed = leftDown && !wasKeyDown;
        if (justPressed) {
                nextClick = now;
                lastKeyDown = now;
                hadEntityThisHold = false;
        }
        wasKeyDown = leftDown;
        if (!leftDown) {
                if (holdingBlock) {
                        pushMouseButton(false);
                        holdingBlock = false;
                }
                hadEntityThisHold = false;
                return;
        }

        auto lp = SDK::ClientInstance::get()->getLocalPlayer();
        if (!lp || !lp->gameMode) {
                return;
        }

        auto level = SDK::ClientInstance::get()->minecraft->getLevel();
        if (!level) {
                return;
        }

        auto hr = level->getHitResult();
        bool hitEntity = hr && hr->hitType == SDK::HitType::ENTITY;
        bool hitBlock = hr && hr->hitType == SDK::HitType::BLOCK;
        if (hitEntity) {
                hadEntityThisHold = true;
        }

        if (justPressed && hitBlock) {
                if (!holdingBlock) {
                        pushMouseButton(true);
                        holdingBlock = true;
                }
                return;
        }
        if (hitBlock && !holdingBlock && !hadEntityThisHold) {
                pushMouseButton(true);
                holdingBlock = true;
                return;
        }
        if (!hitBlock && holdingBlock) {
                pushMouseButton(false);
                holdingBlock = false;
        }
        if (holdingBlock) {
                return;
        }

        if (hitEntity && !wasHitEntity) {
                nextClick = now;
        }
        wasHitEntity = hitEntity;

        if (hitEntity) {
                lastEntityHit = now;
        }

        bool proOnly = std::get<BoolValue>(proMode);
        if (proOnly) {
                if (hitEntity && lastAttackTarget && (now - lastAttackAt) < std::chrono::milliseconds(2000)) {
                        if (lastAttackTarget->invulnerableTime > 0) {
                                wasInvulBlocked = true;
                                if (now < nextInvulSwing) {
                                        return;
                                }
                                nextInvulSwing = now + std::chrono::milliseconds(250);
                        }
                        if (wasInvulBlocked) {
                                nextClick = now;
                                wasInvulBlocked = false;
                        }
                } else {
                        wasInvulBlocked = false;
                        nextInvulSwing = now;
                }
        }

        bool critOnly = std::get<BoolValue>(critMode);
        if (critOnly) {
                auto sv = lp->stateVector;
                if (!sv) {
                        return;
                }
                if (sv->velocity.y > 0.05f) {
                        if (now < nextCritSwing) {
                                return;
                        }
                        nextCritSwing = now + std::chrono::milliseconds(250);
                } else {
                        nextCritSwing = now;
                }
        }

        if (now < nextClick) return;
        bool restrict = std::get<BoolValue>(restrictMode);
        float grace = std::clamp(std::get<FloatValue>(graceMs).value, 0.f, 500.f);
        auto graceWindow = std::chrono::milliseconds(static_cast<int>(grace));
        bool inGrace = (now - lastEntityHit <= graceWindow) || (now - lastKeyDown <= graceWindow);
        if (restrict && !hitEntity && !inGrace) {
                return;
        }

        auto sv = lp->stateVector;
        bool falling = critOnly && sv && sv->velocity.y < -0.05f && (sv->pos.y + 0.001f < sv->posOld.y);
        auto interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::milliseconds(50));
        if (!falling) {
                float cpsVal = std::clamp(std::get<FloatValue>(cps).value, 1.f, 20.f);
                interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::duration<double>(1.0 / cpsVal));
        }
        nextClick = now + interval;
        pushMouseButton(true);
        pushMouseButton(false);
}
void AutoClicker::onEnable() {
        shouldClick = false;
        wasKeyDown = false;
        holdingBlock = false;
        wasInvulBlocked = false;
        wasHitEntity = false;
        hadEntityThisHold = false;
        nextInvulSwing = std::chrono::steady_clock::now();
	lastAttackTarget = nullptr;
	lastAttackAt = {};
	nextCritSwing = std::chrono::steady_clock::now();
	blockUntil = {};
}

void AutoClicker::onDisable() {
        shouldClick = false;
        wasKeyDown = false;
        if (holdingBlock) {
                pushMouseButton(false);
                holdingBlock = false;
        }
        wasInvulBlocked = false;
        wasHitEntity = false;
        hadEntityThisHold = false;
        nextInvulSwing = std::chrono::steady_clock::now();
	lastAttackTarget = nullptr;
	lastAttackAt = {};
	nextCritSwing = std::chrono::steady_clock::now();
	blockUntil = {};
}

void AutoClicker::blockClicksFor(std::chrono::milliseconds duration) {
	blockUntil = std::chrono::steady_clock::now() + duration;
}




