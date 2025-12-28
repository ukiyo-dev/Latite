#pragma once
#include "client/feature/module/Module.h"

class PlayerDelay : public Module {
public:
	PlayerDelay();
	~PlayerDelay() = default;

	int getDelayMs() const;

private:
	ValueType delayMs = FloatValue(0.f);
};
