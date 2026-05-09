//
//  SurfaceThermalNub.cpp
//  BigSurface
//
//  Created by HafidzRadhival on 10/05/26.
//  Copyright © 2026 Xia Shangning. All rights reserved.
//

#include "SurfaceThermalNub.hpp"
#include "../SurfaceSerialHub/SerialProtocol.h"
#include "../SurfaceSerialHub/SurfaceSerialHubDriver.hpp"

#define super SurfaceSerialHubClient
OSDefineMetaClassAndStructors(SurfaceThermalNub, SurfaceSerialHubClient);

static IOPMPowerState thermalPowerStates[2] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMPowerOn, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
};

bool SurfaceThermalNub::attach(IOService* provider) {
    if (!super::attach(provider)) return false;
    ssh = OSDynamicCast(SurfaceSerialHubDriver, provider);
    return (ssh != nullptr);
}

void SurfaceThermalNub::detach(IOService* provider) {
    ssh = nullptr;
    super::detach(provider);
}

bool SurfaceThermalNub::start(IOService *provider) {
    if (!super::start(provider)) return false;
    
    PMinit();
    ssh->joinPMtree(this);
    registerPowerDriver(this, thermalPowerStates, 2);
    changePowerStateToPriv(1);
    
    registerService();
    
    return true;
}

IOReturn SurfaceThermalNub::getFanSpeed(UInt16 *rpm) {
    if (!rpm || !ssh) return kIOReturnBadArgument;
    
    UInt8 response[4] = {0};
    // Minta 4 byte dari SAM (TC=0x05, IID=0x01, CID=0x01)
    IOReturn status = ssh->getResponse(SSH_TC_FAN, SSH_TID_PRIMARY, 0x01, 0x01, nullptr, 0, true, response, 4);
    
    if (status == kIOReturnSuccess) {
        // Biasanya RPM itu 16-bit Little Endian di byte 0 dan 1
        *rpm = (UInt16)response[0] | ((UInt16)response[1] << 8);
    } else {
        IOLog("!!! SurfaceThermalNub: SAM Error 0x%08X (IID=0x01)\n", status);
    }
    
    return status;
}

void SurfaceThermalNub::eventReceived(UInt8 tc, UInt8 tid, UInt8 iid, UInt8 cid, UInt8 *data_buffer, UInt16 length) {
    // Not implemented
}
