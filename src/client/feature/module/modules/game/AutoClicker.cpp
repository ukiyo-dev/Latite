#include "pch.h"
#include "AutoClicker.h"
#include "AutoEat.h"
#include "AutoTool.h"
#include "client/Latite.h"
#include "client/input/Keyboard.h"
#include "client/event/events/UpdateEvent.h"
#include "mc/common/world/actor/player/Player.h"
#include "mc/common/world/actor/Actor.h"
#include "mc/common/client/game/MinecraftGame.h"
#include "mc/common/world/level/Level.h"
#include "mc/common/world/level/HitResult.h"
#include "mc/common/world/ItemStack.h"
#include "mc/common/client/game/MouseInputPacket.h"
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

	static SDK::Actor* findActorByEntityIdCached(SDK::Level* level,
	                                            uint32_t entityId,
	                                            SDK::Level** cacheLevel,
	                                            uint8_t* cacheNext,
	                                            std::array<uint32_t, 10>* cacheIds,
	                                            std::array<SDK::Actor*, 10>* cacheActors) {
		if (!level || entityId == 0) return nullptr;

		if (cacheLevel && *cacheLevel != level) {
			// Level changed (dimension/world). Invalidate cache.
			if (cacheNext) *cacheNext = 0;
			if (cacheIds) cacheIds->fill(0);
			if (cacheActors) cacheActors->fill(nullptr);
			*cacheLevel = level;
		}

		// Fast path: cache hit.
		if (cacheIds && cacheActors) {
			for (size_t i = 0; i < cacheIds->size(); i++) {
				if ((*cacheIds)[i] == entityId && (*cacheActors)[i]) {
					return (*cacheActors)[i];
				}
			}
		}

		SDK::Actor* found = nullptr;
		for (auto* actor : level->getRuntimeActorList()) {
			if (!actor) continue;
			if (actor->entityContext.getId() == entityId) {
				found = actor;
				break;
			}
		}

		if (cacheIds && cacheActors && cacheNext) {
			size_t idx = (*cacheNext) % cacheIds->size();
			(*cacheIds)[idx] = entityId;
			(*cacheActors)[idx] = found;
			*cacheNext = static_cast<uint8_t>((idx + 1) % cacheIds->size());
		}
		return found;
	}

	// Keep only "key" combat entities for aim-based logic.
	// Mapping based on .tmp/bedrock_1.21.130_protocol.json LegacyEntityType.
	static bool isCombatRelevantEntityType(uint8_t typeId) {
		// Always allow players.
		if (typeId == 63 /* player */) return true;

		// Filter common non-combat / utility entities.
		switch (typeId) {
			case 61: // armor_stand
			case 64: // item (dropped item)
			case 68: // xp_bottle
			case 69: // xp_orb
			case 73: // thrown_trident
			case 77: // fishing_hook
			case 80: // arrow
			case 81: // snowball
			case 82: // egg
			case 83: // painting
			case 84: // minecart
			case 90: // boat
			case 93: // lightning_bolt
			case 95: // area_effect_cloud
				return false;
			default:
				break;
		}

		// Treat everything else as a mob-ish combat target.
		return true;
	}

	// Teams detection: extract color code from nameTag (Hive-style §[color]Name).
	// Returns empty string if no color code found.
	static std::string extractTeamColorCode(const std::string& nameTag) {
		// Clean formatting codes (§r, §l, §o, §k, §m, §n)
		std::string cleaned;
		cleaned.reserve(nameTag.size());
		for (size_t i = 0; i < nameTag.size(); i++) {
			if (nameTag[i] == '\xC2' && i + 1 < nameTag.size() && nameTag[i + 1] == '\xA7') {
				// UTF-8 § (C2 A7)
				if (i + 2 < nameTag.size()) {
					char code = nameTag[i + 2];
					// Skip formatting codes: r, l, o, k, m, n
					if (code == 'r' || code == 'l' || code == 'o' || 
					    code == 'k' || code == 'm' || code == 'n') {
						i += 2;
						continue;
					}
				}
			} else if (nameTag[i] == '\xA7') {
				// Single-byte § (legacy)
				if (i + 1 < nameTag.size()) {
					char code = nameTag[i + 1];
					if (code == 'r' || code == 'l' || code == 'o' || 
					    code == 'k' || code == 'm' || code == 'n') {
						i += 1;
						continue;
					}
				}
			}
			cleaned += nameTag[i];
		}

		// Find first color code (§[0-9a-f])
		for (size_t i = 0; i < cleaned.size(); i++) {
			bool isSection = false;
			size_t codeOffset = 0;
			if (cleaned[i] == '\xC2' && i + 1 < cleaned.size() && cleaned[i + 1] == '\xA7') {
				isSection = true;
				codeOffset = 2;
			} else if (cleaned[i] == '\xA7') {
				isSection = true;
				codeOffset = 1;
			}
			if (isSection && i + codeOffset < cleaned.size()) {
				char code = cleaned[i + codeOffset];
				// Color codes: 0-9, a-f
				if ((code >= '0' && code <= '9') || (code >= 'a' && code <= 'f')) {
					return std::string(1, code);
				}
			}
		}
		return "";
	}

	static bool isOnSameTeam(SDK::Actor* localPlayer, SDK::Actor* target) {
		if (!localPlayer || !target) return false;
		std::string lpTeam = extractTeamColorCode(localPlayer->getNameTag());
		std::string targetTeam = extractTeamColorCode(target->getNameTag());
		if (lpTeam.empty() || targetTeam.empty()) return false;
		return lpTeam == targetTeam;
	}

	static int colorCodeIndex(char c) {
		if (c >= '0' && c <= '9') return c - '0';
		if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
		if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
		return -1;
	}

	static bool hasMultipleTeams(SDK::Level* level,
	                            bool& cached,
	                            std::chrono::steady_clock::time_point& nextCheck) {
		auto now = std::chrono::steady_clock::now();
		if (now < nextCheck) return cached;
		nextCheck = now + std::chrono::seconds(5);

		cached = false;
		if (!level) return false;

		bool seen[16]{};
		int uniqueCount = 0;

		for (auto* actor : level->getRuntimeActorList()) {
			if (!actor) continue;
			if (!actor->isPlayer()) continue;
			std::string team = extractTeamColorCode(actor->getNameTag());
			if (team.empty()) continue;
			int idx = colorCodeIndex(team[0]);
			if (idx < 0 || idx >= 16) continue;
			if (!seen[idx]) {
				seen[idx] = true;
				uniqueCount++;
			}
			if (uniqueCount >= 2) {
				cached = true;
				return true;
			}
		}
		return false;
	}

	static bool isInLobby(SDK::Player* lp,
	                      bool& cached,
	                      std::chrono::steady_clock::time_point& nextCheck) {
		auto now = std::chrono::steady_clock::now();
		if (now < nextCheck) return cached;
		nextCheck = now + std::chrono::seconds(5);

		if (!lp || !lp->supplies || !lp->supplies->inventory) {
			cached = false;
			return cached;
		}
		auto* inv = lp->supplies->inventory;
		for (int i = 0; i < 9; i++) {
			auto* stack = inv->getItem(i);
			if (!stack || !stack->valid || stack->itemCount == 0) continue;
			auto* item = stack->getItem();
			if (!item) continue;
			std::string id = item->namespacedId.getString();
			if (!id.empty() && id.find("minecraft:") != 0) {
				cached = true;
				return cached;
			}
		}
		cached = false;
		return cached;
	}

}
AutoClicker::AutoClicker() : Module("AutoClicker", LocalizeString::get("client.module.autoClicker.name"),
                                    LocalizeString::get("client.module.autoClicker.desc"), GAME, nokeybind) {
        addSliderSetting("cps", LocalizeString::get("client.module.autoClicker.cps.name"),
                         LocalizeString::get("client.module.autoClicker.cps.desc"), this->cps, FloatValue(1.f),
                         FloatValue(20.f), FloatValue(1.f));
	addSetting("autoMode", LocalizeString::get("client.module.autoClicker.auto.name"),
	           LocalizeString::get("client.module.autoClicker.auto.desc"), this->autoMode);
	addSetting("teamsMode", LocalizeString::get("client.module.autoClicker.teams.name"),
	           LocalizeString::get("client.module.autoClicker.teams.desc"), this->teamsMode, "autoMode");
	addSetting("proMode", LocalizeString::get("client.module.autoClicker.pro.name"),
	           LocalizeString::get("client.module.autoClicker.pro.desc"), this->proMode);
	// Note: removed legacy "restrict mode" grace window; auto mode is aim-based.
	addSetting("smartCrit", LocalizeString::get("client.module.autoClicker.smartCrit.name"),
	           LocalizeString::get("client.module.autoClicker.smartCrit.desc"), this->smartCritMode);

	listen<UpdateEvent>((EventListenerFunc)&AutoClicker::onTick);
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

		bool leftDown = Latite::getKeyboard().isKeyDown(VK_LBUTTON);
		if (!leftDown) leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;

		bool autoModeEnabled = std::get<BoolValue>(autoMode);
		// Holding LMB always falls back to original manual click logic
		// (air/entity spam), even when auto mode is enabled.
		bool autoActive = autoModeEnabled && !leftDown;

		shouldClick = autoModeEnabled || leftDown;

		bool justPressed = false;
		if (!autoActive) {
			justPressed = leftDown && !wasKeyDown;
			if (justPressed) {
				nextClick = now;
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
		} else {
			// Auto mode doesn't use the hold-to-click state machine.
			wasKeyDown = leftDown;
			if (holdingBlock) {
				pushMouseButton(false);
				holdingBlock = false;
			}
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

		// Resolve entity under crosshair (HitResult stores EntityContext id).
		SDK::Actor* crosshairActor = nullptr;
		if (hitEntity && hr) {
			crosshairActor = findActorByEntityIdCached(level,
			                                          hr->entity.id,
			                                          &cachedHitLevel,
			                                          &cachedHitNext,
			                                          &cachedHitEntityIds,
			                                          &cachedHitActors);
		}

		// Auto mode: strictly target players only.
		// Manual mode / ProMode: keep broader combat filter.
		if (hitEntity) {
			if (!crosshairActor) {
				hitEntity = false;
			} else if (autoActive) {
				hitEntity = crosshairActor->isPlayer();
				if (hitEntity && std::get<BoolValue>(teamsMode)) {
					if (isInLobby(lp, cachedIsInLobby, lobbyCheckNext)) {
						hitEntity = false;
					} else if (hasMultipleTeams(level, cachedHasMultipleTeams, teamCheckNext)
					           && isOnSameTeam(lp, crosshairActor)
					           && !leftDown) {
						hitEntity = false;
					}
				}
			} else {
				hitEntity = isCombatRelevantEntityType(crosshairActor->getEntityTypeID());
			}
		}


        if (hitEntity) {
                hadEntityThisHold = true;
        }

		if (!autoActive && justPressed && hitBlock) {
			if (!holdingBlock) {
				pushMouseButton(true);
				holdingBlock = true;
			}
			return;
		}
		if (!autoActive && hitBlock && !holdingBlock && !hadEntityThisHold) {
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

	bool critMode = std::get<BoolValue>(smartCritMode);
	bool rising = false;
	if (critMode && lp->stateVector) {
		float dy = lp->stateVector->pos.y - lp->stateVector->posOld.y;
		rising = dy > 0.01f;
	}

	bool proOnly = std::get<BoolValue>(proMode);
	if (proOnly) {
		if (hitEntity && hr) {
			uint32_t entityId = hr->entity.id;
			auto target = crosshairActor ? crosshairActor : findActorByEntityIdCached(level,
			                                                                  entityId,
			                                                                  &cachedHitLevel,
			                                                                  &cachedHitNext,
			                                                                  &cachedHitEntityIds,
			                                                                  &cachedHitActors);
			// Threshold: allow attacks only when the target is about to leave i-frames.
			if (target && target->invulnerableTime > 1) {
				return;
			}
		}
	}

		if (now < nextClick) return;
		if (!shouldClick) return;
		if (autoActive && !hitEntity) {
			return;
		}
		auto eatBase = Latite::getModuleManager().find("AutoEat");
		auto eatMod = eatBase ? static_cast<AutoEat*>(eatBase.get()) : nullptr;
		if (eatMod && eatMod->isEnabled() && eatMod->isActive()) return;

        float cpsVal = std::clamp(std::get<FloatValue>(cps).value, 1.f, 20.f);
        double seconds = 1.0 / cpsVal;
        if (lastSwingWasRising) seconds *= 2.0;
        auto interval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(seconds));
		nextClick = now + interval;
		lastSwingWasRising = rising;
		// If AutoTool is enabled, let it pick the best weapon
		// right before we inject the click.
		if (autoActive && hitEntity) {
			auto toolBase = Latite::getModuleManager().find("AutoTool");
			auto tool = toolBase ? static_cast<AutoTool*>(toolBase.get()) : nullptr;
			if (tool && tool->isEnabled()) {
				tool->selectBestWeaponOnce();
			}
		}
		pushMouseButton(true);
		pushMouseButton(false);
}
void AutoClicker::onEnable() {
        shouldClick = false;
        wasKeyDown = false;
        holdingBlock = false;
        wasHitEntity = false;
        hadEntityThisHold = false;
	cachedHitLevel = nullptr;
	cachedHitNext = 0;
	cachedHitEntityIds.fill(0);
	cachedHitActors.fill(nullptr);
	cachedIsInLobby = false;
	lobbyCheckNext = {};
	cachedHasMultipleTeams = false;
	teamCheckNext = {};
	lastSwingWasRising = false;
}

void AutoClicker::onDisable() {
        shouldClick = false;
        wasKeyDown = false;
        if (holdingBlock) {
                pushMouseButton(false);
                holdingBlock = false;
        }
        wasHitEntity = false;
        hadEntityThisHold = false;
	cachedHitLevel = nullptr;
	cachedHitNext = 0;
	cachedHitEntityIds.fill(0);
	cachedHitActors.fill(nullptr);
	cachedIsInLobby = false;
	lobbyCheckNext = {};
	cachedHasMultipleTeams = false;
	teamCheckNext = {};
	lastSwingWasRising = false;
}
