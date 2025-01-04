#include "Regen.hpp"

#include <Features/FeatureManager.hpp>
#include <Features/Events/BaseTickEvent.hpp>
#include <Features/Events/PacketOutEvent.hpp>
#include <Features/Events/PacketInEvent.hpp>
#include <Features/Events/PingUpdateEvent.hpp>
#include <Features/Events/SendImmediateEvent.hpp>
#include <Features/Modules/Visual/Interface.hpp>
#include <SDK/Minecraft/ClientInstance.hpp>
#include <SDK/Minecraft/Inventory/PlayerInventory.hpp>
#include <SDK/Minecraft/Actor/GameMode.hpp>
#include <SDK/Minecraft/Network/PacketID.hpp>
#include <SDK/Minecraft/Network/Packets/InventoryTransactionPacket.hpp>
#include <SDK/Minecraft/Network/Packets/PlayerAuthInputPacket.hpp>
#include <SDK/Minecraft/Network/Packets/MobEquipmentPacket.hpp>
#include <SDK/Minecraft/Network/Packets/LevelEventPacket.hpp>
#include <SDK/Minecraft/Network/Packets/UpdateBlockPacket.hpp>
#include <SDK/Minecraft/World/BlockLegacy.hpp>
#include <SDK/Minecraft/World/Level.hpp>
#include <SDK/Minecraft/World/HitResult.hpp>
#include <Features/Modules/Player/ChestStealer.hpp>
#include <Features/Modules/Player/Scaffold.hpp>

void Regen::initializeRegen()
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    GameMode* gm = player->getGameMode();

    if (mIsMiningBlock) {
        gm->stopDestroyBlock(mCurrentBlockPos);
    }
    mCurrentBlockPos = { INT_MAX, INT_MAX, INT_MAX };
    mTargettingBlockPos = { INT_MAX, INT_MAX, INT_MAX };
    mCurrentBlockFace = -1;
    mBreakingProgress = 0.f;
    mShouldSpoofSlot = true;
    mIsMiningBlock = false;
    mIsUncovering = false;
    mIsStealing = false;
    mToolSlot = -1;
    mOffGround = false;
    mCanReplace = false;
    mStartDestroyCount = 0;

    mFakePositions.clear();
}

void Regen::resetSyncSpeed()
{
    mWasUncovering = false;
    mLastTargettingBlockPos = { INT_MAX, INT_MAX, INT_MAX };
    mLastTargettingBlockPosDestroySpeed = 1.f;
    mLastToolSlot = 0;
}

bool Regen::isValidBlock(glm::ivec3 blockPos, bool redstoneOnly, bool exposedOnly, bool isStealing)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    Block* block = ClientInstance::get()->getBlockSource()->getBlock(blockPos);
    if (!block) return false;

    // Air Check
    if (block->getmLegacy()->isAir()) return false;

    // BlockID Check
    int blockId = block->getmLegacy()->getBlockId();
    bool isRedstone = blockId == 73 || blockId == 74;
    if (redstoneOnly) {
        if (!isRedstone) return false;
    }

    // Distance Check
    AABB blockAABB = AABB(blockPos, glm::vec3(1, 1, 1));
    glm::vec3 closestPos = blockAABB.getClosestPoint(*player->getPos());
    if (mRange.mValue < glm::distance(closestPos, *player->getPos())) return false;

    // Exposed Check
    int exposedFace = BlockUtils::getExposedFace(blockPos);
    bool canSkipExposedCheck = false;
    float percentage = (mBreakingProgress / mCurrentDestroySpeed);
    canSkipExposedCheck = mIsMiningBlock && !mIsUncovering && blockPos == mCurrentBlockPos && mAntiCover.mValue && mCompensation.mValue <= percentage && percentage <= 1;
    if (exposedOnly && (!isStealing || !isRedstone) && !canSkipExposedCheck) {
        if (exposedFace == -1) return false;
    }

    // Steal
    if (isStealing && (!mCanSteal || mTargettingBlockPos != mEnemyTargettingBlockPos) && exposedFace == -1) {
        if (mDebug.mValue && mStealNotify.mValue) ChatUtils::displayClientMessage("§esteal cancelled");
        return false;
    }

#ifdef __PRIVATE_BUILD__
    // Anti Steal
    if ((mAntiSteal.mValue || antiStealerEnabled) && blockPos == mBlackListedOrePos && exposedFace == -1) return false;
#endif
#ifdef __DEBUG__
    // Black List Check
    if (mAvoidEnemyOre.mValue && std::find(mOreBlackList.begin(), mOreBlackList.end(), blockPos) != mOreBlackList.end()) return false;
#endif

    return true;
}

bool Regen::isValidRedstone(glm::ivec3 blockPos)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return false;

    int blockID = ClientInstance::get()->getBlockSource()->getBlock(blockPos)->getmLegacy()->getBlockId();

    // BlockID Check
    if (blockID != 73 && blockID != 74) return false;

    // BlockPos Check
    if (mBlockRange.mValue < glm::distance(*player->getPos(), glm::vec3(blockPos)) || blockPos == mTargettingBlockPos) return false;

    // IsMiningPosition Check
    if (BlockUtils::isMiningPosition(blockPos)) return false;

    // Place position check
    int exposedFace = BlockUtils::getExposedFace(blockPos);
    if (exposedFace != -1 && mIsMiningBlock) {
        glm::ivec3 placePos = blockPos + mOffsetList[exposedFace];
        glm::ivec3 deltaPos = mCurrentBlockPos - placePos;
        if (abs(deltaPos.x) + abs(deltaPos.y) + abs(deltaPos.z) <= 1) return false;
    }

    return true;
}

void Regen::queueBlock(glm::ivec3 blockPos)
{
    Block* block = ClientInstance::get()->getBlockSource()->getBlock(blockPos);
    mCurrentBlockPos = blockPos;
    mCurrentBlockFace = BlockUtils::getExposedFace(blockPos);
    if (mCurrentBlockFace == -1) mCurrentBlockFace = 0;
    mIsMiningBlock = true;
    mBreakingProgress = 0.f;
    int bestToolSlot = ItemUtils::getBestBreakingTool(block, mHotbarOnly.mValue);
    float destroySpeed = ItemUtils::getDestroySpeed(bestToolSlot, block);
    if (mCurrentDestroySpeed <= destroySpeed) mShouldRotate = true;
    PacketUtils::spoofSlot(bestToolSlot, false);
    mShouldSpoofSlot = false;
    BlockUtils::startDestroyBlock(blockPos, mCurrentBlockFace);
    mToolSlot = bestToolSlot;
    mShouldSetbackSlot = true;

    if (mDynamicDestroySpeed.mValue) {
        std::string blockName = block->getmLegacy()->getmName();
        for (auto& c : BlockUtils::mDynamicSpeeds) {
            if (c.blockName == blockName) {
                if (mDebug.mValue && mFastOreNotify.mValue) ChatUtils::displayClientMessage("§agaming");
                break;
            }
        }
    }
}

