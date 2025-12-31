#include "pch.h"
#include "CPSCounter.h"
#include "client/Latite.h"
#include "client/event/events/ClickEvent.h"
#include "client/feature/module/modules/game/BlockPlacer.h"

CPSCounter::CPSCounter() : TextModule("CPS", LocalizeString::get("client.textmodule.cpsCounter.name"),
                                      LocalizeString::get("client.textmodule.cpsCounter.desc"), HUD, 400.f, 0,
                                      true /*cache CPS text*/) {
    mode.addEntry(EnumEntry(0, LocalizeString::get("client.textmodule.cpsCounter.modeState0.name"),
                            LocalizeString::get("client.textmodule.cpsCounter.modeState0.name")));
    mode.addEntry(EnumEntry(1, LocalizeString::get("client.textmodule.cpsCounter.modeState1.name"),
                            LocalizeString::get("client.textmodule.cpsCounter.modeState1.name")));
    mode.addEntry(EnumEntry(2, LocalizeString::get("client.textmodule.cpsCounter.modeState2.name"),
                            LocalizeString::get("client.textmodule.cpsCounter.modeState2.name")));
    addEnumSetting("Mode", LocalizeString::get("client.textmodule.cpsCounter.mode.name"),
                   LocalizeString::get("client.textmodule.cpsCounter.mode.desc"), mode);
	addSetting("effectivePlace", LocalizeString::get("client.textmodule.cpsCounter.effectivePlace.name"),
		LocalizeString::get("client.textmodule.cpsCounter.effectivePlace.desc"), effectivePlace);

    std::get<TextValue>(this->prefix).str = L"CPS: ";

	listen<ClickEvent>((EventListenerFunc)&CPSCounter::onClick);
}

void CPSCounter::updateRightCps(std::chrono::steady_clock::time_point now) {
	constexpr auto window = std::chrono::seconds(1);
	while (!rightCpsHistory.empty() && now - rightCpsHistory.front() >= window) {
		rightCpsHistory.pop_front();
	}
	rightCps = static_cast<int>(rightCpsHistory.size());
}

void CPSCounter::onClick(Event& evGeneric) {
	auto& ev = reinterpret_cast<ClickEvent&>(evGeneric);
	if (ev.getMouseButton() != 2 || !ev.isDown()) {
		return;
	}
	auto now = std::chrono::steady_clock::now();
	rightCpsHistory.push_back(now);
	updateRightCps(now);
}

std::wstringstream CPSCounter::text(bool isDefault, bool inEditor) {
	std::wstringstream wss;
	auto cpsL = Latite::get().getTimings().getCPSL();
	auto cpsR = Latite::get().getTimings().getCPSR();
	int adjustedRight = cpsR;
	auto blockBase = Latite::getModuleManager().find("BlockPlacer");
	auto blockMod = blockBase ? static_cast<BlockPlacer*>(blockBase.get()) : nullptr;
	bool fallback = false;
	if (std::get<BoolValue>(effectivePlace)) {
		if (blockMod && blockMod->isEnabled()) {
			adjustedRight = blockMod->getPlaceCps();
		} else {
			fallback = true;
		}
	}
	if (fallback && !wasFallback) {
		rightCpsHistory.clear();
		rightCps = 0;
		if (blockMod) {
			auto now = std::chrono::steady_clock::now();
			constexpr auto window = std::chrono::seconds(1);
			for (const auto& when : blockMod->getPlaceHistory()) {
				if (now - when < window) {
					rightCpsHistory.push_back(when);
				}
			}
			updateRightCps(now);
		}
	}
	wasFallback = fallback;
	if (fallback) {
		updateRightCps(std::chrono::steady_clock::now());
		adjustedRight = rightCps;
	}
	switch (mode.getSelectedKey()) {
	case 0:
		wss << cpsL;
		break;
	case 1:
		wss << adjustedRight;
		break;
	case 2:
		wss << cpsL << " | " << adjustedRight;
		break;
	}
	return wss;
}

