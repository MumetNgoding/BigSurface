//
//  ThermalManager.hpp
//  BigSurface
//
//  Created by HafidzRadhival on 10/05/26.
//  Copyright © 2026 Xia Shangning. All rights reserved.
//

#ifndef ThermalManager_hpp
#define ThermalManager_hpp

#include <IOKit/IOLib.h>
#include <IOKit/IOLocks.h>
#include <libkern/c++/OSObject.h>

struct ThermalState {
    UInt16 fanSpeedRPM;
    UInt16 targetSpeedRPM;
};

class ThermalManager : public OSObject {
    OSDeclareDefaultStructors(ThermalManager);
    
private:
    static ThermalManager* instance;

public:
    ThermalState state;
    IOSimpleLock* stateLock;

    static ThermalManager* getShared();
    
    // OSObject overrides
    virtual bool init() override;
    virtual void free() override;
    
    void updateFanSpeed(UInt16 rpm);
};

#endif /* ThermalManager_hpp */
