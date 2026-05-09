//
//  SurfaceThermalNub.hpp
//  BigSurface
//
//  Created by HafidzRadhival on 10/05/26.
//  Copyright © 2026 Xia Shangning. All rights reserved.
//

#ifndef SurfaceThermalNub_hpp
#define SurfaceThermalNub_hpp

#include "../SurfaceSerialHub/SurfaceSerialHubDriver.hpp"

class EXPORT SurfaceThermalNub : public SurfaceSerialHubClient {
    OSDeclareDefaultStructors(SurfaceThermalNub)
    
private:
    SurfaceSerialHubDriver* ssh {nullptr};

public:
    virtual bool attach(IOService* provider) override;
    virtual void detach(IOService* provider) override;
    virtual bool start(IOService* provider) override;
    
    // Function to get fan speed (RPM) from SAM
    virtual IOReturn getFanSpeed(UInt16 *rpm);
    
    // Abstract method implementation
    virtual void eventReceived(UInt8 tc, UInt8 tid, UInt8 iid, UInt8 cid, UInt8 *data_buffer, UInt16 length) override;
};

#endif /* SurfaceThermalNub_hpp */
