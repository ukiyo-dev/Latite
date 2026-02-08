#include "pch.h"
#include "AutoTool.h"
#include "AutoEat.h"
#include "client/Latite.h"
#include "client/input/Keyboard.h"
#include "client/event/events/UpdateEvent.h"
#include "mc/common/world/actor/player/Player.h"
#include "mc/common/client/game/MinecraftGame.h"
#include "mc/common/world/level/Level.h"
#include "mc/common/world/level/HitResult.h"
#include "mc/common/world/level/BlockSource.h"
#include "mc/common/world/level/block/Block.h"
#include "mc/common/world/ItemStack.h"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <Windows.h>

namespace
{
    bool contains(std::string const &text, std::string_view needle)
    {
        return text.find(needle) != std::string::npos;
    }

    std::string toLower(std::string text)
    {
        for (char &ch : text)
        {
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        }
        return text;
    }

    std::string normalizeName(std::string name)
    {
        auto colon = name.find(':');
        if (colon != std::string::npos && colon + 1 < name.size())
        {
            name = name.substr(colon + 1);
        }
        if (name.rfind("tile.", 0) == 0)
        {
            name = name.substr(5);
        }
        return toLower(name);
    }

    std::string getMatchName(SDK::Item *item)
    {
        if (!item)
            return {};
        std::string name = item->namespacedId.getString();
        if (name.empty())
        {
            name = item->id.getString();
        }
        if (name.empty())
        {
            name = item->translateName;
        }
        return normalizeName(name);
    }

    int tierFromName(std::string const &name)
    {
        if (contains(name, "netherite"))
            return 6;
        if (contains(name, "diamond"))
            return 5;
        if (contains(name, "iron"))
            return 4;
        if (contains(name, "gold"))
            return 3;
        if (contains(name, "stone"))
            return 2;
        if (contains(name, "wooden") || contains(name, "wood"))
            return 1;
        return 0;
    }

    bool isSword(std::string const &name)
    {
        return contains(name, "sword");
    }

    bool isAxe(std::string const &name)
    {
        return contains(name, "axe") && !contains(name, "pickaxe");
    }

    bool isPickaxe(std::string const &name)
    {
        return contains(name, "pickaxe");
    }

    bool isShovel(std::string const &name)
    {
        return contains(name, "shovel");
    }

    bool isShears(std::string const &name)
    {
        return contains(name, "shears");
    }

    bool isTrident(std::string const &name)
    {
        return contains(name, "trident");
    }

    bool isArmorName(std::string const &name)
    {
        return contains(name, "helmet") ||
               contains(name, "chestplate") ||
               contains(name, "leggings") ||
               contains(name, "boots");
    }

    int scoreForWeapon(std::string const &name)
    {
        int tier = tierFromName(name);
        if (isTrident(name))
            return 104;
        if (isSword(name))
            return 100 + tier;
        if (isAxe(name))
            return 90 + tier;
        if (isPickaxe(name))
            return 80 + tier;
        return 0;
    }

    enum class ToolKind
    {
        Pickaxe,
        Axe,
        Shovel,
        Shears
    };

    int scoreForToolKind(ToolKind kind, std::string const &name)
    {
        int tier = tierFromName(name);
        switch (kind)
        {
        case ToolKind::Pickaxe:
            return isPickaxe(name) ? 100 + tier : 0;
        case ToolKind::Axe:
            return isAxe(name) ? 100 + tier : 0;
        case ToolKind::Shovel:
            return isShovel(name) ? 100 + tier : 0;
        case ToolKind::Shears:
            return isShears(name) ? 200 : 0;
        default:
            return 0;
        }
    }

    bool endsWith(std::string const &text, std::string_view suffix)
    {
        if (suffix.size() > text.size())
            return false;
        return std::equal(suffix.rbegin(), suffix.rend(), text.rbegin());
    }

    int blockPriority(std::string const &name)
    {
        if (endsWith(name, "terracotta"))
            return 0;
        if (endsWith(name, "wool"))
            return 1;
        if (endsWith(name, "stone"))
            return 2;
        if (endsWith(name, "_planks"))
            return 3;
        if (endsWith(name, "dirt") || contains(name, "dirt"))
            return 4;
        if (endsWith(name, "glass") || contains(name, "glass"))
            return 5;
        return -1;
    }