Regen::PathFindingResult Regen::getBestPathToBlock(glm::ivec3 blockPos)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return { glm::ivec3(INT_MAX, INT_MAX, INT_MAX), 0 };

    BlockSource* source = ClientInstance::get()->getBlockSource();
    if (!source) return { glm::ivec3(INT_MAX, INT_MAX, INT_MAX), 0 };

    float currentBreakingTime = 0;
    float bestBreakingTime = INT_MAX;
    glm::ivec3 bestPos = { INT_MAX, INT_MAX, INT_MAX };

    if (mUncoverMode.mValue == UncoverMode::Normal) {
        bool foundPath = false;
        std::vector<BlockInfo> blockList = BlockUtils::getBlockList(blockPos, mUncoverRange.mValue);
        for (auto& block : blockList) {
            if (!BlockUtils::isAirBlock(block.mPosition) && BlockUtils::getExposedFace(block.mPosition) != -1) {
                glm::ivec3 delta = block.mPosition - blockPos;
                float dist = abs(delta.x) + abs(delta.y) + abs(delta.z);
                if (dist < bestBreakingTime) {
                    bestBreakingTime = dist;
                    bestPos = block.mPosition;
                    foundPath = true;
                }
            }
        }
        if(!foundPath) return { glm::ivec3(INT_MAX, INT_MAX, INT_MAX), 0 };
    }
    else if (mUncoverMode.mValue == UncoverMode::Fast) {

        static std::vector<glm::ivec3> offsetList = {
            glm::ivec3(0, 1, 0),
            glm::ivec3(0, 0, -1),
            glm::ivec3(0, 0, 1),
            glm::ivec3(-1, 0, 0),
            glm::ivec3(1, 0, 0),
        };

        for (int i = 0; i < 5; i++) {
            currentBreakingTime = 0;
            glm::ivec3 pos1 = blockPos + offsetList[i];
            Block* currentBlock = source->getBlock(pos1);
            bool isAir = BlockUtils::isAirBlock(pos1);
            float blockBreakingTime1 = 0;
            if (!isAir) blockBreakingTime1 = 1 / ItemUtils::getDestroySpeed(ItemUtils::getBestBreakingTool(currentBlock, mHotbarOnly.mValue), currentBlock);
            for (int i2 = 0; i2 < 5; i2++) {
                if (-offsetList[i] == offsetList[i2]) continue;
                glm::ivec3 pos2 = pos1 + offsetList[i2];
                Block* currentBlock2 = source->getBlock(pos2);
                bool isAir2 = BlockUtils::isAirBlock(pos2);
                float blockBreakingTime2 = 0;
                if (!isAir2) blockBreakingTime2 = 1 / ItemUtils::getDestroySpeed(ItemUtils::getBestBreakingTool(currentBlock2, mHotbarOnly.mValue), currentBlock2);
                currentBreakingTime = blockBreakingTime1 + blockBreakingTime2;
                if (currentBreakingTime < bestBreakingTime) {
                    bestBreakingTime = currentBreakingTime;
                    if (!isAir2) bestPos = pos2;
                    else bestPos = pos1;
                }
            }
        }
    }

    return { bestPos, bestBreakingTime };
}

void Regen::onEnable()
{
    gFeatureManager->mDispatcher->listen<BaseTickEvent, &Regen::onBaseTickEvent, nes::event_priority::VERY_LAST>(this);
    gFeatureManager->mDispatcher->listen<PacketOutEvent, &Regen::onPacketOutEvent, nes::event_priority::VERY_LAST>(this);
    gFeatureManager->mDispatcher->listen<PacketInEvent, &Regen::onPacketInEvent>(this);
    gFeatureManager->mDispatcher->listen<SendImmediateEvent, &Regen::onSendImmediateEvent, nes::event_priority::VERY_LAST>(this);
    gFeatureManager->mDispatcher->listen<PingUpdateEvent, &Regen::onPingUpdateEvent, nes::event_priority::VERY_FIRST>(this);

    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    mShouldRotate = false;
    mIsMiningBlock = false;
    mEnemyTargettingBlockPos = { 0, 0, 0 };
    miningRedstones.clear();

    resetSyncSpeed();

    initializeRegen();
}

void Regen::onDisable()
{
    gFeatureManager->mDispatcher->deafen<BaseTickEvent, &Regen::onBaseTickEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketOutEvent, &Regen::onPacketOutEvent>(this);
    gFeatureManager->mDispatcher->deafen<PacketInEvent, &Regen::onPacketInEvent>(this);
    gFeatureManager->mDispatcher->deafen<SendImmediateEvent, &Regen::onSendImmediateEvent>(this);
    gFeatureManager->mDispatcher->deafen<PingUpdateEvent, &Regen::onPingUpdateEvent>(this);

    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    if (mIsMiningBlock) {
        player->getSupplies()->mSelectedSlot = mToolSlot;
        player->getGameMode()->stopDestroyBlock(mCurrentBlockPos);
        mIsMiningBlock = false;
    }
    mWasMiningBlock = false;
}

