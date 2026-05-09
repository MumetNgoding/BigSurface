//
//  ThermalKeyImplementations.hpp
//  BigSurface
//
//  Created by HafidzRadhival on 10/05/26.
//  Copyright © 2026 Xia Shangning. All rights reserved.
//

#ifndef ThermalKeyImplementations_hpp
#define ThermalKeyImplementations_hpp

#include <VirtualSMCSDK/kern_vsmcapi.hpp>
#include "ThermalManager.hpp"

class FanKey : public VirtualSMCValue {
protected:
    size_t index;
public:
    FanKey(size_t index) : index(index) {}
};

class F0Ac : public FanKey { using FanKey::FanKey; SMC_RESULT readAccess() override; };
class F0Tg : public FanKey { using FanKey::FanKey; SMC_RESULT readAccess() override; };
class F0Mn : public FanKey { using FanKey::FanKey; SMC_RESULT readAccess() override; };
class F0Mx : public FanKey { using FanKey::FanKey; SMC_RESULT readAccess() override; };
class NumF : public VirtualSMCValue { SMC_RESULT readAccess() override; };

#endif /* ThermalKeyImplementations_hpp */