    bool isRightClickWhitelist(std::string const &name)
    {
        return endsWith(name, "sword") ||
               endsWith(name, "pickaxe") ||
               endsWith(name, "axe") ||
               endsWith(name, "shovel") ||
               endsWith(name, "shears") ||
               endsWith(name, "trident") ||
               endsWith(name, "diamond") ||
               endsWith(name, "emerald") ||
               endsWith(name, "ingot");
    }

    bool isPreferredBlock(SDK::ItemStack *stack)
    {
        if (!stack || !stack->valid || stack->itemCount == 0)
            return false;
        auto item = stack->getItem();
        if (!item)
            return false;
        return blockPriority(getMatchName(item)) >= 0;
    }

    int findBestBlockSlot(SDK::PlayerInventory *inv)
    {
        if (!inv || !inv->inventory)
            return -1;
        int bestSlot = -1;
        int bestPriority = 999;
        int bestCount = 0;

        for (int i = 0; i < 9; ++i)
        {
            auto stack = inv->inventory->getItem(i);
            if (!stack || !stack->valid || stack->itemCount == 0)
                continue;
            auto item = stack->getItem();
            if (!item)
                continue;
            auto name = getMatchName(item);
            int prio = blockPriority(name);
            if (prio < 0)
                continue;
            int count = static_cast<int>(stack->itemCount);
            if (prio < bestPriority || (prio == bestPriority && count > bestCount))
            {
                bestPriority = prio;
                bestCount = count;
                bestSlot = i;
            }
        }

        return bestSlot;
    }

    ToolKind toolForBlock(std::string const &name)
    {
        if (contains(name, "wool") || contains(name, "leaves") || contains(name, "leaf") ||
            contains(name, "vine") || contains(name, "cobweb") || contains(name, "web"))
        {
            return ToolKind::Shears;
        }

        if (contains(name, "log") || contains(name, "wood") || contains(name, "planks") ||
            contains(name, "stem") || contains(name, "hyphae") ||
            contains(name, "chest") || contains(name, "barrel") ||
            contains(name, "crafting_table") ||
            contains(name, "ladder") || contains(name, "fence") ||
            contains(name, "door") || contains(name, "sign"))
        {
            return ToolKind::Axe;
        }

        if (contains(name, "terracotta") || contains(name, "hardened_clay"))
        {
            return ToolKind::Pickaxe;
        }

        if (contains(name, "dirt") || contains(name, "grass") || contains(name, "sand") ||
            contains(name, "gravel") || contains(name, "clay") || contains(name, "snow") ||
            contains(name, "soul_sand") || contains(name, "mud") ||
            contains(name, "farmland") || contains(name, "path") || contains(name, "podzol"))
        {
            return ToolKind::Shovel;
        }

        return ToolKind::Pickaxe;
    }

    int findBestWeaponSlot(SDK::PlayerInventory *inv)
    {
        if (!inv || !inv->inventory)
            return -1;
        int bestSlot = -1;
        int bestScore = 0;
        for (int i = 0; i < 9; ++i)
        {
            auto stack = inv->inventory->getItem(i);
            if (!stack || !stack->valid || stack->itemCount == 0)
                continue;
            auto item = stack->getItem();
            if (!item)
                continue;
            int score = scoreForWeapon(getMatchName(item));
            if (score > bestScore)
            {
                bestScore = score;
                bestSlot = i;
            }
        }
        return bestSlot;
    }

    int findBestToolSlot(SDK::PlayerInventory *inv, ToolKind kind)
    {
        if (!inv || !inv->inventory)
            return -1;
        int bestSlot = -1;
        int bestScore = 0;
        for (int i = 0; i < 9; ++i)
        {
            auto stack = inv->inventory->getItem(i);
            if (!stack || !stack->valid || stack->itemCount == 0)
                continue;
            auto item = stack->getItem();
            if (!item)
                continue;
            int score = scoreForToolKind(kind, getMatchName(item));
            if (score > bestScore)
            {
                bestScore = score;
                bestSlot = i;
            }
        }
        return bestSlot;
    }

