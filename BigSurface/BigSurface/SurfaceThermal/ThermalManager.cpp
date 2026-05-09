//
//  ThermalManager.cpp
//  BigSurface
//
//  Created by HafidzRadhival on 10/05/26.
//  Copyright © 2026 Xia Shangning. All rights reserved.
//

#include "ThermalManager.hpp"

ThermalManager* ThermalManager::instance = nullptr;

OSDefineMetaClassAndStructors(ThermalManager, OSObject)

ThermalManager* ThermalManager::getShared() {
    if (!instance) {
        instance = new ThermalManager();
        if (instance && !instance->init()) {
            instance->release();
            instance = nullptr;
        }
    }
    return instance;
}

bool ThermalManager::init() {
    if (!OSObject::init()) return false;
    
    state.fanSpeedRPM = 0;
    state.targetSpeedRPM = 0;
    stateLock = IOSimpleLockAlloc();
    
    return (stateLock != nullptr);
}

void ThermalManager::free() {
    if (stateLock) {
        IOSimpleLockFree(stateLock);
        stateLock = nullptr;
    }
    OSObject::free();
}

void ThermalManager::updateFanSpeed(UInt16 rpm) {
    if (stateLock) {
        IOSimpleLockLock(stateLock);
        state.fanSpeedRPM = rpm;
        IOSimpleLockUnlock(stateLock);
    }
}
