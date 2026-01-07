#include "pch.h"
#include "AutoAFK.h"
#include "client/event/events/TickEvent.h"
#include "client/Latite.h"

AutoAFK::AutoAFK()
    : Module("AutoAFK",
             LocalizeString::get("client.module.autoAFK.name"),
             LocalizeString::get("client.module.autoAFK.desc"),
             GAME,
             0x4B) {
    listen<TickEvent>(static_cast<EventListenerFunc>(&AutoAFK::onTick));
}

void AutoAFK::onEnable() {
    Latite::getNotifications().push(LocalizeString::get("client.module.autoAFK.enabled"));
}

void AutoAFK::onDisable() {
    auto plr = SDK::ClientInstance::get()->getLocalPlayer();
    if (plr) {
        auto input = plr->getMoveInputComponent();
        if (input) {
            input->rawInputState.jumpDown = false;
        }
    }
    Latite::getNotifications().push(LocalizeString::get("client.module.autoAFK.disabled"));
}

void AutoAFK::onTick(Event&) {
    if (!isEnabled()) {
        return;
    }
    auto plr = SDK::ClientInstance::get()->getLocalPlayer();
    if (!plr) {
        return;
    }
    auto input = plr->getMoveInputComponent();
    if (!input) {
        return;
    }

    input->rawInputState.jumpDown = true;
}