    std::string getBlockName(const SDK::HitResult *hr)
    {
        if (!hr)
            return {};
        auto region = SDK::ClientInstance::get()->getRegion();
        if (!region)
            return {};
        auto block = region->getBlock(hr->hitBlock);
        if (!block || !block->legacyBlock)
            return {};

        std::string name = block->legacyBlock->namespacedId.string;
        if (name.empty())
        {
            name = block->legacyBlock->translateName;
        }
        return normalizeName(name);
    }

    bool tryGetBlockName(const SDK::HitResult *hr, char *out, size_t outSize)
    {
        if (!hr || !out || outSize == 0)
            return false;
        out[0] = '\0';
        __try
        {
            auto region = SDK::ClientInstance::get()->getRegion();
            if (!region)
                return false;
            auto block = region->getBlock(hr->hitBlock);
            if (!block || !block->legacyBlock)
                return false;
            const std::string &nsStr = block->legacyBlock->namespacedId.string;
            const std::string &trStr = block->legacyBlock->translateName;
            const std::string *nameStr = &nsStr;
            if (nameStr->empty())
                nameStr = &trStr;
            if (nameStr->empty())
                return false;
            size_t len = nameStr->size();
            if (len >= outSize)
                len = outSize - 1;
            std::memcpy(out, nameStr->data(), len);
            out[len] = '\0';
            return true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            out[0] = '\0';
            return false;
        }
    }

    void sendHotbarKey(int slot)
    {
        int clamped = std::clamp(slot, 0, 8);
        std::string mapping = "hotbar." + std::to_string(clamped + 1);
        int vk = Latite::getKeyboard().getMappedKey(mapping);
        if (vk <= 0 || vk > 255)
            return;

        INPUT inputs[2]{};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wVk = static_cast<WORD>(vk);
        inputs[0].ki.wScan = static_cast<WORD>(MapVirtualKey(vk, MAPVK_VK_TO_VSC));
        inputs[1] = inputs[0];
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, inputs, sizeof(INPUT));
    }
}

AutoTool::AutoTool()
    : Module("AutoTool",
             LocalizeString::get("client.module.autoTool.name"),
             LocalizeString::get("client.module.autoTool.desc"),
             GAME,
             nokeybind)
{
    addSetting("rightClickBlock",
               LocalizeString::get("client.module.autoTool.rightClickBlock.name"),
               LocalizeString::get("client.module.autoTool.rightClickBlock.desc"),
               rightClickBlock);
    listen<UpdateEvent>((EventListenerFunc)&AutoTool::onTick);
}

void AutoTool::onEnable()
{
    cursorWasGrabbed = false;
    cursorGrabbedAt = {};
    weaponLocked = false;
    wasLeftDown = false;
    lastSelectedSlot = -1;
    lastSelectedName.clear();
    lastSelectedHadItem = false;
    suppressRightClickBlock = false;
}

void AutoTool::onDisable()
{
    cursorWasGrabbed = false;
    cursorGrabbedAt = {};
    weaponLocked = false;
    wasLeftDown = false;
    lastSelectedSlot = -1;
    lastSelectedName.clear();
    lastSelectedHadItem = false;
    suppressRightClickBlock = false;
}

void AutoTool::selectBestWeaponOnce()
{
	auto lp = SDK::ClientInstance::get()->getLocalPlayer();
	if (!lp || !lp->supplies || !lp->supplies->inventory)
		return;

	// Respect AutoEat: don't swap while eating/using.
	auto eatBase = Latite::getModuleManager().find("AutoEat");
	auto eatMod = eatBase ? static_cast<AutoEat*>(eatBase.get()) : nullptr;
	if (eatMod && eatMod->isActive())
		return;

	int bestSlot = findBestWeaponSlot(lp->supplies);
	if (bestSlot != -1 && bestSlot != lp->supplies->selectedSlot) {
		sendHotbarKey(bestSlot);
	}
}

