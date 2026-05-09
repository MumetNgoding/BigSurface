//
//  SurfaceThermalDriver.hpp
//  BigSurface
//
//  Created by HafidzRadhival on 10/05/26.
//  Copyright © 2026 Xia Shangning. All rights reserved.
//

#ifndef SurfaceThermalDriver_hpp
#define SurfaceThermalDriver_hpp

#include <IOKit/IOService.h>
#include <IOKit/IOTimerEventSource.h>
#include <VirtualSMCSDK/kern_vsmcapi.hpp>
#include "../SurfaceSerialHubDevices/SurfaceThermalNub.hpp"

#ifndef MODULE_VERSION
#define MODULE_VERSION 1.1.0
#endif

class EXPORT SurfaceThermalDriver : public IOService {
    OSDeclareDefaultStructors(SurfaceThermalDriver)
    
private:
    SurfaceThermalNub*      nub {nullptr};
    IOWorkLoop*             work_loop {nullptr};
    IOTimerEventSource*     timer {nullptr};
    IONotifier*             vsmcNotifier {nullptr};
    
    // VirtualSMC integration - Nama harus pas sama IOMatchCategory
    VirtualSMCAPI::Plugin vsmcPlugin {
        "SurfaceThermalDriver",
        parseModuleVersion(xStringify(MODULE_VERSION)),
        VirtualSMCAPI::Version,
    };
    
    void updateFanSpeed();
    static void timerCallback(OSObject* owner, IOTimerEventSource* sender);

public:
    virtual bool start(IOService* provider) override;
    virtual void stop(IOService* provider) override;
    virtual IOReturn setPowerState(unsigned long whichState, IOService *device) override;
    
    static bool vsmcNotificationHandler(void *sensors, void *refCon, IOService *vsmc, IONotifier *notifier);
};

#endif /* SurfaceThermalDriver_hpp */
