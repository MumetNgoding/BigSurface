//
//  ThermalKeyImplementations.cpp
//  BigSurface
//
//  Created by HafidzRadhival on 10/05/26.
//  Copyright © 2026 Xia Shangning. All rights reserved.
//

#include "ThermalKeyImplementations.hpp"

SMC_RESULT F0Ac::readAccess() {
    UInt16 *ptr = reinterpret_cast<UInt16 *>(data);
    auto manager = ThermalManager::getShared();
    
    if (manager->stateLock) {
        IOSimpleLockLock(manager->stateLock);
        UInt16 rpm = manager->state.fanSpeedRPM;
        IOSimpleLockUnlock(manager->stateLock);
        
        // Balikin ke format fpe2 (kali 4) biar persentase di Stats bener
        *ptr = OSSwapHostToBigInt16(rpm << 2);
    }
    
    return SmcSuccess;
}

SMC_RESULT F0Tg::readAccess() {
    UInt16 *ptr = reinterpret_cast<UInt16 *>(data);
    auto manager = ThermalManager::getShared();
    
    if (manager->stateLock) {
        IOSimpleLockLock(manager->stateLock);
        UInt16 rpm = manager->state.fanSpeedRPM;
        IOSimpleLockUnlock(manager->stateLock);
        *ptr = OSSwapHostToBigInt16(rpm << 2);
    }
    return SmcSuccess;
}

SMC_RESULT F0Mn::readAccess() {
    UInt16 *ptr = reinterpret_cast<UInt16 *>(data);
    // Min 0 RPM
    *ptr = OSSwapHostToBigInt16(0 << 2);
    return SmcSuccess;
}

SMC_RESULT F0Mx::readAccess() {
    UInt16 *ptr = reinterpret_cast<UInt16 *>(data);
    // Max 6500 RPM
    *ptr = OSSwapHostToBigInt16(6500 << 2);
    return SmcSuccess;
}

SMC_RESULT NumF::readAccess() {
    data[0] = 1;
    return SmcSuccess;
}
