//
//  ThermalCpuPower.cpp
//  BigSurface
//
//  Created by HafidzRadhival on 13/05/26.
//  Copyright © 2026 Xia Shangning. All rights reserved.
//

#include "ThermalCpuPower.h"
#include <i386/proc_reg.h>

// Dual-sink logging: IOLog → system log, printf → dmesg/kprintf buffer
#define LOG(str, ...) { IOLog("ThermalCpuPower::" str "\n", ##__VA_ARGS__); \
                        printf("ThermalCpuPower::" str "\n", ##__VA_ARGS__); }

#define super SurfaceSerialHubClient

// XNU cross-processor rendezvous.
// action_func is called on ALL logical CPU cores with interrupts disabled.
// Essential for MSR writes that must be coherent across the entire die.
extern "C" void mp_rendezvous_no_intrs(void (*setup_func)(void *),
                                        void (*action_func)(void *),
                                        void (*teardown_func)(void *),
                                        void *arg);

OSDefineMetaClassAndStructors(ThermalCpuPower, SurfaceSerialHubClient)

// =============================================================================
// MARK: - Lifecycle
// =============================================================================

bool ThermalCpuPower::start(IOService *provider) {
    if (!super::start(provider)) return false;

    // --- Cast provider to SurfaceThermalNub ---
    thermalNub = OSDynamicCast(SurfaceThermalNub, provider);
    if (!thermalNub) {
        LOG("FATAL: Provider is not SurfaceThermalNub. Aborting start.");
        return false;
    }

    // --- Acquire SurfaceSerialHubDriver for SAM commands & event registration ---
    ssh = OSDynamicCast(SurfaceSerialHubDriver, thermalNub->getProvider());
    if (!ssh) {
        LOG("FATAL: Cannot acquire SurfaceSerialHubDriver. SAM interface unavailable.");
        return false;
    }

    // --- WorkLoop ---
    workLoop = getWorkLoop();
    if (!workLoop) {
        LOG("FATAL: Failed to acquire WorkLoop.");
        return false;
    }
    workLoop->retain();

    // --- CommandGate (serialises eventReceived dispatches with timer ticks) ---
    commandGate = IOCommandGate::commandGate(this);
    if (!commandGate) {
        LOG("FATAL: Failed to allocate IOCommandGate.");
        workLoop->release();
        workLoop = nullptr;
        return false;
    }
    if (workLoop->addEventSource(commandGate) != kIOReturnSuccess) {
        LOG("FATAL: Failed to add CommandGate to WorkLoop.");
        OSSafeReleaseNULL(commandGate);
        workLoop->release();
        workLoop = nullptr;
        return false;
    }

    // --- Timer Event Source (2-second evaluation loop) ---
    cpuTimerSource = IOTimerEventSource::timerEventSource(this,
                        &ThermalCpuPower::timerCallback);
    if (!cpuTimerSource) {
        LOG("FATAL: Failed to allocate IOTimerEventSource.");
        workLoop->removeEventSource(commandGate);
        OSSafeReleaseNULL(commandGate);
        workLoop->release();
        workLoop = nullptr;
        return false;
    }
    if (workLoop->addEventSource(cpuTimerSource) != kIOReturnSuccess) {
        LOG("FATAL: Failed to add TimerEventSource to WorkLoop.");
        OSSafeReleaseNULL(cpuTimerSource);
        workLoop->removeEventSource(commandGate);
        OSSafeReleaseNULL(commandGate);
        workLoop->release();
        workLoop = nullptr;
        return false;
    }

    // --- Register for Async SAM Thermal Events (TC=0x03, IID=0x01) ---
    // Receives CID=0x0B (Thermal Trip-Point Notify) for zero-latency response.
    IOReturn evtRet = ssh->registerEvent(this, kSamTcThermal, 0x01);
    if (evtRet != kIOReturnSuccess) {
        LOG("Warning: SAM thermal event registration failed (0x%X). "
            "Timer-only polling mode active.", evtRet);
    } else {
        LOG("SAM thermal event listener registered (TC=0x03, IID=0x01).");
    }

    // --- Apply Initial Casual Profile (Apple Ultra-Low Idle) ---
    setPowerProfile(kPowerModeCasual);
    cpuTimerSource->setTimeoutMS(kTimerIntervalMS);

    LOG("Driver started — Apple Silicon Power Philosophy active "
        "[Idle: EPP=0xFF, RAPL=12W | Load: EPP=0x80, RAPL=20W/20W]");
    registerService();
    return true;
}

void ThermalCpuPower::stop(IOService *provider) {
    if (ssh) {
        ssh->unregisterEvent(this, kSamTcThermal, 0x01);
        ssh = nullptr;
    }

    if (cpuTimerSource) {
        cpuTimerSource->cancelTimeout();
        if (workLoop) workLoop->removeEventSource(cpuTimerSource);
        OSSafeReleaseNULL(cpuTimerSource);
    }

    if (commandGate) {
        if (workLoop) workLoop->removeEventSource(commandGate);
        OSSafeReleaseNULL(commandGate);
    }

    OSSafeReleaseNULL(workLoop);
    thermalNub = nullptr;

    LOG("Driver stopped.");
    super::stop(provider);
}

// =============================================================================
// MARK: - SAM EC Async Event Handler
// Called on the SSH driver's workloop — MUST NOT block.
// =============================================================================

void ThermalCpuPower::eventReceived(UInt8 tc, UInt8 tid, UInt8 iid, UInt8 cid,
                                     UInt8 *data_buffer, UInt16 length) {
    // Filter: only act on TC=0x03 (Thermal), CID=0x0B (Trip-Point Notify)
    if (tc != kSamTcThermal || cid != kSamCidTripPoint) return;

    LOG("SAM Trip-Point event received (TC=0x%02X, CID=0x%02X). "
        "Dispatching immediate thermal evaluation.", tc, cid);

    // runAction safely dispatches evaluateSystemLoad to *our* workloop,
    // serialising it against the 2-second timer tick — zero blocking overhead.
    if (commandGate) {
        commandGate->runAction(
            OSMemberFunctionCast(IOCommandGate::Action, this,
                                 &ThermalCpuPower::evaluateSystemLoad),
            nullptr, nullptr, nullptr, nullptr);
    }
}

// =============================================================================
// MARK: - 2-Second Timer Callback
// =============================================================================

void ThermalCpuPower::timerCallback(OSObject *owner, IOTimerEventSource *sender) {
    auto *self = OSDynamicCast(ThermalCpuPower, owner);
    if (!self) return;
    self->evaluateSystemLoad();
    sender->setTimeoutMS(kTimerIntervalMS);
}

// =============================================================================
// MARK: - Core Evaluation Loop (runs on our workloop — thread-safe)
// =============================================================================

void ThermalCpuPower::evaluateSystemLoad() {
    const uint32_t load = sched_mach_factor;
    const uint32_t temp = readCpuTemperature();

    // --- Publish live telemetry to IORegistry ---
    // Readable via: ioreg -lp IOService -n ThermalCpuPower | grep Current
    setProperty("CurrentLoadFactor",  (unsigned long long)load, 64);
    setProperty("CurrentMode",        (unsigned long long)currentMode, 32);
    setProperty("CurrentTemperature", (unsigned long long)temp, 64);
    setProperty("ThermalThrottled",   isThermalThrottled);
    setProperty("RecoveryCounter",    (unsigned long long)thermalRecoveryCounter, 32);

    // =========================================================================
    // THERMAL GUARD — Hysteresis-Based Safety Clamp
    //
    //  CLAMP entry:   temp >= 85°C → force Casual instantly, reset counter
    //  CLAMP release: temp < 75°C for 3 consecutive ticks (6 seconds stable)
    //                 If temp bounces back ≥ 75°C mid-recovery, reset counter.
    // =========================================================================
    if (temp >= kTempThresholdHigh && !isThermalThrottled) {
        LOG("THERMAL GUARD: %u°C breach! Emergency clamp to Ultra-Low Idle.", temp);
        isThermalThrottled     = true;
        thermalRecoveryCounter = 0;
        setPowerProfile(kPowerModeCasual);
    }

    if (isThermalThrottled) {
        if (temp < kTempThresholdRecovery) {
            thermalRecoveryCounter++;
            LOG("THERMAL GUARD: Recovery %u/%u (Temp: %u°C)",
                thermalRecoveryCounter, kThermalRecoveryCycles, temp);

            if (thermalRecoveryCounter >= kThermalRecoveryCycles) {
                LOG("THERMAL GUARD: Stable below %u°C for %u cycles. Clamp released.",
                    kTempThresholdRecovery, kThermalRecoveryCycles);
                isThermalThrottled     = false;
                thermalRecoveryCounter = 0;
            }
        } else {
            if (thermalRecoveryCounter > 0) {
                LOG("THERMAL GUARD: Temp rose to %u°C during recovery. Counter reset.", temp);
                thermalRecoveryCounter = 0;
            }
        }
    }

    LOG("Tick — Load: %u, Temp: %u°C, Mode: %d%s",
        load, temp, static_cast<int>(currentMode),
        isThermalThrottled ? " [THROTTLED]" : "");

    // Skip load-based transitions while thermal clamp is active
    if (isThermalThrottled) return;

    // =========================================================================
    // LOAD-BASED STATE MACHINE
    // =========================================================================
    switch (currentMode) {

        case kPowerModeCasual:
            // sched_mach_factor below threshold = scheduler under pressure = heavy load
            if (load < kLoadThreshold) {
                LOG("Heavy load detected (%u). Switching to Sustained 20W Ceiling.", load);
                setPowerProfile(kPowerModeHeavy);
            }
            break;

        case kPowerModeHeavy:
            if (load >= kLoadThreshold) {
                // Load relieved — begin 10-second cooldown before stepping down
                currentMode     = kPowerModeCooldown;
                cooldownCounter = 0;
                LOG("Load relieved (%u). Entering 10-second cooldown debounce.", load);
            }
            break;

        case kPowerModeCooldown:
            if (load < kLoadThreshold) {
                // Load spiked again within cooldown window — abort and stay Heavy
                currentMode     = kPowerModeHeavy;
                cooldownCounter = 0;
                LOG("Load respiked during cooldown (%u). Aborting cooldown.", load);
            } else {
                cooldownCounter++;
                if (cooldownCounter >= kCooldownLimit) {
                    LOG("Cooldown complete (%u ticks). Returning to Ultra-Low Idle.",
                        cooldownCounter);
                    setPowerProfile(kPowerModeCasual);
                }
            }
            break;
    }
}

// =============================================================================
// MARK: - Power Profile Transition
// =============================================================================

void ThermalCpuPower::setPowerProfile(PowerMode mode) {
    currentMode = mode;

    if (mode == kPowerModeCasual) {
        // ------------------------------------------------------------------
        // Apple Silicon Ultra-Low Idle (Adjusted for iGPU):
        //   EPP = 0xD0 (hardware will favour efficiency but keep Ring Bus active)
        //   RAPL PL1=12W, PL2=18W (cool idle, but allows burst for fluid UI)
        //   SAM  = Battery Saver (EC lowers fan curve + voltage rails)
        // ------------------------------------------------------------------
        HwpContext hwp = { kEppCasual };
        mp_rendezvous_no_intrs(nullptr, writeHwpMsr, nullptr, &hwp);
        setRaplLimits(kRaplCasualPL1Watts, kRaplCasualPL2Watts);
        dispatchSamPerfCommand(kSamPerfCasual);
        LOG("→ Casual [Cool Idle]: EPP=0xD0, RAPL=%uW/%uW, SAM=0x02",
            kRaplCasualPL1Watts, kRaplCasualPL2Watts);

    } else {
        // ------------------------------------------------------------------
        // Apple Silicon Sustained Power Ceiling:
        //   EPP = 0x80 (balanced — allows turbo but doesn't chase max clocks)
        //   RAPL PL1 = PL2 = 20W (symmetrical flat ceiling, NO PL2 spike)
        //   SAM  = Recommended (EC activates full fan curve)
        //
        //   By flattening PL2 = PL1 = 20W we eliminate the Intel "burst then
        //   throttle" pattern. The CPU gets smooth, predictable 20W sustained
        //   performance — exactly how an Apple M-Series chip delivers stable
        //   compute without thermal overshoot on a thin chassis.
        // ------------------------------------------------------------------
        HwpContext hwp = { kEppHeavy };
        mp_rendezvous_no_intrs(nullptr, writeHwpMsr, nullptr, &hwp);
        setRaplLimits(kRaplHeavyWatts, kRaplHeavyWatts);
        dispatchSamPerfCommand(kSamPerfHeavy);
        kickstartFan();
        LOG("→ Heavy [Sustained Ceiling]: EPP=0x80, RAPL=%uW/%uW (flat), SAM=0x01",
            kRaplHeavyWatts, kRaplHeavyWatts);
    }
}

// =============================================================================
// MARK: - RAPL Power Limit (MSR 0x610) — Lock-Bit Aware, All-Core Synchronised
// =============================================================================

void ThermalCpuPower::setRaplLimits(uint32_t pl1Watts, uint32_t pl2Watts) {
    // --- Read RAPL Power Unit (MSR 0x606, bits [3:0]) ---
    // unit = 2^(-powerUnit) W/LSB.  Ice Lake: typically 3 → 0.125 W/LSB.
    const uint64_t unitMsr   = rdmsr64(kMsrRaplPowerUnit);
    const uint32_t powerUnit = unitMsr & 0x0F;
    // Guard against garbage reads (must be 1–4 for real hardware)
    const uint32_t safeUnit  = (powerUnit >= 1 && powerUnit <= 4) ? powerUnit : 3;

    // --- Check RAPL Lock Bit (MSR 0x610 bit 63) ---
    // If locked, writes are silently discarded by hardware. Log and bail.
    const uint64_t currentMsr = rdmsr64(kMsrPkgPowerLimit);
    if (currentMsr & kRaplLockBit) {
        LOG("RAPL: MSR 0x610 is LOCKED (Bit 63=1). "
            "Cannot update PL1/PL2 — Surface firmware has claimed ownership.");
        return;
    }

    // --- Compute Raw RAPL Values ---
    // raw = watts × 2^powerUnit  (inverse of the unit formula)
    // Example: 20W with safeUnit=3 → 20 × 8 = 160 = 0xA0
    //          12W with safeUnit=3 → 12 × 8 = 96  = 0x60
    const uint64_t pl1Raw = ((uint64_t)pl1Watts << safeUnit) & 0x7FFF;
    const uint64_t pl2Raw = ((uint64_t)pl2Watts << safeUnit) & 0x7FFF;

    // --- Build New MSR Value (preserve TAU / time-window bits) ---
    uint64_t newMsr = currentMsr;

    // PL1 — bits [14:0]: power value, [15]: enable, [16]: clamp
    newMsr &= ~(0x7FFFULL);        // Clear PL1 value
    newMsr |=  pl1Raw;             // Set PL1 value
    newMsr |=  kRaplPL1EnableBit;  // Enable PL1
    newMsr |=  kRaplPL1ClampBit;   // Enable PL1 clamping

    // PL2 — bits [46:32]: power value, [47]: enable, [48]: clamp
    newMsr &= ~(0x7FFFULL << 32);  // Clear PL2 value
    newMsr |=  (pl2Raw << 32);     // Set PL2 value
    newMsr |=  kRaplPL2EnableBit;  // Enable PL2
    newMsr |=  kRaplPL2ClampBit;   // Enable PL2 clamping

    // --- Synchronised Write Across All Logical Processors ---
    RaplContext ctx = { newMsr };
    mp_rendezvous_no_intrs(nullptr, writeRaplMsr, nullptr, &ctx);

    // Compute milliwatts per unit for logging (avoids %f which kernel printf doesn't support)
    // milliwatts_per_unit = 1000 / 2^safeUnit  (e.g. safeUnit=3 → 125 mW/LSB)
    const uint32_t mwPerUnit = 1000U >> safeUnit;
    LOG("RAPL: PL1=%uW (raw=0x%llX), PL2=%uW (raw=0x%llX) [unit=%u mW/LSB, powerUnit=%u]",
        pl1Watts, pl1Raw, pl2Watts, pl2Raw, mwPerUnit, safeUnit);

}

void ThermalCpuPower::writeRaplMsr(void *arg) {
    const RaplContext *ctx = static_cast<const RaplContext *>(arg);
    if (!ctx) return;
    wrmsr64(kMsrPkgPowerLimit, ctx->msr);
}

// =============================================================================
// MARK: - Fan Subsystem (TC 0x05)
// =============================================================================

void ThermalCpuPower::kickstartFan() {
    // Proactively activate the Surface EC fan performance profile the moment
    // we enter Heavy mode. This minimises the initial thermal gradient spike
    // before the 20W ceiling has time to stabilise CPU junction temperature.
    LOG("Fan: Activating performance fan profile (TC=0x05, CID=0x02)...");
    dispatchFanCommand(kSamCidFanKick, kSamPerfHeavy);
}

// =============================================================================
// MARK: - SAM EC Command Dispatch
// =============================================================================

void ThermalCpuPower::dispatchSamPerfCommand(uint8_t profileByte) {
    if (!ssh) { LOG("SAM: ssh is null — skipping perf command."); return; }

    uint8_t payload = profileByte;
    // TC=0x03 (Thermal), TID=0x01, IID=0x01, CID=0x03 (Set Performance Mode)
    UInt16 reqId = ssh->sendCommand(kSamTcThermal, 0x01, 0x01, kSamCidSetPerf,
                                    &payload, sizeof(payload), true);
    if (reqId == 0) {
        LOG("SAM: Performance profile dispatch FAILED (0x%02X).", profileByte);
    } else {
        LOG("SAM: Performance profile 0x%02X dispatched (ReqID=%u).", profileByte, reqId);
    }
}

void ThermalCpuPower::dispatchFanCommand(uint8_t cid, uint8_t payload) {
    if (!ssh) { LOG("SAM: ssh is null — skipping fan command."); return; }

    uint8_t buf = payload;
    // TC=0x05 (Fan), TID=0x01, IID=0x01
    UInt16 reqId = ssh->sendCommand(kSamTcFan, 0x01, 0x01, cid,
                                    &buf, sizeof(buf), false);
    if (reqId == 0) {
        LOG("Fan: TC=0x05 CID=0x%02X dispatch FAILED.", cid);
    } else {
        LOG("Fan: TC=0x05 CID=0x%02X dispatched (ReqID=%u).", cid, reqId);
    }
}

// =============================================================================
// MARK: - CPU Temperature (MSR 0x19C / 0x1A0)
// =============================================================================

uint32_t ThermalCpuPower::readCpuTemperature() {
    // MSR 0x1A0 bits [23:16]: TCC Activation Temperature (typically 100°C on Ice Lake)
    const uint64_t targetMsr = rdmsr64(0x1A0);
    uint32_t tcc = (targetMsr >> 16) & 0xFF;

    // MSR 0x19C (IA32_THERM_STATUS):
    //   Bit  31:    Reading Valid
    //   Bits [22:16]: Digital Readout (temperature offset BELOW TCC)
    //   Actual temp = TCC - offset
    const uint64_t statusMsr = rdmsr64(0x19C);
    const bool     valid     = static_cast<bool>((statusMsr >> 31) & 0x1);
    const uint32_t offset    = (statusMsr >> 16) & 0x7F;

    // Sanity: TCC should be 70–110°C on real Ice Lake hardware
    if (tcc < 70 || tcc > 110) tcc = 100;

    // Return 0 (safely below all thresholds) if sensor isn't ready or offset wraps
    if (!valid || offset > tcc) return 0;

    return tcc - offset;
}

// =============================================================================
// MARK: - HWP MSR Write Rendezvous
// Executed simultaneously on ALL logical CPU cores with interrupts disabled.
// Writes only EPP bits [31:24] — all other HWP fields are left untouched.
// =============================================================================

void ThermalCpuPower::writeHwpMsr(void *arg) {
    const HwpContext *ctx = static_cast<const HwpContext *>(arg);
    if (!ctx) return;

    uint64_t oldMsr = rdmsr64(kMsrHwpRequest);
    uint64_t msr    = oldMsr;

    uint8_t maxPerf = (oldMsr >> 8) & 0xFF;

    msr &= ~((0xFFULL) |
             (0xFFULL << 8) |
             (0xFFULL << 16) |
             (0xFFULL << 24));

    const uint8_t minPerf     = 0x20;
    const uint8_t desiredPerf = 0x00;

    msr |= ((uint64_t)minPerf);
    msr |= ((uint64_t)maxPerf << 8);
    msr |= ((uint64_t)desiredPerf << 16);
    msr |= ((uint64_t)ctx->epp << 24);

    wrmsr64(kMsrHwpRequest, msr);

    LOG("HWP: Min=0x%02X Max=0x%02X Desired=0x%02X EPP=0x%02X | MSR: 0x%llX -> 0x%llX",
        minPerf,
        maxPerf,
        desiredPerf,
        ctx->epp,
        oldMsr,
        msr);
}
