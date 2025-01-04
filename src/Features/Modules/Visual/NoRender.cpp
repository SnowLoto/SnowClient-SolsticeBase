//
// Created by vastrakai on 11/14/2024.
//

#include "NoRender.hpp"

std::vector<unsigned char> gRetOvrPatch = { 0xC3, 0x90 };
DEFINE_PATCH_FUNC(NoRender::patchOverlay, SigManager::Unknown_renderBlockOverlay, gRetOvrPatch);

std::vector<unsigned char> gFrBytes = {0xC3}; // 0xC3 is the opcode for "ret"
DEFINE_PATCH_FUNC(NoRender::patchFireRender, SigManager::FireRender, gFrBytes);

void NoRender::onEnable()
{
    patchOverlay(mNoBlockOverlay.mValue);
    patchFireRender(mNoFire.mValue);
}

void NoRender::onDisable()
{
    patchOverlay(false);
    patchFireRender(false);
}