void Regen::onBaseTickEvent(BaseTickEvent& event) {
    mWasMiningBlock = mIsMiningBlock;

    auto player = event.mActor;

    bool gaming = false;
    if (!mIsMiningBlock) gaming = false;

#ifndef __PRIVATE_BUILD__

    if(!mUncover.mValue) {
        stealEnabled = false;
    } else if(mUncover.mValue && !stealEnabled) {
        stealEnabled = true;
    }

#else
    stealEnabled = true;  // lilycat asked me to make release builds not able to use stealer with toggled off uncover
#endif

    if (mDynamicUncover.mValue) {
        for (auto& pos : mLastBrokenOrePos) {
            // detect opponent broke exposed ore
            if (BlockUtils::isAirBlock(pos)) {
                //
            }
        }
        for (auto &pos: mLastBrokenCoveringBlockPos) {
            // detect opponent uncovered ore
            if (BlockUtils::isAirBlock(pos)) {
                if(mDebug.mValue && mDynamicUncoverNotify.mValue) ChatUtils::displayClientMessage("§eenemy uncovered ore");
                mLastUncoverDetected = NOW;
                break;
            }
        }
    }

    mLastBrokenOrePos.clear();
    mLastBrokenCoveringBlockPos.clear();

#ifdef __PRIVATE_BUILD__
    if (!mAntiSteal.mValue) {
        if (mEnableAntiSteal.mValue) {
            if (NOW < lastStealerDetected + 5000) {
                antiStealerEnabled = true;
            } else {
                antiStealerEnabled = false;
            }
        } else if (!mEnableAntiSteal.mValue && antiStealerEnabled) antiStealerEnabled = false;
    }
#endif

    if (mUncover.mValue) {
        if (mDynamicUncover.mValue && NOW < mLastUncoverDetected + (mDisableDuration.mValue * 1000)) {
            mCurrentUncover = false;
        } else if (mDisableUncover.mValue && stealerDetected &&
                   NOW < uncoverDisabledTime + (mDisableSeconds.mValue * 1000)) {
            mCurrentUncover = false;
        } else if (mNoUncoverWhileStealing.mValue && NOW < lastStoleTime + 5000) {
            mCurrentUncover = false;
        } else {
            mCurrentUncover = true;
        }
    } else mCurrentUncover = false;

    BlockSource *source = ClientInstance::get()->getBlockSource();
    if (!source) return;
    PlayerInventory *supplies = player->getSupplies();
    mPreviousSlot = supplies->getmSelectedSlot();

    float absorption = player->getAbsorption();
    bool maxAbsorption = 10 <= absorption;
    bool steal = mSteal.mValue && (mCanSteal || mIsStealing) && isValidBlock(mEnemyTargettingBlockPos, true, false) && stealEnabled;

    bool isStealDelayed = false;
    Block* coveredBlock = source->getBlock(mLastEnemyLayerBlockPos);
    int coveredBlockToolSlot = ItemUtils::getBestBreakingTool(coveredBlock, mHotbarOnly.mValue);
    uint64_t coveredBlockBreakingTime = NOW - (mLastStealerUpdate - mPing);
    float coveredBlockDestroySpeed = ItemUtils::getDestroySpeed(coveredBlockToolSlot, coveredBlock);
    float coveredBlockProgress = coveredBlockDestroySpeed * (coveredBlockBreakingTime / 50);
#ifdef __PRIVATE_BUILD__
    isStealDelayed = mDelayedSteal.mValue && steal && !mIsStealing && coveredBlockProgress < (mOpponentDestroySpeed.mValue - coveredBlockDestroySpeed) && player->isOnGround();
#endif

    int pickaxeSlot = ItemUtils::getBestItem(SItemType::Pickaxe, mHotbarOnly.mValue);
    ItemStack *stack = supplies->getContainer()->getItem(pickaxeSlot);
    bool hasPickaxe = stack->mItem && stack->getItem()->getItemType() == SItemType::Pickaxe;

    // Stealer Timeout
    if (mCanSteal && mLastStealerUpdate + mStealerTimeout.mValue <= NOW) {
        mCanSteal = false;
        if (mDebug.mValue && mStealNotify.mValue) {
            ChatUtils::displayClientMessage("§cstealer timeouted!");
        }
    }

    // Replacer
#ifdef __PRIVATE_BUILD__
    if (mReplace.mValue) {
        if (mCanReplace && hasPickaxe) {
            int hardestBlockSlot = ItemUtils::getHardestBlock(pickaxeSlot, mHotbarOnly.mValue);
            if (0 <= hardestBlockSlot) {
                glm::ivec3 placePos = mCurrentBlockPos;
                glm::ivec3 hitPos = placePos + glm::ivec3(0, -1, 0);
                if (BlockUtils::isAirBlock(placePos) && !BlockUtils::isAirBlock(hitPos)) {
                    mCurrentPlacePos = placePos;
                    mShouldRotateToPlacePos = true;
                    mPreviousSlot = supplies->mSelectedSlot;

                    supplies->mSelectedSlot = hardestBlockSlot;
                    PacketUtils::spoofSlot(hardestBlockSlot, false);
                    if (mSwing.mValue) player->swing();
                    BlockUtils::placeBlock(placePos, 1);
                    if (mDebug.mValue && mRaperNotify.mValue) ChatUtils::displayClientMessage("§etrying to flag enemy");
                    mShouldSetbackSlot = true;
                    mShouldSpoofSlot = true;
                    supplies->mSelectedSlot = mPreviousSlot;
                    mLastReplacedPos = placePos;
                    mLastReplaced = NOW;
                    mCanReplace = false;
                    return;
                }
            }
        }
#ifdef __PRIVATE_BUILD__
        if (mReplace.mValue) {
            if (NOW <= mLastReplaced + 1000 && BlockUtils::isAirBlock(mLastReplacedPos)) {
                if (mDebug.mValue && mRaperNotify.mValue) ChatUtils::displayClientMessage("§aenemy flagged!");
                mLastReplaced = 0;
            }
        }
#endif
    }
#endif


    // Ore Blocker
    if (mBlockOre.mValue) {
        bool placedBlock = false;
        if (!miningRedstones.empty()) {
            if (0 < ItemUtils::getAllPlaceables(mHotbarOnly.mValue)) {
                for (auto &pos: miningRedstones) {
                    if (mMulti.mValue) {
                        // Multi ore blocker
                        std::vector<glm::ivec3> placePositions;
                        int exposedFace = BlockUtils::getExposedFace(pos);
                        if (exposedFace == -1 || !isValidRedstone(pos)) continue;
                        for (int i = 0; i < 6; i++) {
                            glm::ivec3 placePos = pos + mOffsetList[i];
                            glm::ivec3 hitPos = placePos + glm::ivec3(0, -1, 0);
                            if (!BlockUtils::isAirBlock(placePos) || BlockUtils::isAirBlock(hitPos)) continue;
                            if (player->getPos()->y < placePos.y) continue;
                            placePositions.push_back(placePos);
                        }

                        // avoid crash
                        if (placePositions.empty()) continue;

                        mCurrentPlacePos = pos;
                        mShouldRotateToPlacePos = true;
                        mPreviousSlot = supplies->mSelectedSlot;
                        int blockSlot = ItemUtils::getPlaceableItemOnBlock(placePositions[0], mHotbarOnly.mValue,
                                                                           false);
                        if (blockSlot == -1) continue;

                        supplies->mSelectedSlot = blockSlot;
                        PacketUtils::spoofSlot(blockSlot, false);
                        if (mSwing.mValue) player->swing();
                        for (auto &placePos: placePositions) {
                            BlockUtils::placeBlock(placePos, 1);
                        }
                        if (mDebug.mValue && mBlockNotify.mValue) ChatUtils::displayClientMessage("§acovered enemy's ore");
                        mShouldSetbackSlot = true;
                        mShouldSpoofSlot = true;
                        supplies->mSelectedSlot = mPreviousSlot;
                        placedBlock = true;
                        break;
                    } else {
                        // Single ore blocker
                        int exposedFace = BlockUtils::getExposedFace(pos);
                        if (exposedFace == -1 || !isValidRedstone(pos)) continue;
                        glm::ivec3 placePos = pos + mOffsetList[exposedFace];
                        glm::ivec3 hitPos = placePos + glm::ivec3(0, -1, 0);
                        if (ClientInstance::get()->getBlockSource()->getBlock(hitPos)->mLegacy->isAir()) continue;
                        if (!BlockUtils::isAirBlock(placePos)) continue;
                        if (player->getPos()->y < placePos.y) continue;

                        mCurrentPlacePos = placePos;
                        mShouldRotateToPlacePos = true;
                        mPreviousSlot = supplies->mSelectedSlot;
                        int blockSlot = ItemUtils::getPlaceableItemOnBlock(placePos, mHotbarOnly.mValue, false);
                        if (blockSlot == -1) continue;

                        supplies->mSelectedSlot = blockSlot;
                        PacketUtils::spoofSlot(blockSlot, false);
                        if (mSwing.mValue) player->swing();
                        BlockUtils::placeBlock(placePos, 1);
                        if (mDebug.mValue && mBlockNotify.mValue) ChatUtils::displayClientMessage("§acovered enemy's ore");
                        mShouldSetbackSlot = true;
                        mShouldSpoofSlot = true;
                        supplies->mSelectedSlot = mPreviousSlot;
                        placedBlock = true;
                        break;
                    }
                }
            }
        }
        miningRedstones.clear();
        if (placedBlock) return;
    }

    auto chestStealer = gFeatureManager->mModuleManager->getModule<ChestStealer>();
    bool mStealing = chestStealer && chestStealer->mEnabled && chestStealer->mIsStealing;

    auto scaffold = gFeatureManager->mModuleManager->getModule<Scaffold>();
    bool isScaffold = scaffold && scaffold->mEnabled;

    // Return if maxAbsorption is reached, OR if a block was placed in the last 200ms
    if (maxAbsorption && !mAlwaysMine.mValue && !mQueueRedstone.mValue && (!mAlwaysSteal.mValue || !steal || isStealDelayed) ||
        player->getStatusFlag(ActorFlags::Noai) || !hasPickaxe || player->isDestroying() || mStealing || isScaffold) {
        initializeRegen();
        resetSyncSpeed();
        if (mIsConfuserActivated) {
            player->getGameMode()->stopDestroyBlock(mLastConfusedPos);
            mIsConfuserActivated = false;
        }

        if (mShouldSetbackSlot) {
            PacketUtils::spoofSlot(mPreviousSlot, false);
            mShouldSetbackSlot = false;
        }
        //mCanSteal = false;
        return;
    }

    if (mStealerDetecter.mValue) {
        if (startedStealerDetection) {
            if (NOW > stealerDetectionStartTime + 5000) {
                if (!(amountOfStolenBlocks > mAmountOfBlocksToDetect.mValue)) {
                    if (mDebug.mValue && mStealerDetectorNotify.mValue) ChatUtils::displayClientMessage("§ereset");
                    startedStealerDetection = false;
                    stealerDetectionStartTime = 0;
                    amountOfStolenBlocks = 0;
                }
            }
        }
    }

    if (mIsMiningBlock && source->getBlock(mTargettingBlockPos)->mLegacy->isAir()) {
        if (mDebug.mValue && mStealNotify.mValue) ChatUtils::displayClientMessage("§cyour ore was stolen!");

        if (mStealerDetecter.mValue) {
            if (startedStealerDetection) {
                if (NOW > stealerDetectionStartTime + 5000) {
                    if (amountOfStolenBlocks >= mAmountOfBlocksToDetect.mValue) {
                        stealerDetected = true;
                        mCurrentUncover = false;
                        uncoverDisabledTime = NOW;
                        startedStealerDetection = false;
                        stealerDetectionStartTime = 0;
                        amountOfStolenBlocks = 0;
                        lastStealerDetected = NOW;
                        if(mDebug.mValue && mStealerDetectorNotify.mValue) ChatUtils::displayClientMessage("§estealer detected");
                    }
                } else {
                    amountOfStolenBlocks++;
                }
            } else {
                stealerDetectionStartTime = NOW;
                startedStealerDetection = true;
                amountOfStolenBlocks = 1;
                if(mDebug.mValue && mStealerDetectorNotify.mValue) ChatUtils::displayClientMessage("§astarted stealer detection");
            }
        }
    }

    bool shouldChangeOre = false;
    if (mStealPriority.mValue == StealPriority::Steal && steal && (!mIsStealing || mTargettingBlockPos != mEnemyTargettingBlockPos)) {
        if (mAntiConfuse.mValue && mAntiConfuseMode.mValue == AntiConfuseMode::RedstoneCheck) {
            std::vector<BlockInfo> blockList = BlockUtils::getBlockList(*player->getPos(), mRange.mValue);
            std::vector<BlockInfo> exposedBlockList;

            for (int i = 0; i < blockList.size(); i++) {
                if (isValidBlock(blockList[i].mPosition, true, true)) {
                    exposedBlockList.push_back(blockList[i]);
                } else continue;
            }
            if (exposedBlockList.empty()) shouldChangeOre = true;
        } else shouldChangeOre = true;

        if (isStealDelayed) shouldChangeOre = false;
    }

    if (
#ifdef __PRIVATE_BUILD__
        isValidBlock(mCurrentBlockPos, !mIsUncovering, !mIsUncovering, mIsStealing) && isValidBlock(mTargettingBlockPos, true, false) &&
#else
        isValidBlock(mCurrentBlockPos, !mCurrentUncover, !mIsUncovering, mIsStealing) && mTargettingBlockPos != mBlackListedOrePos &&
#endif
        !shouldChangeOre) { // Check if current block is valid
        Block* currentBlock = source->getBlock(mCurrentBlockPos);
        int exposedFace = BlockUtils::getExposedFace(mCurrentBlockPos);
        int bestToolSlot = ItemUtils::getBestBreakingTool(currentBlock, mHotbarOnly.mValue);
        mToolSlot = bestToolSlot;
        if (mShouldSpoofSlot) {
            PacketUtils::spoofSlot(bestToolSlot, false);
            mShouldSpoofSlot = false;
            return;
        }

        bool isRedstone =
            currentBlock->getmLegacy()->getBlockId() == 73 || currentBlock->getmLegacy()->getBlockId() == 74;

        float destroySpeed = 1.f;

        bool wasOnGround = player->isOnGround();

        if (!wasOnGround) mOffGround = true;

        switch (mCalcMode.mValue) {
        case CalcMode::Minecraft:
            destroySpeed = ItemUtils::getDestroySpeed(bestToolSlot, currentBlock);
            break;
#ifdef __PRIVATE_BUILD__
        case CalcMode::Test:
            destroySpeed = ItemUtils::getDestroySpeed(bestToolSlot, currentBlock);
            break;
#endif
        }

        bool synchedSpeed = false;
        resetSyncSpeed();
        bool nukedBlock = false;
        if (!mDynamicDestroySpeed.mValue || (mOnGroundOnly.mValue && mOffGround)) {
            if (isRedstone) {
#ifdef __PRIVATE_BUILD__
                if (mCalcMode.mValue == CalcMode::Test) {
                    mCurrentDestroySpeed = 0.66666672f;
                }
                else {
                    mCurrentDestroySpeed = mDestroySpeed.mValue;
                }
#else
                mCurrentDestroySpeed = mDestroySpeed.mValue;
#endif
            }
            else {
#ifdef __PRIVATE_BUILD__
                if (mCalcMode.mValue == CalcMode::Test) {
                    mCurrentDestroySpeed = 0.66666672f;
                }
                else {
                    mCurrentDestroySpeed = mOtherDestroySpeed.mValue;
                }
#else
                mCurrentDestroySpeed = mOtherDestroySpeed.mValue;
#endif
            }
        }
        else {
            std::string blockName = currentBlock->getmLegacy()->getmName();
            bool found = false;
            if (mOnGroundOnly.mValue && mNuke.mValue) {
                ItemStack* stack = supplies->getContainer()->getItem(mToolSlot);
                bool selectedShovel = stack && stack->mItem && stack->getItem()->getItemType() == SItemType::Shovel;
                if (selectedShovel) {
                    for (auto& c : BlockUtils::mNukeSpeeds) {
                        if (c.blockName == blockName) {
                            mCurrentDestroySpeed = c.destroySpeed;
                            found = true;
                            nukedBlock = true;
                            break;
                        }
                    }
                }
            }
            if (!found) {
                for (auto& c : BlockUtils::mDynamicSpeeds) {
                    if (c.blockName == blockName) {
                        mCurrentDestroySpeed = c.destroySpeed;
                        found = true;
                        break;
                    }
                }
            }
            if (!found) {
                if (isRedstone) {
#ifdef __PRIVATE_BUILD__
                    if (mCalcMode.mValue == CalcMode::Test) {
                        mCurrentDestroySpeed = 0.66666672f;
                    }
                    else {
                        mCurrentDestroySpeed = mDestroySpeed.mValue;
                    }
#else
                    mCurrentDestroySpeed = mDestroySpeed.mValue;
#endif
                }
                else {
#ifdef __PRIVATE_BUILD__
                    if (mCalcMode.mValue == CalcMode::Test) {
                        mCurrentDestroySpeed = 0.66666672f;
                    }
                    else {
                        mCurrentDestroySpeed = mOtherDestroySpeed.mValue;
                    }
#else
                    mCurrentDestroySpeed = mOtherDestroySpeed.mValue;
#endif
                }
            }
            else {
                if (!gaming) {
                    spdlog::debug("Found dynamic speed for block: {} [{}%]", blockName,
                        static_cast<int>(mCurrentDestroySpeed * 100));
                    gaming = true;
                }
            }
        }


        mBreakingProgress += destroySpeed;

        // Confuse 2
        bool shouldConfuse = mConfuse2.mValue && (mConfuseMode.mValue == ConfuseMode::Always ||
            (mConfuseMode.mValue == ConfuseMode::Auto &&
                mLastStealerDetected + mConfuseDuration.mValue > NOW));
        if (shouldConfuse && 0.1 <= mBreakingProgress && mIsUncovering) {
            initializeRegen();
            resetSyncSpeed();
            if(mDebug.mValue && mConfuseNotify.mValue) ChatUtils::displayClientMessage("§aconfused enemy's stealer");
            return;
        }


        bool finishBreak = true;
        if (maxAbsorption && isRedstone && !mAlwaysMine.mValue && !mIsStealing) finishBreak = false;

        if ((mCurrentDestroySpeed <= mBreakingProgress && (!mIsStealing || exposedFace != -1)) && finishBreak) {
            mShouldRotate = true;
            supplies->mSelectedSlot = bestToolSlot;
            if (mSwing.mValue) player->swing();
            BlockUtils::destroyBlock(mCurrentBlockPos, exposedFace, mInfiniteDurability.mValue);
            mCanReplace = mIsStealing || (2 <= mStartDestroyCount && !mIsUncovering);
            if (mIsStealing) {
                if (mDebug.mValue && mStealNotify.mValue) ChatUtils::displayClientMessage("§astole enemy's ore");
                if (mNoUncoverWhileStealing.mValue)
                    lastStoleTime = NOW;
            }
            if (mDebug.mValue && mSyncSpeedNotify.mValue && synchedSpeed)
                ChatUtils::displayClientMessage("§asynched destroy speed");

            if (nukedBlock)
                if (mDebug.mValue && mNukeNotify.mValue) ChatUtils::displayClientMessage("§enuked block");
            supplies->mSelectedSlot = mPreviousSlot;
            mIsMiningBlock = false;

            mWasUncovering = mIsUncovering;
            mLastTargettingBlockPos = mTargettingBlockPos;
            mLastTargettingBlockPosDestroySpeed = destroySpeed;
            mLastToolSlot = bestToolSlot;
            return;
        }
    } else { // Find new block
        initializeRegen();
        std::vector<BlockInfo> blockList = BlockUtils::getBlockList(*player->getPos(), mRange.mValue);
        std::vector<BlockInfo> exposedBlockList;
        std::vector<BlockInfo> unexposedBlockList;

        for (int i = 0; i < blockList.size(); i++) {
            if (isValidBlock(blockList[i].mPosition, true, false)) {
                if (BlockUtils::getExposedFace(blockList[i].mPosition) != -1) exposedBlockList.push_back(blockList[i]);
                else unexposedBlockList.push_back(blockList[i]);
            } else continue;
        }

        if (mCanSteal && mSteal.mValue && stealEnabled && !isStealDelayed && isValidBlock(mEnemyTargettingBlockPos, true, false)) {
            if (!(mAntiConfuse.mValue && mAntiConfuseMode.mValue == AntiConfuseMode::RedstoneCheck) || exposedBlockList.empty()) {
                queueBlock(mEnemyTargettingBlockPos);
                mTargettingBlockPos = mEnemyTargettingBlockPos;
                mIsStealing = true;
                return;
            }
        }

        glm::vec3 playerPos = *player->getPos();
        glm::ivec3 pos = {INT_MAX, INT_MAX, INT_MAX};
        glm::ivec3 targettingPos = {INT_MAX, INT_MAX, INT_MAX};
        if (!exposedBlockList.empty()) {
            // Confuser
            bool shouldConfuse = mConfuse.mValue && (mConfuseMode.mValue == ConfuseMode::Always ||
                                                     (mConfuseMode.mValue == ConfuseMode::Auto &&
                                                      mLastStealerDetected + mConfuseDuration.mValue > NOW));
            if (shouldConfuse) {
                if (mIsConfuserActivated) {
                    player->getGameMode()->stopDestroyBlock(mLastConfusedPos);
                    mIsConfuserActivated = false;
                } else if (!unexposedBlockList.empty()) {
                    glm::ivec3 confusePos = unexposedBlockList[0].mPosition + glm::ivec3(0, -1, 0);
                    BlockUtils::startDestroyBlock(confusePos, 0);
                    mLastConfusedPos = confusePos;
                    mIsConfuserActivated = true;
                    mLastConfuse = NOW;
                    if (mDebug.mValue && mConfuseNotify.mValue) ChatUtils::displayClientMessage("§aconfused enemy's stealer");
                    return;
                }
            }
            float closestDistance = INT_MAX;
            bool foundTargettingBlock = false;
            for (int i = 0; i < exposedBlockList.size(); i++) {
                glm::vec3 blockPos = exposedBlockList[i].mPosition;
                float dist = glm::distance(playerPos, blockPos);
                if (dist < closestDistance) {
                    closestDistance = dist;
                    pos = blockPos;
                    targettingPos = blockPos;
                }
                if (glm::ivec3(blockPos) == mLastTargettingBlockPos && mWasUncovering) foundTargettingBlock = true;
            }
            if (mOreSelectionMode.mValue == OreSelectionMode::Normal && foundTargettingBlock) {
                if (pos != mLastTargettingBlockPos) {
                    pos = mLastTargettingBlockPos;
                }
            }
#ifdef __PRIVATE_BUILD__
            // Ore Faker
            if (mOreFaker.mValue) {
                Actor *target = nullptr;
                auto actors = ActorUtils::getActorList(false, true);

                std::ranges::sort(actors, [&](Actor *a, Actor *b) -> bool {
                    return a->distanceTo(player) < b->distanceTo(player);
                });

                for (auto actor: actors) {
                    if (actor == player) continue;
                    target = actor;
                    break;
                }

                if (target) {
                    std::vector<BlockInfo> fakeBlocks = BlockUtils::getBlockList(*target->getPos(), mRange.mValue);
                    std::vector<glm::ivec3> fakeExposedBlocks;
                    std::vector<glm::ivec3> fakeUnexposedBlocks;

                    for (int i = 0; i < fakeBlocks.size(); i++) {
                        int blockId = fakeBlocks[i].mBlock->mLegacy->getBlockId();
                        if (blockId == 73 || blockId == 74) {
                            if (BlockUtils::getExposedFace(fakeBlocks[i].mPosition) != -1)
                                fakeExposedBlocks.push_back(fakeBlocks[i].mPosition);
                            else fakeUnexposedBlocks.push_back(fakeBlocks[i].mPosition);
                        } else continue;
                    }
                    if (mExposed.mValue) {
                        for (int i = 0; i < fakeExposedBlocks.size(); i++) {
                            if (fakeExposedBlocks[i] == pos) continue;

                            BlockUtils::startDestroyBlock(fakeExposedBlocks[i], 0);
                            mFakePositions.push_back(fakeExposedBlocks[i]);
                        }
                    }
                    if (mUnexposed.mValue) {
                        for (int i = 0; i < fakeUnexposedBlocks.size(); i++) {
                            BlockUtils::startDestroyBlock(fakeUnexposedBlocks[i], 0);
                            mFakePositions.push_back(fakeUnexposedBlocks[i]);
                        }
                    }
                    if (mCovering.mValue) {
                        for (int i = 0; i < fakeUnexposedBlocks.size(); i++) {
                            glm::ivec3 currentBlockPos = fakeUnexposedBlocks[i] + mOffsetList[1];
                            if (ClientInstance::get()->getBlockSource()->getBlock(currentBlockPos)->mLegacy->mSolid) {
                                BlockUtils::startDestroyBlock(currentBlockPos, 0);
                                mFakePositions.push_back(currentBlockPos);
                            }
                        }
                    }
                }
            }
#endif
            // queue block
            queueBlock(pos);
            mTargettingBlockPos = targettingPos;
            return;
        } else if (mCurrentUncover && !unexposedBlockList.empty()) {
            bool foundBlock = false;
            bool isNextToRedstone = false;
            float fastestTime = INT_MAX;
            for (int i = 0; i < unexposedBlockList.size(); i++) {
                glm::ivec3 redstonePos = unexposedBlockList[i].mPosition;
                PathFindingResult result = getBestPathToBlock(redstonePos);
                float currentTime = result.time;
                if (currentTime >= fastestTime || !isValidBlock(result.blockPos, false, false)) continue;

                for (auto &[face, offset]: BlockUtils::blockFaceOffsets) {
                    int ID = source->getBlock(result.blockPos + glm::ivec3(offset))->getmLegacy()->getBlockId();
                    if (ID == 73 || ID == 74) {
                        isNextToRedstone = true;
                        break;
                    }
                }

                if (BlockUtils::getExposedFace(result.blockPos) == -1 && isNextToRedstone) continue;

                fastestTime = currentTime;
                pos = result.blockPos;
                targettingPos = redstonePos;
                foundBlock = true;
            }
            if (foundBlock) {
                queueBlock(pos);
                mIsUncovering = true;
                mTargettingBlockPos = targettingPos;
                return;
            }
        }

        if (mShouldSetbackSlot) {
            PacketUtils::spoofSlot(mPreviousSlot, false);
            mShouldSetbackSlot = false;
        }

        resetSyncSpeed();
    }
}


