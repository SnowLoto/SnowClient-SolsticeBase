#pragma once
//
// Created by Solar on 11/18/2024.
//

#include <Features/Modules/Module.hpp>


class Goofy : public ModuleBase<Goofy> {
public:
    Goofy() : ModuleBase("Goofy", "Goofiest animations ever!", ModuleCategory::Visual, 0, false)
    {

        mNames = {
            {Lowercase, "goofy"},
            {LowercaseSpaced, "goofy"},
            {Normal, "Goofy"},
            {NormalSpaced, "Goofy"}
        };
    }

    void onEnable() override;
    void onDisable() override;
    void onBoneRenderEvent(class BoneRenderEvent& event);
};