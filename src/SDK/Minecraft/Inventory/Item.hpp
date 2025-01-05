//
// Created by vastrakai on 7/5/2024.
//

#pragma once

#include <string>
#include <Utils/MemUtils.hpp>

enum class SItemType
{
    Helmet,
    Chestplate,
    Leggings,
    Boots,
    Sword,
    Pickaxe,
    Axe,
    Shovel,
    None
};

class Item {
public:
    CLASS_FIELD(short, mItemId, 0xA2);
    CLASS_FIELD(int, mArmorItemType, 0x244);
    CLASS_FIELD(int, mProtection, 0x264);
    CLASS_FIELD(std::string, mName, 0xD0);

    int getArmorSlot();
    bool isHelmet();
    bool isChestplate();
    bool isLeggings();
    bool isBoots();
    bool isSword();
    bool isPickaxe();
    bool isAxe();
    bool isShovel();
    int getItemTier();
    SItemType getItemType();

};