void Regen::onRenderEvent(RenderEvent& event)
{
    // NOTE: Keep in mind if you have a RenderEvent that needs something to animate out when disabled,
    //       you need to put the listen call in the module's constructor without any deafen call.

    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    if (mRenderBlock.mValue) renderBlock();
    if (mOreFaker.mValue && mRenderFakeOre.mValue) renderFakeOres();
    if (mRenderProgressBar.mValue) {
        if (mProgressBarStyle.mValue == ProgressBarStyle::Old) {
            renderProgressBar();
        } else if (mProgressBarStyle.mValue == ProgressBarStyle::New)
        {
            renderNewProgressBar();
        }
    }
}

void Regen::renderProgressBar()
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    static float lastProgress = 0.f;
    float percentDone = 1.f;

    percentDone = mBreakingProgress;
    percentDone /= mCurrentDestroySpeed;
    if (percentDone < lastProgress) lastProgress = percentDone;
    percentDone = MathUtils::lerp(lastProgress, percentDone, ImGui::GetIO().DeltaTime * 30.f);
    lastProgress = percentDone;
    // clamp percentDone
    percentDone = MathUtils::clamp(percentDone, 0.f, 1.f);

    float delta = ImGui::GetIO().DeltaTime;

    static EasingUtil inEase = EasingUtil();
    static float anim = 0.f;
    constexpr float easeSpeed = 10.f;
    (mEnabled && mWasMiningBlock || mEnabled && mIsMiningBlock) ? inEase.incrementPercentage(delta * easeSpeed / 10)
        : inEase.decrementPercentage(delta * 2 * easeSpeed / 10);
    float inScale = inEase.easeOutExpo();
    if (inEase.isPercentageMax()) inScale = 0.996;
    inScale = MathUtils::clamp(inScale, 0.0f, 0.996);
    anim = MathUtils::lerp(0, 1, inEase.easeOutExpo());
    anim = MathUtils::lerp(anim, (mEnabled && mWasMiningBlock || mEnabled && mIsMiningBlock) ? 1.f : 0.f, delta * 10.f);


    if (anim < 0.001f) return;

    auto drawList = ImGui::GetBackgroundDrawList();

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImVec2 pos = ImVec2(ImGui::GetIO().DisplaySize.x / 2, ImGui::GetIO().DisplaySize.y / 2.5f);
    pos.y += pos.y / 1.14;
    ImVec2 boxSize = ImVec2(200 * anim, 47 * anim);
    // Center da progress bar
    pos.x -= boxSize.x / 2;
    pos.y -= boxSize.y / 2;

    ImVec2 progressPos = ImVec2(pos.x, pos.y);
    ImVec2 progressSize = ImVec2(boxSize.x * percentDone, boxSize.y);

    int daPerc = percentDone * 100;

    static std::string text = "Mining [0%]";

    static ImColor color = ImColor(255, 255, 0, 153);
    ImColor targetColor = ImColor(0, 255, 0, 153);

    bool isMining = mIsMiningBlock && mBreakingProgress > 0.001f; // i hate you.

    auto interfaceMod = gFeatureManager->mModuleManager->getModule<Interface>();
    bool isLowercase = interfaceMod->mNamingStyle.mValue == Lowercase || interfaceMod->mNamingStyle.mValue == LowercaseSpaced;

    if (mIsStealing && mEnabled) // Stealing
    {
        targetColor = ImColor(255, 0, 0, 153);
        if (isLowercase) text = "stealing " + std::to_string(daPerc) + "%";
        else text = "Stealing " + std::to_string(daPerc) + "%";
    }
    else if (mIsUncovering && mEnabled) // Uncovering
    {
        if (isLowercase) text = "uncovering " + std::to_string(daPerc) + "%";
        else text = "Uncovering " + std::to_string(daPerc) + "%";
        targetColor = ImColor(255, 255, 0, 153);
    }
    else if (isMining && 10 <= player->getAbsorption() && mEnabled) // Queueing
    {
        if (isLowercase) text = "queueing " + std::to_string(daPerc) + "%";
        else text = "Queueing " + std::to_string(daPerc) + "%";
        targetColor = ImColor(0, 255, 255, 153);
    }
    else // Mining
    {
        if (isLowercase) text = "mining " + std::to_string(daPerc) + "%";
        else text = "Mining " + std::to_string(daPerc) + "%";
        targetColor = ImColor(0, 255, 0, 153);
    }

    color = ImColor(MathUtils::lerp(color.Value, targetColor.Value, ImGui::GetIO().DeltaTime * 7.5f));

    float daPadding = -25.f * anim;

    float max = pos.x + boxSize.x;
    ImVec2 bgMin = ImVec2(pos.x + boxSize.x * percentDone, pos.y);
    ImVec2 bgMax = ImVec2(pos.x + boxSize.x, pos.y + (boxSize.y + daPadding));
    ImVec2 progMax = ImVec2(pos.x + (boxSize.x * percentDone + 6.f), pos.y + (boxSize.y + daPadding));
    progMax.x = std::clamp(progMax.x, pos.x, max);

    float rounding = 15.f * anim;

    if (percentDone > 0.001f)
    {
        drawList->AddShadowRect(ImVec2(pos.x, pos.y), progMax, color, 50.f, ImVec2(), ImDrawCornerFlags_All, rounding);
        drawList->PushClipRect(ImVec2(pos.x, pos.y), ImVec2(pos.x + (boxSize.x * percentDone), pos.y + (boxSize.y - 10.f)));
        //drawList->AddRectFilled(ImVec2(pos.x, pos.y), progMax, color, rounding);
        //ImColor leftSide = ImColor(color.Value.x * 0.40f, color.Value.y * 0.40f, color.Value.z * 0.40f, color.Value.w);
        ImColor leftSide = color;
        drawList->AddRectFilledMultiColor(ImVec2(pos.x, pos.y), progMax, leftSide, color, color, leftSide, rounding, ImDrawCornerFlags_All);
        drawList->PopClipRect();
    }

    if (percentDone > 0.001f) drawList->PushClipRect(bgMin, bgMax);
    drawList->AddRectFilled(ImVec2(pos.x + boxSize.x * percentDone - 6, pos.y), bgMax, ImColor(0.f, 0.f, 0.f, 0.6f), rounding);
    if (percentDone > 0.001f) drawList->PopClipRect();

    FontHelper::pushPrefFont(true, true, true);

    float fontSize = 20.f * anim;
    ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str());
    // center da text between
    ImVec2 textPos = ImVec2(pos.x + (boxSize.x - textSize.x) / 2, pos.y + (2.5f * anim));

    ImRenderUtils::drawShadowText(drawList, text, textPos, ImColor(255, 255, 255, 255), fontSize);
    FontHelper::popPrefFont();

}

