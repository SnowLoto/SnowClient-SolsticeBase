//
// Created by Solar on 11/18/2024.
//

#include "Goofy.hpp"

#include <Features/Events/BoneRenderEvent.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Actor/ActorPartModel.hpp>

void Goofy::onEnable()
{
    gFeatureManager->mDispatcher->listen<BoneRenderEvent, &Goofy::onBoneRenderEvent>(this);
}

void Goofy::onDisable()
{
    gFeatureManager->mDispatcher->deafen<BoneRenderEvent, &Goofy::onBoneRenderEvent>(this);
}

void Goofy::onBoneRenderEvent(BoneRenderEvent& event)
{
    auto ent = event.mActor;
    auto player = ClientInstance::get()->getLocalPlayer();
    auto bone = event.mBone;
    auto partModel = event.mPartModel;

    if (ent != ClientInstance::get()->getLocalPlayer()) return;

    if (bone->mBoneStr == "rightarm" || bone->mBoneStr == "leftarm")
    {
        if ((!ClientInstance::get()->getMouseGrabbed()))
        {
            player->getWalkAnimationComponent()->mWalkAnimSpeed = 0.79;

            if (Keyboard::isUsingMoveKeys() && player->getSwingProgress() == 0)
            {
                partModel->mRot.z *= 3.f;
                partModel->mRot.x *= 1.4f;
            }

            if (Keyboard::isUsingMoveKeys())
            {
                float mAngle = bone->mBoneStr == "rightarm" ? 10.38 : -10.38;
                partModel->mRot.z = mAngle;
            }
        }
    }
}