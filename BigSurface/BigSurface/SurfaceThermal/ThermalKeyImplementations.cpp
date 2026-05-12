//
//  ThermalKeyImplementations.cpp
//  BigSurface
//
//  Created by HafidzRadhival on 10/05/26.
//  Copyright © 2026 Xia Shangning. All rights reserved.
//

#include "ThermalKeyImplementations.hpp"

// Apple FPE2 encoding (RPM * 4)
#define RPM_TO_FPE2(rpm) ((rpm) << 2)

// Configurable values
#define MAX_REAL_RPM 7200
#define APPLE_THERMAL_SCALE 100
#define MIN_VALID_RPM 500

SMC_RESULT F0Ac::readAccess() {
    UInt16 *ptr = reinterpret_cast<UInt16 *>(data);
    auto manager = ThermalManager::getShared();

    if (manager->stateLock) {
        IOSimpleLockLock(manager->stateLock);

        UInt16 realRPM = manager->state.fanSpeedRPM;

        IOSimpleLockUnlock(manager->stateLock);

        // Safety clamp
        if (realRPM > MAX_REAL_RPM)
            realRPM = MAX_REAL_RPM;

        // Ignore bogus low RPM noise
        if (realRPM < MIN_VALID_RPM)
            realRPM = 0;

        // Scale for Apple thermal compatibility
        UInt16 scaledRPM = (realRPM * APPLE_THERMAL_SCALE) / 100;

        *ptr = OSSwapHostToBigInt16(RPM_TO_FPE2(scaledRPM));
    }

    return SmcSuccess;
}

SMC_RESULT F0Tg::readAccess() {
    UInt16 *ptr = reinterpret_cast<UInt16 *>(data);
    auto manager = ThermalManager::getShared();

    if (manager->stateLock) {
        IOSimpleLockLock(manager->stateLock);

        UInt16 realRPM = manager->state.fanSpeedRPM;

        IOSimpleLockUnlock(manager->stateLock);

        // Safety clamp
        if (realRPM > MAX_REAL_RPM)
            realRPM = MAX_REAL_RPM;

        // Ignore bogus low RPM noise
        if (realRPM < MIN_VALID_RPM)
            realRPM = 0;

        // Full real RPM for monitoring apps
        *ptr = OSSwapHostToBigInt16(RPM_TO_FPE2(realRPM));
    }

    return SmcSuccess;
}

SMC_RESULT F0Mn::readAccess() {
    UInt16 *ptr = reinterpret_cast<UInt16 *>(data);

    // Minimum fan speed
    *ptr = OSSwapHostToBigInt16(RPM_TO_FPE2(0));

    return SmcSuccess;
}

SMC_RESULT F0Mx::readAccess() {
    UInt16 *ptr = reinterpret_cast<UInt16 *>(data);

    // Scaled max for Apple thermal expectations
    UInt16 scaledMaxRPM = (MAX_REAL_RPM * APPLE_THERMAL_SCALE) / 100;

    *ptr = OSSwapHostToBigInt16(RPM_TO_FPE2(scaledMaxRPM));

    return SmcSuccess;
}

SMC_RESULT NumF::readAccess() {
    // Single fan device
    data[0] = 1;

    return SmcSuccess;
}