void Regen::renderNewProgressBar()
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    static float lastProgress = 0.f;
    float percentDone = 1.f;

    percentDone = mBreakingProgress;
    percentDone /= mCurrentDestroySpeed;
    if (percentDone < lastProgress) lastProgress = percentDone;
    percentDone = MathUtils::lerp(lastProgress, percentDone, ImGui::GetIO().DeltaTime * 30.f);
    lastProgress = percentDone;
    // clamp percentDone
    percentDone = MathUtils::clamp(percentDone, 0.f, 1.f);

    float delta = ImGui::GetIO().DeltaTime;

    static EasingUtil inEase = EasingUtil();
    static float anim = 0.f;
    constexpr float easeSpeed = 10.f;
    (mEnabled && mWasMiningBlock || mEnabled && mIsMiningBlock) ? inEase.incrementPercentage(delta * easeSpeed / 10)
                                                                : inEase.decrementPercentage(delta * 2 * easeSpeed / 10);
    float inScale = inEase.easeOutExpo();
    if (inEase.isPercentageMax()) inScale = 0.996;
    inScale = MathUtils::clamp(inScale, 0.0f, 0.996);
    anim = MathUtils::lerp(0, 1, inEase.easeOutExpo());
    anim = MathUtils::lerp(anim, (mEnabled && mWasMiningBlock || mEnabled && mIsMiningBlock) ? 1.f : 0.f, delta * 10.f);


    if (anim < 0.001f) return;

    auto drawList = ImGui::GetBackgroundDrawList();

    ImVec2 displaySize = ImGui::GetIO().DisplaySize;
    ImVec2 pos = ImVec2(ImGui::GetIO().DisplaySize.x / 2, ImGui::GetIO().DisplaySize.y / 2.5f);
    pos.y += pos.y / 1.14;
    ImVec2 boxSize = ImVec2(216 * anim, 48 * anim);
    // Center da progress bar
    pos.x -= boxSize.x / 2;
    pos.y -= boxSize.y / 2;

    pos.y += mOffset.mValue;

    ImVec2 progressPos = ImVec2(pos.x, pos.y);
    ImVec2 progressSize = ImVec2(boxSize.x * percentDone, boxSize.y);

    int daPerc = percentDone * 100;

    static std::string text = "Mining 0%";

    static ImColor color = ImColor(255, 255, 0, 180);
    ImColor targetColor = ImColor(0, 255, 0, 153);

    bool isMining = mIsMiningBlock && mBreakingProgress > 0.001f; // i hate you.

    auto interfaceMod = gFeatureManager->mModuleManager->getModule<Interface>();
    bool isLowercase = interfaceMod->mNamingStyle.mValue == Lowercase || interfaceMod->mNamingStyle.mValue == LowercaseSpaced;

    if (mIsStealing && mEnabled) // Stealing
    {
        targetColor = ImColor(153, 50, 204, 169);
        if (isLowercase) text = "stealing " + std::to_string(daPerc) + "%";
        else text = "Stealing " + std::to_string(daPerc) + "%";
    }
    else if (mIsUncovering && mEnabled) // Uncovering
    {
        if (isLowercase) text = "uncovering " + std::to_string(daPerc) + "%";
        else text = "Uncovering " + std::to_string(daPerc) + "%";
        targetColor = ImColor(240, 128, 128, 169);
    }
    else if (isMining && 10 <= player->getAbsorption() && mEnabled) // Queueing
    {
        if (isLowercase) text = "queueing " + std::to_string(daPerc) + "%";
        else text = "Queueing " + std::to_string(daPerc) + "%";
        targetColor = ImColor(123, 104, 238, 169);
    }
    else // Mining
    {
        if (isLowercase) text = "mining " + std::to_string(daPerc) + "%";
        else text = "Mining " + std::to_string(daPerc) + "%";
        targetColor = ImColor(255, 215, 33, 169);
    }

    color = ImColor(MathUtils::lerp(color.Value, targetColor.Value, ImGui::GetIO().DeltaTime * 7.5f));

    float daPadding = -25.f * anim;

    float max = pos.x + boxSize.x;
    ImVec2 bgMin = ImVec2(pos.x + boxSize.x * percentDone, pos.y);
    ImVec2 bgMax = ImVec2(pos.x + boxSize.x, pos.y + (boxSize.y + daPadding));
    ImVec2 progMax = ImVec2(pos.x + (boxSize.x * percentDone + 6.f), pos.y + (boxSize.y + daPadding));
    progMax.x = std::clamp(progMax.x, pos.x, max);

    float rounding = 99;

    if (percentDone > 0.001f)
    {
        drawList->AddShadowRect(ImVec2(pos.x, pos.y), progMax, ColorUtils::getThemedColor(0), 50.f, ImVec2(), ImDrawCornerFlags_All, rounding);
        drawList->PushClipRect(ImVec2(pos.x, pos.y), ImVec2(pos.x + (boxSize.x * percentDone), pos.y + (boxSize.y - 10.f)));
        //drawList->AddRectFilled(ImVec2(pos.x, pos.y), progMax, color, rounding);
        //ImColor leftSide = ImColor(color.Value.x * 0.40f, color.Value.y * 0.40f, color.Value.z * 0.40f, color.Value.w);
        ImColor leftSide = color;
        drawList->AddRectFilledMultiColor(ImVec2(pos.x, pos.y), progMax, ColorUtils::getThemedColor(0), ColorUtils::getThemedColor(1), ColorUtils::getThemedColor(2), ColorUtils::getThemedColor(3), rounding, ImDrawCornerFlags_All);
        drawList->PopClipRect();
    }

    if (percentDone > 0.001f) drawList->PushClipRect(bgMin, bgMax);
    drawList->AddRectFilled(ImVec2(pos.x + boxSize.x * percentDone - 6, pos.y), bgMax, ImColor(0.f, 0.f, 0.f, 0.6f), rounding);
    if (percentDone > 0.001f) drawList->PopClipRect();

    FontHelper::pushPrefFont(true, true, true);

    float fontSize = 20.f * anim;
    ImVec2 textSize = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text.c_str());
    // center da text between
    ImVec2 textPos = ImVec2(pos.x + (boxSize.x - textSize.x) / 2, pos.y + 1.6);

    ImRenderUtils::drawShadowText(drawList, text, textPos, ImColor(255, 255, 255, 255), fontSize);
    FontHelper::popPrefFont();

}