void AutoTool::onTick(Event &)
{
    auto mcGame = SDK::ClientInstance::get()->minecraftGame;
    if (!mcGame)
    {
        cursorWasGrabbed = false;
        return;
    }

    bool grabbed = mcGame->isCursorGrabbed();
    if (grabbed && !cursorWasGrabbed)
    {
        cursorGrabbedAt = std::chrono::steady_clock::now();
    }
    cursorWasGrabbed = grabbed;
    if (!grabbed)
        return;
    if (std::chrono::steady_clock::now() - cursorGrabbedAt < std::chrono::milliseconds(100))
    {
        return;
    }

    bool leftDown = Latite::getKeyboard().isKeyDown(VK_LBUTTON);
    if (!leftDown)
    {
        leftDown = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
    }
    bool rightDown = Latite::getKeyboard().isKeyDown(VK_RBUTTON);
    if (!rightDown)
    {
        rightDown = (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0;
    }

    auto eatBase = Latite::getModuleManager().find("AutoEat");
    auto eatMod = eatBase ? static_cast<AutoEat *>(eatBase.get()) : nullptr;
    bool eatBlocked = eatMod && eatMod->isActive();

    auto lp = SDK::ClientInstance::get()->getLocalPlayer();
    if (!lp || !lp->supplies || !lp->supplies->inventory)
        return;

    auto level = SDK::ClientInstance::get()->minecraft->getLevel();
    if (!level)
        return;
    auto hr = level->getHitResult();
    SDK::HitType hitType = hr ? hr->hitType : SDK::HitType::AIR;
    auto inv = lp->supplies;
    auto selected = inv->inventory->getItem(inv->selectedSlot);
    bool selectedHasItem = selected && selected->valid && selected->itemCount > 0;
    std::string selectedName = selectedHasItem ? getMatchName(selected->getItem())
                                               : std::string{};

    if (inv->selectedSlot != lastSelectedSlot)
    {
        lastSelectedSlot = inv->selectedSlot;
        lastSelectedName = selectedName;
        lastSelectedHadItem = selectedHasItem;
        suppressRightClickBlock = false;
    }
    else
    {
        if (lastSelectedHadItem && !selectedHasItem && isArmorName(lastSelectedName))
        {
            suppressRightClickBlock = true;
        }
        else if (selectedHasItem)
        {
            suppressRightClickBlock = false;
        }
        else if (!lastSelectedHadItem)
        {
            suppressRightClickBlock = false;
        }

        if (selectedHasItem)
        {
            lastSelectedName = selectedName;
            lastSelectedHadItem = true;
        }
    }

    if (rightDown && std::get<BoolValue>(rightClickBlock) && !eatBlocked &&
        !suppressRightClickBlock)
    {
        bool keepCurrent = isPreferredBlock(selected);
        bool allowSwitch = keepCurrent || isRightClickWhitelist(selectedName) || selectedName.empty();
        if (allowSwitch && !keepCurrent)
        {
            int bestSlot = findBestBlockSlot(inv);
            if (bestSlot != -1 && bestSlot != inv->selectedSlot)
            {
                sendHotbarKey(bestSlot);
            }
        }
    }

    if (!leftDown)
    {
        weaponLocked = false;
        wasLeftDown = false;
        return;
    }
    bool justPressed = !wasLeftDown;
    if (justPressed)
    {
        weaponLocked = false;
    }
    wasLeftDown = true;
    if (eatBlocked)
        return;
    bool hitBlock = (hitType == SDK::HitType::BLOCK);
    bool hitEntity = (hitType == SDK::HitType::ENTITY);
    if (hitEntity)
    {
        weaponLocked = true;
    }

    int bestSlot = -1;
    if (weaponLocked)
    {
        bestSlot = findBestWeaponSlot(lp->supplies);
    }
    else if (hitBlock)
    {
        char nameBuf[256];
        if (!tryGetBlockName(hr, nameBuf, sizeof(nameBuf)))
            nameBuf[0] = '\0';
        std::string blockName = normalizeName(nameBuf);
        ToolKind kind = toolForBlock(blockName);
        bestSlot = findBestToolSlot(lp->supplies, kind);
        if (bestSlot == -1 && kind != ToolKind::Pickaxe)
        {
            bestSlot = findBestToolSlot(lp->supplies, ToolKind::Pickaxe);
        }
    }
    else if (justPressed && !hitEntity)
    {
        bestSlot = findBestWeaponSlot(lp->supplies);
    }

    if (bestSlot != -1 && bestSlot != lp->supplies->selectedSlot)
    {
        sendHotbarKey(bestSlot);
    }
}
