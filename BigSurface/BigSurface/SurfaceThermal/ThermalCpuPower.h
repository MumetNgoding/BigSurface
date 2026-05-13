//
//  ThermalCpuPower.h
//  BigSurface
//
//  Created by HafidzRadhival on 13/05/26.
//  Copyright © 2026 Xia Shangning. All rights reserved.
//

#ifndef ThermalCpuPower_h
#define ThermalCpuPower_h

#include <IOKit/IOService.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOCommandGate.h>
#include "../SurfaceSerialHub/SurfaceSerialHubDriver.hpp"
#include "../SurfaceSerialHubDevices/SurfaceThermalNub.hpp"

// XNU internal kernel scheduler metric.
// Lower value = CPU scheduler is under heavy pressure = high load.
extern "C" {
    extern uint32_t sched_mach_factor;
}

/**
 * ThermalCpuPower
 *
 * An IOKit driver implementing the Apple Silicon sustained-power philosophy
 * on Intel Ice Lake via HWP EPP (MSR 0x774) and RAPL (MSR 0x610), synchronized
 * with the Microsoft Surface SAM Embedded Controller.
 *
 * Inherits SurfaceSerialHubClient to receive asynchronous SAM EC notifications
 * (TC 0x03, CID 0x0B) for zero-latency thermal trip-point response.
 */
class ThermalCpuPower : public SurfaceSerialHubClient {
    OSDeclareDefaultStructors(ThermalCpuPower)

public:
    virtual bool start(IOService *provider) override;
    virtual void stop(IOService *provider) override;

    /**
     * SAM EC asynchronous event callback.
     * Invoked by SurfaceSerialHubDriver on its own workloop thread.
     * MUST NOT block. Dispatches to our workloop via IOCommandGate::runAction.
     */
    virtual void eventReceived(UInt8 tc, UInt8 tid, UInt8 iid, UInt8 cid,
                               UInt8 *data_buffer, UInt16 length) override;

private:
    // =========================================================================
    // Power State Machine
    // =========================================================================
    enum PowerMode {
        kPowerModeCasual,    // Ultra-Low Idle  (EPP=0xFF, PL1=12W,  SAM=0x02)
        kPowerModeHeavy,     // Sustained Burst  (EPP=0x80, PL1=PL2=20W, SAM=0x01)
        kPowerModeCooldown   // Debounce window before returning to Casual
    };

    // =========================================================================
    // MSR Rendezvous Context Structures
    // Passed as argument to mp_rendezvous_no_intrs — must be stack-safe.
    // =========================================================================
    struct HwpContext {
        uint8_t epp; // Energy Performance Preference (bits [31:24] of MSR 0x774)
    };

    struct RaplContext {
        uint64_t msr; // Full 64-bit value to write directly to MSR 0x610
    };

    // =========================================================================
    // IOKit Resources
    // =========================================================================
    IOWorkLoop             *workLoop       {nullptr};
    IOCommandGate          *commandGate    {nullptr};
    IOTimerEventSource     *cpuTimerSource {nullptr};
    SurfaceThermalNub      *thermalNub     {nullptr};
    SurfaceSerialHubDriver *ssh            {nullptr};

    // =========================================================================
    // State Variables
    // =========================================================================
    PowerMode currentMode            {kPowerModeCasual};
    uint32_t  cooldownCounter        {0};
    bool      isThermalThrottled     {false};
    uint32_t  thermalRecoveryCounter {0}; // Consecutive cycles below recovery threshold

    // =========================================================================
    // Load / Timer Thresholds
    // =========================================================================
    // sched_mach_factor is an inverse load metric: lower = heavier scheduler load.
    static const uint32_t kLoadThreshold   = 400;  // Trigger Heavy below this value
    static const uint32_t kTimerIntervalMS = 500;  // Evaluation tick: 500 ms (fast response)
    static const uint32_t kCooldownLimit   = 20;   // 20 ticks × 0.5s = 10s cooldown

    // =========================================================================
    // Thermal Guard Thresholds (Hysteresis)
    //
    //  CLAMP:   temp >= 85°C → immediately force Casual, reset recovery counter
    //  RECOVER: temp <  75°C for 3 consecutive ticks → release clamp
    //           (recovery counter resets if temp bounces back ≥ 75°C mid-recovery)
    // =========================================================================
    static const uint32_t kTempThresholdHigh     = 85; // Emergency clamp ceiling
    static const uint32_t kTempThresholdRecovery = 75; // Recovery target
    static const uint32_t kThermalRecoveryCycles = 12; // 12 ticks × 0.5s = 6s stability required