void Regen::renderBlock()
{
    if (!mIsMiningBlock || !mEnabled) return;

    auto player = ClientInstance::get()->getLocalPlayer();

    static float lastProgress = 0.f;
    float progress = 1.f;

    progress = mBreakingProgress;
    progress /= mCurrentDestroySpeed;
    if (progress < lastProgress) lastProgress = progress;
    progress = MathUtils::lerp(lastProgress, progress, ImGui::GetIO().DeltaTime * 30.f);
    lastProgress = progress;

    // clamp the progress to 0-1
    progress = MathUtils::clamp(progress, 0.f, 1.f);

    if (progress < 0.01f) return;

    auto size = glm::vec3(progress, progress, progress);
    glm::vec3 blockPos = mCurrentBlockPos;
    blockPos.x += 0.5f - (progress / 2.f);
    blockPos.y += 0.5f - (progress / 2.f);
    blockPos.z += 0.5f - (progress / 2.f);
    auto blockAABB = AABB(blockPos, size);

    static ImColor color = ImColor(255, 255, 0, 255);
    ImColor targetColor = ImColor(255, 255, 0, 255);

    if (mIsStealing) targetColor = ImColor(255, 0, 0, 255);
    else if (mIsUncovering) targetColor = ImColor(255, 255, 0, 255);
    else if (mIsMiningBlock && 10 <= player->getAbsorption()) targetColor = ImColor(0, 255, 255, 255);
    else targetColor = ImColor(0, 255, 0, 255);

    // lerping the color
    color = ImColor(MathUtils::lerp(color.Value, targetColor.Value, ImGui::GetIO().DeltaTime * 10.f));

    RenderUtils::drawOutlinedAABB(blockAABB, true, ColorUtils::getThemedColor(0));
};

