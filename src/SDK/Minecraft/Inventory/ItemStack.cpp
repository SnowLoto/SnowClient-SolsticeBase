//
// Created by vastrakai on 11/19/2024.
//
#include "ItemStack.hpp"

#include <SDK/Minecraft/Actor/Actor.hpp>
#include <SDK/Minecraft/World/Level.hpp>

struct _SampleLol
{
    PAD(0x80);
};

static std::unique_ptr<_SampleLol> _smplData = std::make_unique<_SampleLol>();

ItemStack ItemStack::fromDescriptor(class NetworkItemStackDescriptor const& desc)
{
    // send mental help
    static auto func = SigManager::ItemStack_fromDescriptor;

    auto palette = ClientInstance::get()->getLocalPlayer()->getLevel()->getBlockPalette();

    ItemStack item = ItemStack();
    MemUtils::callFastcall<ItemStack*, ItemStack*, NetworkItemStackDescriptor const&, BlockPalette*, bool>(func, &item, desc, palette, true);
    return item;
}
