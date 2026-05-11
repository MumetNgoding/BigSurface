//
//  SurfaceThermalDriver.cpp
//  BigSurface
//
//  Created by HafidzRadhival on 10/05/26.
//  Copyright © 2026 Xia Shangning. All rights reserved.
//

#include "SurfaceThermalDriver.hpp"
#include "ThermalKeyImplementations.hpp"

#define LOG(str, ...)    IOLog("%s::" str "\n", "SurfaceThermalDriver", ##__VA_ARGS__)

#define super IOService
OSDefineMetaClassAndStructors(SurfaceThermalDriver, IOService)

static IOPMPowerState thermalDriverPowerStates[2] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMPowerOn, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
};

bool SurfaceThermalDriver::start(IOService* provider) {
    if (!super::start(provider)) return false;
    
    nub = OSDynamicCast(SurfaceThermalNub, provider);
    if (!nub) {
        LOG("Failed to cast provider to SurfaceThermalNub!");
        return false;
    }
    
    work_loop = getWorkLoop();
    if (!work_loop) return false;
    
    timer = IOTimerEventSource::timerEventSource(this, &SurfaceThermalDriver::timerCallback);
    if (!timer || work_loop->addEventSource(timer) != kIOReturnSuccess) {
        OSSafeReleaseNULL(timer);
        return false;
    }
    
    VirtualSMCAPI::addKey(SMC_MAKE_IDENTIFIER('F','0','A','c'), vsmcPlugin.data, VirtualSMCAPI::valueWithData(nullptr, 2, SmcKeyTypeFpe2, new F0Ac(0)));
    VirtualSMCAPI::addKey(SMC_MAKE_IDENTIFIER('F','0','T','g'), vsmcPlugin.data, VirtualSMCAPI::valueWithData(nullptr, 2, SmcKeyTypeFpe2, new F0Tg(0)));
    VirtualSMCAPI::addKey(SMC_MAKE_IDENTIFIER('F','0','M','n'), vsmcPlugin.data, VirtualSMCAPI::valueWithData(nullptr, 2, SmcKeyTypeFpe2, new F0Mn(0)));
    VirtualSMCAPI::addKey(SMC_MAKE_IDENTIFIER('F','0','M','x'), vsmcPlugin.data, VirtualSMCAPI::valueWithData(nullptr, 2, SmcKeyTypeFpe2, new F0Mx(0)));
    VirtualSMCAPI::addKey(SMC_MAKE_IDENTIFIER('F','N','u','m'), vsmcPlugin.data, VirtualSMCAPI::valueWithUint8(1, new NumF));
    VirtualSMCAPI::addKey(SMC_MAKE_IDENTIFIER('N','u','m','F'), vsmcPlugin.data, VirtualSMCAPI::valueWithUint8(1, new NumF));

    qsort(const_cast<VirtualSMCKeyValue *>(vsmcPlugin.data.data()), vsmcPlugin.data.size(), sizeof(VirtualSMCKeyValue), VirtualSMCKeyValue::compare);

    PMinit();
    nub->joinPMtree(this);
    registerPowerDriver(this, thermalDriverPowerStates, 2);
    changePowerStateToPriv(1);

    vsmcNotifier = VirtualSMCAPI::registerHandler(vsmcNotificationHandler, this);
    if (!vsmcNotifier)
        LOG("Failed to register VirtualSMC handler!");
    
    updateFanSpeed();
    timer->setTimeoutMS(1000);
    
    registerService();
    return true;
}

void SurfaceThermalDriver::stop(IOService* provider) {
    if (vsmcNotifier) {
        vsmcNotifier->remove();
        vsmcNotifier = nullptr;
    }
    if (timer) {
        timer->cancelTimeout();
        work_loop->removeEventSource(timer);
        OSSafeReleaseNULL(timer);
    }
    PMstop();
    super::stop(provider);
}

IOReturn SurfaceThermalDriver::setPowerState(unsigned long whichState, IOService *device) {
    return kIOPMAckImplied;
}

void SurfaceThermalDriver::timerCallback(OSObject* owner, IOTimerEventSource* sender) {
    auto self = OSDynamicCast(SurfaceThermalDriver, owner);
    if (self) {
        self->updateFanSpeed();
        sender->setTimeoutMS(5000);
    }
}

void SurfaceThermalDriver::updateFanSpeed() {
    UInt16 rpm = 0;
    if (nub && nub->getFanSpeed(&rpm) == kIOReturnSuccess) {
        ThermalManager::getShared()->updateFanSpeed(rpm);
    }
}

bool SurfaceThermalDriver::vsmcNotificationHandler(void *sensors, void *refCon, IOService *vsmc, IONotifier *notifier) {
    if (sensors && vsmc) {
        auto &plugin = static_cast<SurfaceThermalDriver *>(sensors)->vsmcPlugin;
        auto ret = vsmc->callPlatformFunction(VirtualSMCAPI::SubmitPlugin, true, sensors, &plugin, nullptr, nullptr);
        if (ret == kIOReturnSuccess) {
            IOLog("SurfaceThermalDriver::Plugin submitted\n");
            return true;
        } else {
            IOLog("SurfaceThermalDriver::Plugin submission failure %X\n", ret);
        }
    }
    return false;
}