void Regen::renderFakeOres() {
    if (!mEnabled) return;

    if (!mFakePositions.empty()) {
        for (auto& pos : mFakePositions) {
            auto boxAABB = AABB(pos, glm::vec3(1.f, 1.f, 1.f));
            RenderUtils::drawOutlinedAABB(boxAABB, true);
        }
    }
}

void Regen::onPacketOutEvent(PacketOutEvent& event)
{
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    if (event.mPacket->getId() == PacketID::PlayerAuthInput)
    {
        auto paip = event.getPacket<PlayerAuthInputPacket>();
        if (mShouldRotate)
        {
            const glm::vec3 blockPos = mCurrentBlockPos;
            auto blockAABB = AABB(blockPos, glm::vec3(1, 1, 1));
            glm::vec2 rotations = MathUtils::getRots(*player->getPos(), blockAABB);
            paip->mRot = rotations;
            paip->mYHeadRot = rotations.y;
            mShouldRotate = false;
        }
        if (mShouldRotateToPlacePos) {
            const glm::vec3 blockPos = mCurrentPlacePos;
            auto blockAABB = AABB(blockPos, glm::vec3(1, 1, 1));
            glm::vec2 rotations = MathUtils::getRots(*player->getPos(), blockAABB);
            paip->mRot = rotations;
            paip->mYHeadRot = rotations.y;
            mShouldRotateToPlacePos = false;
        }
    }
    else if (event.mPacket->getId() == PacketID::InventoryTransaction)
    {
        if (const auto it = event.getPacket<InventoryTransactionPacket>(); it->mTransaction->type ==
            ComplexInventoryTransaction::Type::ItemUseTransaction)
        {
            const auto transac = reinterpret_cast<ItemUseInventoryTransaction*>(it->mTransaction.get());
            if (transac->mActionType == ItemUseInventoryTransaction::ActionType::Place)
            {
                transac->mClickPos = BlockUtils::clickPosOffsets[transac->mFace];
                for (int i = 0; i < 3; i++)
                {
                    if (transac->mClickPos[i] == 0.5)
                    {
                        transac->mClickPos[i] = MathUtils::randomFloat(-0.49f, 0.49f);
                    }
                }
            }
        }
    }
    else if (event.mPacket->getId() == PacketID::MobEquipment)
    {
        auto mp = event.getPacket<MobEquipmentPacket>();
        if (mp->mSlot == mToolSlot) mShouldSpoofSlot = false;
        else mShouldSpoofSlot = true;
    }
}

