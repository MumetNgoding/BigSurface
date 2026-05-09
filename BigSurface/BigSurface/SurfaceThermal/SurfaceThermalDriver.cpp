//
//  SurfaceThermalDriver.cpp
//  BigSurface
//
//  Created by HafidzRadhival on 10/05/26.
//  Copyright © 2026 Xia Shangning. All rights reserved.
//

#include "SurfaceThermalDriver.hpp"
#include "ThermalKeyImplementations.hpp"

#define super IOService
OSDefineMetaClassAndStructors(SurfaceThermalDriver, IOService)

static IOPMPowerState thermalDriverPowerStates[2] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
    {1, kIOPMPowerOn, kIOPMPowerOn, kIOPMPowerOn, 0, 0, 0, 0, 0, 0, 0, 0}
};

bool SurfaceThermalDriver::start(IOService* provider) {
    IOLog("!!! SurfaceThermalDriver: Masuk ke fungsi start()\n");
    
    if (!super::start(provider)) return false;
    
    nub = OSDynamicCast(SurfaceThermalNub, provider);
    if (!nub) {
        IOLog("!!! SurfaceThermalDriver: Gagal casting ke Nub!\n");
        return false;
    }
    
    work_loop = getWorkLoop();
    if (!work_loop) return false;
    
    timer = IOTimerEventSource::timerEventSource(this, &SurfaceThermalDriver::timerCallback);
    if (!timer || work_loop->addEventSource(timer) != kIOReturnSuccess) {
        OSSafeReleaseNULL(timer);
        return false;
    }
    
    // 1. Daftarkan Kunci SMC
    IOLog("!!! SurfaceThermalDriver: Menyiapkan kunci SMC...\n");
    
    // Kita tambahin log buat tiap penambahan kunci
    #define ADD_KEY(name, value) \
        if (VirtualSMCAPI::addKey(name, vsmcPlugin.data, value)) { \
            IOLog("!!! SurfaceThermalDriver: Berhasil nambah kunci %c%c%c%c\n", (char)(name >> 24), (char)(name >> 16), (char)(name >> 8), (char)name); \
        } else { \
            IOLog("!!! SurfaceThermalDriver: GAGAL nambah kunci %c%c%c%c\n", (char)(name >> 24), (char)(name >> 16), (char)(name >> 8), (char)name); \
        }

    ADD_KEY(SMC_MAKE_IDENTIFIER('F','0','A','c'), VirtualSMCAPI::valueWithData(nullptr, 2, SmcKeyTypeFpe2, new F0Ac(0)));
    ADD_KEY(SMC_MAKE_IDENTIFIER('F','0','T','g'), VirtualSMCAPI::valueWithData(nullptr, 2, SmcKeyTypeFpe2, new F0Tg(0)));
    ADD_KEY(SMC_MAKE_IDENTIFIER('F','0','M','n'), VirtualSMCAPI::valueWithData(nullptr, 2, SmcKeyTypeFpe2, new F0Mn(0)));
    ADD_KEY(SMC_MAKE_IDENTIFIER('F','0','M','x'), VirtualSMCAPI::valueWithData(nullptr, 2, SmcKeyTypeFpe2, new F0Mx(0)));
    ADD_KEY(SMC_MAKE_IDENTIFIER('F','N','u','m'), VirtualSMCAPI::valueWithUint8(1, new NumF));
    ADD_KEY(SMC_MAKE_IDENTIFIER('N','u','m','F'), VirtualSMCAPI::valueWithUint8(1, new NumF));

    // 2. Sortir Kunci
    qsort(const_cast<VirtualSMCKeyValue *>(vsmcPlugin.data.data()), vsmcPlugin.data.size(), sizeof(VirtualSMCKeyValue), VirtualSMCKeyValue::compare);
    IOLog("!!! SurfaceThermalDriver: Kunci SMC disortir. Total: %zu\n", vsmcPlugin.data.size());

    // 3. Power Management
    PMinit();
    nub->joinPMtree(this);
    registerPowerDriver(this, thermalDriverPowerStates, 2);
    changePowerStateToPriv(1);

    // 4. Daftarin ke VirtualSMC
    IOLog("!!! SurfaceThermalDriver: Mencoba mendaftarkan handler VirtualSMC...\n");
    vsmcNotifier = VirtualSMCAPI::registerHandler(vsmcNotificationHandler, this);
    if (!vsmcNotifier) {
        IOLog("!!! SurfaceThermalDriver: GAGAL mendaftarkan handler!\n");
    } else {
        IOLog("!!! SurfaceThermalDriver: Handler VirtualSMC berhasil didaftarkan.\n");
    }
    
    // Langsung update sekali buat pancingan
    updateFanSpeed();
    timer->setTimeoutMS(1000);
    
    IOLog("!!! SurfaceThermalDriver: Start selesai! Memanggil registerService()...\n");
    registerService();
    
    return true;
}

void SurfaceThermalDriver::stop(IOService* provider) {
    IOLog("!!! SurfaceThermalDriver: Driver dihentikan (stop)\n");
    if (vsmcNotifier) {
        vsmcNotifier->remove();
        vsmcNotifier = nullptr;
    }
    PMstop();
    if (timer) {
        timer->cancelTimeout();
        work_loop->removeEventSource(timer);
        OSSafeReleaseNULL(timer);
    }
    super::stop(provider);
}

IOReturn SurfaceThermalDriver::setPowerState(unsigned long whichState, IOService *device) {
    return kIOPMAckImplied;
}

void SurfaceThermalDriver::timerCallback(OSObject* owner, IOTimerEventSource* sender) {
    auto self = OSDynamicCast(SurfaceThermalDriver, owner);
    if (self) {
        self->updateFanSpeed();
        sender->setTimeoutMS(5000); // Kita buat lebih santai jadi 5 detik
    }
}

void SurfaceThermalDriver::updateFanSpeed() {
    UInt16 rpm = 0;
    if (nub && nub->getFanSpeed(&rpm) == kIOReturnSuccess) {
        ThermalManager::getShared()->updateFanSpeed(rpm);
    }
}

bool SurfaceThermalDriver::vsmcNotificationHandler(void *sensors, void *refCon, IOService *vsmc, IONotifier *notifier) {
    IOLog("!!! SurfaceThermalDriver: Handler VirtualSMC DIPANGGIL! sensors=%p, refCon=%p\n", sensors, refCon);
    
    SurfaceThermalDriver *self = static_cast<SurfaceThermalDriver *>(refCon);
    if (!self) self = static_cast<SurfaceThermalDriver *>(sensors);
    
    if (self && vsmc) {
        auto &plugin = self->vsmcPlugin;
        auto ret = vsmc->callPlatformFunction(VirtualSMCAPI::SubmitPlugin, true, sensors, &plugin, nullptr, nullptr);
        if (ret == kIOReturnSuccess) {
            IOLog("!!! SurfaceThermalDriver: BERHASIL mengirim plugin!\n");
            return true;
        } else {
            IOLog("!!! SurfaceThermalDriver: GAGAL mengirim plugin! Error: 0x%08X\n", ret);
        }
    }
    return false;
}