    // =========================================================================
    // HWP MSR 0x774 (IA32_HWP_REQUEST) Constants
    //
    // Apple Silicon Philosophy mapping:
    //   Casual → EPP=0xFF: CPU forced to maximum power-efficiency bias.
    //                      The hardware clock will drop to ~800MHz at rest,
    //                      mimicking an Apple E-Core's idle behaviour.
    //   Heavy  → EPP=0x80: Balanced — allows full turbo headroom but avoids
    //                      the aggressive boost-then-crash of EPP=0x00.
    // =========================================================================
    static const uint8_t  kEppCasual     = 0xD0; // Power Efficiency (prevents Ring Bus starving)
    static const uint8_t  kEppHeavy      = 0x80; // Balanced (Performance/Efficiency)
    static const uint32_t kMsrHwpRequest = 0x774;

    // =========================================================================
    // SAM EC Protocol Constants
    // =========================================================================
    static const uint8_t kSamTcThermal    = 0x03; // Target Category: Thermal
    static const uint8_t kSamTcFan        = 0x05; // Target Category: Fan
    static const uint8_t kSamCidSetPerf   = 0x03; // CID: Set Performance Mode
    static const uint8_t kSamCidTripPoint = 0x0B; // CID: Thermal Trip-Point Notify
    static const uint8_t kSamCidFanKick   = 0x02; // CID: Activate Fan Profile
    static const uint8_t kSamPerfCasual   = 0x02; // SAM profile: Battery Saver
    static const uint8_t kSamPerfHeavy    = 0x01; // SAM profile: Recommended

    // =========================================================================
    // RAPL Power Limit Constants (MSR_PKG_POWER_LIMIT 0x610)
    //
    //  Apple Silicon "Sustained Power Ceiling" mapping (Adjusted for iGPU):
    //    Casual → PL1=12W, PL2=18W: Cool idle, but allows iGPU burst for fluid UI.
    //    Heavy  → PL1=20W, PL2=20W: Symmetrical ceiling — no PL2 spike, no
    //                               thermal overshoot. Sustained predictable perf.
    //
    //  MSR 0x610 Field Map:
    //    [14: 0] PL1 raw value   [15] PL1 Enable  [16] PL1 Clamp  [23:17] PL1 TAU
    //    [46:32] PL2 raw value   [47] PL2 Enable  [48] PL2 Clamp  [55:49] PL2 TAU
    //    [63]    Lock bit — if 1, hardware ignores writes silently
    //
    //  MSR 0x606 RAPL Power Unit: bits[3:0] → unit = 2^(-bits) W/LSB
    //    Ice Lake typical: 3 → 0.125W per unit
    // =========================================================================
    static const uint32_t kMsrPkgPowerLimit   = 0x610;
    static const uint32_t kMsrRaplPowerUnit   = 0x606;
    static const uint64_t kRaplLockBit        = (1ULL << 63);
    static const uint64_t kRaplPL1EnableBit   = (1ULL << 15);
    static const uint64_t kRaplPL1ClampBit    = (1ULL << 16);
    static const uint64_t kRaplPL2EnableBit   = (1ULL << 47);
    static const uint64_t kRaplPL2ClampBit    = (1ULL << 48);

    // Watt targets (Apple Silicon philosophy, relaxed for Intel iGPU)
    static const uint32_t kRaplCasualPL1Watts = 12; // Cool idle envelope
    static const uint32_t kRaplCasualPL2Watts = 18; // Burst headroom for iGPU/UI
    static const uint32_t kRaplHeavyWatts     = 20; // Flat symmetrical ceiling

    // =========================================================================
    // Private Methods
    // =========================================================================

    // Timer & state machine
    static void timerCallback(OSObject *owner, IOTimerEventSource *sender);
    void        evaluateSystemLoad();
    void        setPowerProfile(PowerMode mode);
    uint32_t    readCpuTemperature();

    // RAPL management (lock-bit aware, all-core synchronized)
    void        setRaplLimits(uint32_t pl1Watts, uint32_t pl2Watts);
    static void writeRaplMsr(void *arg);

    // Fan subsystem
    void        kickstartFan();

    // SAM dispatch
    void        dispatchSamPerfCommand(uint8_t profileByte);
    void        dispatchFanCommand(uint8_t cid, uint8_t payload);

    // HWP MSR write — executed on all logical CPUs simultaneously
    static void writeHwpMsr(void *arg);
};

#endif /* ThermalCpuPower_h */