void Regen::onPacketInEvent(class PacketInEvent& event) {
    auto player = ClientInstance::get()->getLocalPlayer();
    if (!player) return;

    if (event.mPacket->getId() == PacketID::LevelEvent) {
        auto levelEvent = event.getPacket<LevelEventPacket>();
        if (levelEvent->mEventId == 3600) { // Start destroying block
            auto blockAtPos = ClientInstance::get()->getBlockSource()->getBlock(levelEvent->mPos);

            if (blockAtPos->mLegacy->getBlockId() == 10099) { // ignore crumbling blocks due to false detecting
                return;
            }
#ifdef __PRIVATE_BUILD__
            if (mReplace.mValue && mIsMiningBlock && !mIsUncovering && !mIsStealing && mCurrentBlockPos == glm::ivec3(levelEvent->mPos)) {
                mStartDestroyCount++;
                if (2 <= mStartDestroyCount) {
                    if(mDebug.mValue && mRaperNotify.mValue) ChatUtils::displayClientMessage("§eenemy targetting same ore!");
                }
            }
#endif
            if (BlockUtils::isMiningPosition(glm::ivec3(levelEvent->mPos)) || mConfuse.mValue && mLastConfusedPos == glm::ivec3(levelEvent->mPos) && mLastConfuse + 1000 > NOW) return;

#ifdef __DEBUG__
            if (mAvoidEnemyOre.mValue) {
                int blockId = blockAtPos->mLegacy->getBlockId();
                if (blockId == 73 || blockId == 74) {
                    mOreBlackList.push_back(glm::ivec3(levelEvent->mPos));
                }
                else {
                    std::vector<BlockInfo> blockList = BlockUtils::getBlockList(glm::ivec3(levelEvent->mPos), 2);
                    for (int i = 0; i < blockList.size(); i++) {
                        int blockId2 = blockList[i].mBlock->mLegacy->getBlockId();
                        if (blockId2 == 73 || blockId2 == 74) {
                            glm::ivec3 delta = blockList[i].mPosition - glm::ivec3(levelEvent->mPos);
                            int dist = abs(delta.x) + abs(delta.y) + abs(delta.z);
                            if (0 < dist && dist <= 2 && BlockUtils::getExposedFace(blockList[i].mPosition) == -1)
                                mOreBlackList.push_back(blockList[i].mPosition);
                        }
                    }
                }
            }
#endif

            // Steal
            int exposedFace = BlockUtils::getExposedFace(levelEvent->mPos);
            if (exposedFace != -1 || !(mAntiConfuse.mValue && mAntiConfuseMode.mValue == AntiConfuseMode::ExposedCheck)) {
                for (auto& offset : mOffsetList) {
                    glm::ivec3 blockPos = glm::ivec3(levelEvent->mPos) + offset;
                    if (isValidBlock(blockPos, true, false) && BlockUtils::getExposedFace(blockPos) == -1) {
                        mEnemyTargettingBlockPos = blockPos;
                        mLastEnemyLayerBlockPos = levelEvent->mPos;
                        mCanSteal = true;
                        mLastStealerUpdate = NOW;
                    }
                }
            }

#ifdef __PRIVATE_BUILD__
            // Anti Steal
            glm::ivec3 pos = glm::ivec3(levelEvent->mPos);
            if (pos == mTargettingBlockPos && pos != mCurrentBlockPos && mIsMiningBlock && mIsUncovering) {
                if (mAntiSteal.mValue || antiStealerEnabled) {
                    mBlackListedOrePos = pos;
                    if (mDebug.mValue && mStealNotify.mValue) ChatUtils::displayClientMessage("§eenemy tried to steal your ore, switching ores!");
                }
                mLastStealerDetected = NOW;
            }
#endif

            // Ore Blocker
            if (mBlockOre.mValue) {
                // normal ore blocker
                if (std::find(mFakePositions.begin(), mFakePositions.end(), glm::ivec3(levelEvent->mPos)) != mFakePositions.end()) return;
                if (isValidRedstone(levelEvent->mPos)) miningRedstones.push_back(levelEvent->mPos);
            }
        }
        else if (levelEvent->mEventId == 3601) { // Stop destroying block
#ifdef __DEBUG__
            if (mAvoidEnemyOre.mValue) {
                auto it1 = std::find(mOreBlackList.begin(), mOreBlackList.end(), glm::ivec3(levelEvent->mPos));
                if (it1 != mOreBlackList.end())
                    mOreBlackList.erase(it1);
                std::vector<BlockInfo> blockList = BlockUtils::getBlockList(glm::ivec3(levelEvent->mPos), 2);
                for (int i = 0; i < blockList.size(); i++) {
                    int blockId2 = blockList[i].mBlock->mLegacy->getBlockId();
                    if (blockId2 == 73 || blockId2 == 74) {
                        glm::ivec3 delta = blockList[i].mPosition - glm::ivec3(levelEvent->mPos);
                        int dist = abs(delta.x) + abs(delta.y) + abs(delta.z);
                        if (0 < dist && dist <= 2) {
                            auto it2 = std::find(mOreBlackList.begin(), mOreBlackList.end(), blockList[i].mPosition);
                            if (it2 != mOreBlackList.end())
                                mOreBlackList.erase(it2);
                        }
                    }
                }
            }
#endif
            if (mCanSteal && glm::ivec3(levelEvent->mPos) == mLastEnemyLayerBlockPos) {
                mCanSteal = false;
            }
#ifdef __PRIVATE_BUILD__
            if (glm::ivec3(levelEvent->mPos) == mBlackListedOrePos) {
                if (mAntiSteal.mValue || antiStealerEnabled) {
                    mBlackListedOrePos = { INT_MAX, INT_MAX, INT_MAX };
                }
            }
#endif
        }
    }
    else if (event.mPacket->getId() == PacketID::UpdateBlock) {
        auto pkt = event.getPacket<UpdateBlockPacket>();
        auto updateBlockPacket = pkt.get();
        if (10 < glm::distance(*player->getPos(), glm::vec3(updateBlockPacket->mPos)) || mCurrentBlockPos == updateBlockPacket->mPos) return;

        if (!BlockUtils::isAirBlock(updateBlockPacket->mPos)) {
            int brokenBlockId = ClientInstance::get()->getBlockSource()->getBlock(updateBlockPacket->mPos)->mLegacy->getBlockId();
            if (brokenBlockId == 73 || brokenBlockId == 74) {
                mLastBrokenOrePos.push_back(updateBlockPacket->mPos);
            }
            for (auto& offset : mOffsetList) {
                glm::ivec3 blockPos = updateBlockPacket->mPos + offset;
                int blockId = ClientInstance::get()->getBlockSource()->getBlock(blockPos)->mLegacy->getBlockId();
                if (blockId == 73 || blockId == 74) {
                    mLastBrokenCoveringBlockPos.push_back(updateBlockPacket->mPos);
                    break;
                }
            }
        }
    }
    else if (event.mPacket->getId() == PacketID::ChangeDimension) {
        mOreBlackList.clear();
    }
}

void Regen::onSendImmediateEvent(SendImmediateEvent& event)
{
    uint8_t packetId = event.send[0];
    if (packetId == 0)
    {
        uint64_t timestamp = *reinterpret_cast<uint64_t*>(&event.send[1]);
        uint64_t timestamp64 = _byteswap_uint64(timestamp);
        uint64_t now = NOW;
        mEventDelay = now - timestamp64;
    }
}

void Regen::onPingUpdateEvent(PingUpdateEvent& event)
{
    mPing = event.mPing - mEventDelay;
}