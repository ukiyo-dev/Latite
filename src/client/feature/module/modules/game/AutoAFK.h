#pragma once

#include "../../Module.h"
class AutoAFK : public Module {
public:
    AutoAFK();
    ~AutoAFK() override = default;

    void onEnable() override;
    void onDisable() override;
    void onTick(Event& ev);

private:
};
