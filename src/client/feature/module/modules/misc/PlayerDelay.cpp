#include "pch.h"
#include "PlayerDelay.h"

PlayerDelay::PlayerDelay()
	: Module("PlayerDelay", LocalizeString::get("client.module.playerDelay.name"),
		LocalizeString::get("client.module.playerDelay.desc"), GAME, Module::nokeybind) {
	addSliderSetting("delayMs", LocalizeString::get("client.module.playerDelay.delayMs.name"),
		LocalizeString::get("client.module.playerDelay.delayMs.desc"),
		delayMs, FloatValue(0.f), FloatValue(200.f), FloatValue(1.f));
}

int PlayerDelay::getDelayMs() const {
	return static_cast<int>(std::round(std::get<FloatValue>(delayMs).value));
}
