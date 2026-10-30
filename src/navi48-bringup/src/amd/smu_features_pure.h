// smu_features_pure.h - ticket #23 (spec #14): the PURE half of stage 9's minimal SMU feature setup on the Navi 44
// path (SMU 14.0.2): the smallest allowed-feature mask that gives the bench what it needs, and the message plan
// that hands it to PMFW. No kernel header is included, so tests/smu_features_test.cpp runs the exact function the
// kext calls (smu_minimal_hw_setup in smu_v14_0.cpp) against a fake mailbox.
//
// What the mask must buy, and nothing more:
//   - PMFW fills the metrics table (the #18 bench run of 2026-10-30: with no feature enabled, PMFW answers
//     TransferTableSmu2Dram(TABLE_SMU_METRICS) OK but leaves the table all zero);
//   - GFXCLK DPM runs, so the idle clamp's SetSoftMaxByFreq(GFXCLK, 200) is accepted (upstream
//     smu_v14_0_set_soft_freq_limited_range returns early unless the clock's DPM feature is enabled, and PMFW
//     answered 0xFF Failed with DPM off on the same bench run);
//   - PMFW owns thermal protection and the fan.
// Everything that can move a clock the firmware console's scanout depends on (display/DCN, memory and fabric
// clocks, the PCIe link, hub power gating, deep sleep) stays off: nothing at stage 9 programs DCN watermarks or
// the PCIe DPM table (upstream does both, via DC and smu_update_pcie_parameters, before it lets those switch).
//
// Upstream reference (torvalds/linux master d89e1d18, read-only):
//   bits    drivers/gpu/drm/amd/pm/swsmu/inc/pmfw_if/smu14_driver_if_v14_0.h, FEATURE_*_BIT (0..63)
//   ids     drivers/gpu/drm/amd/pm/swsmu/inc/pmfw_if/smu_v14_0_2_ppsmc.h, PPSMC_MSG_*
//   mask    smu_v14_0_2_ppt.c smu_v14_0_2_init_allowed_features allows ALL 64 bits; this module is the deliberate
//           deviation. Under SCPM upstream sends no allowed mask at all (amdgpu_smu.c smu_smc_hw_setup).
//   order   amdgpu_smu.c smu_smc_hw_setup: set_driver_table_location -> setup_pptable (14.0.2: COMBO_PPTABLE read
//           from PMFW) -> write_pptable (not under SCPM) -> run_btc (RunDcBtc) -> set_allowed_mask (smu_v14_0.c
//           smu_v14_0_set_allowed_mask: High, then Low) -> update_pcie_parameters -> system_features_control
//           (EnableAllSmuFeatures) -> get_enabled_mask (smu_cmn.c smu_cmn_get_enabled_mask: 14.0.2 maps no
//           GetEnabledSmuFeatures, so GetRunningSmuFeaturesHigh, then Low).
#pragma once
#include <stdint.h>

namespace smu14feat {

// ---- SMU 14 feature bits (smu14_driver_if_v14_0.h) ---------------------------------------------------------------------
constexpr int kFeatureFwDataRead            = 0;
constexpr int kFeatureDpmGfxclk             = 1;
constexpr int kFeatureDpmGfxPowerOptimizer  = 2;
constexpr int kFeatureDpmUclk               = 3;
constexpr int kFeatureDpmFclk               = 4;
constexpr int kFeatureDpmSocclk             = 5;
constexpr int kFeatureDpmLink               = 6;
constexpr int kFeatureDpmDcn                = 7;
constexpr int kFeatureDsDcfclk              = 14;
constexpr int kFeatureGfxoff                = 18;
constexpr int kFeatureThrottlers            = 23;
constexpr int kFeatureFwCtf                 = 28;
constexpr int kFeatureFanControl            = 29;
constexpr int kFeatureDfCstate              = 40;
constexpr int kFeatureMemTempRead           = 46;
constexpr int kFeatureAthubMmhubPg          = 47;

constexpr uint64_t bit(int n) { return (uint64_t)1 << n; }

// ---- the minimal allowed-feature mask, bit by bit ---------------------------------------------------------------------
// IN (6 bits):
//    0 FW_DATA_READ     PMFW's own data/fuse read; every DPM and the metrics producer sit on it. It does not move a
//                       clock. (Upstream allows it; it is in every 14.0.2 FeaturesToRun.)
//    1 DPM_GFXCLK       required: SetSoftMaxByFreq(GFXCLK) is ignored by upstream and refused by PMFW without it
//                       (smu_v14_0.c smu_v14_0_set_soft_freq_limited_range -> smu_cmn_clk_dpm_is_enabled, which
//                       smu_v14_0_2_ppt.c maps GFXCLK to SMU_FEATURE_DPM_GFXCLK_BIT). GFX is not scanned out.
//   23 THROTTLERS       PMFW's PPT / TDC / temperature throttlers (ThrottlerControlMask, THROTTLER_*_BIT): thermal
//                       protection by pulling GFXCLK down. Acts only when a limit is hit.
//   28 FW_CTF           firmware critical-temperature shutdown: the last line of thermal protection.
//   29 FAN_CONTROL      PMFW runs the fan curve (smu_v14_0_2_check_powerplay_table: no_fan is "FeaturesToRun[0]
//                       lacks FEATURE_FAN_CONTROL_BIT", i.e. upstream treats this bit as "PMFW owns the fan").
//   46 MEM_TEMP_READ    PMFW reads the memory temperature, which the metrics table (AvgTemperature[TEMP_MEM]), the
//                       THROTTLER_TEMP_MEM throttler and the fan curve use. A sensor read; no clock moves.
// OUT (58 bits), and why:
//    2 DPM_GFX_POWER_OPTIMIZER  not needed for GFXCLK DPM or the clamp.
//    3 DPM_UCLK, 4 DPM_FCLK     memory / fabric clock switching under a live scanout needs DCN watermarks and a
//    8 VMEMP_SCALING            switch-time contract (upstream DC programs TABLE_WATERMARKS first); stage 9 has
//    9 VDDIO_MEM_SCALING        neither, so a switch can underflow the console. Memory voltage goes with UCLK.
//    5 DPM_SOCCLK               SOCCLK clocks the SOC / display data path; not needed for GFXCLK DPM.
//    6 DPM_LINK                 PCIe gen / width switching; upstream programs it with OverridePcieParameters,
//                               which this plan does not send. The Mac's PCIe link stays where firmware left it.
//    7 DPM_DCN                  display clocks: the console is scanning out.
//   10..15 DS_GFXCLK, DS_SOCCLK, DS_FCLK, DS_LCLK, DS_DCFCLK, DS_UCLK
//                               deep sleep: clocks drop when idle - DCFCLK and UCLK feed the scanout.
//   16 GFX_ULV, 17 FW_DSTATE, 18 GFXOFF
//                               GFX low-voltage / power-off states: would power the GFX core down under the
//                               later bring-up stages (upstream drives GFXOFF itself, through gfx_v12_0).
//   19 BACO, 22 BACO_MPCLK_DS, 45 BACO_CG
//                               bus-active chip-off: never at bring-up.
//   20 MM_DPM, 44 DS_VCN        video engines: unused.
//   21 SOC_MPCLK_DS             SOC deep sleep.
//   24 SMARTSHIFT, 26 ACDC      laptop / APU power sharing and AC-DC GPIO.
//   25 GTHR, 27 VR0HOT          board GPIO thermal / VR-hot inputs: board-dependent; the temperature throttlers
//                               (bit 23, THROTTLER_TEMP_VR_*) and FW_CTF already protect.
//   30 GFX_DCS, 31 GFX_READ_MARGIN, 35 OPTIMIZED_VMIN, 38 GFX_PCC_DFLL, 41 GFX_EDC, 49 EDC_PWRBRK,
//   50 SOC_EDC_XVMIN, 51 GFX_PSM_DIDT, 52 APT_ALL_ENABLE, 53 APT_SQ_THROTTLE, 54 APT_PF_DCS, 55 GFX_EDC_XVMIN,
//   56 GFX_DIDT_XVMIN, 58 CLOCK_STRETCH_COMPENSATOR
//                               performance / voltage-margin optimisers: not needed at idle.
//   32 LED_DISPLAY, 34 OUT_OF_BAND_MONITOR
//                               board LEDs and the out-of-band (BMC) channel: unused.
//   33 GFXCLK_SPREAD_SPECTRUM   changes the GFX PLL; not needed.
//   36 GFX_IMU                  the IMU is brought up by the later stages (IMUInit), not by PMFW at stage 9.
//   37 BOOT_TIME_CAL            RunDcBtc runs regardless of this bit (upstream sends it unconditionally).
//   39 SOC_CG                   SOC clock gating.
//   40 DF_CSTATE                data-fabric C-states stall memory access - the console reads VRAM.
//   42 BOOT_POWER_OPT, 43 CLOCK_POWER_DOWN_BYPASS
//                               boot-power optimisation and clock power-down: not needed.
//   47 ATHUB_MMHUB_PG           power-gates the memory hubs the display fetches through.
//   48 SOC_PCC                  SOC peak-current control.
//   57 FAN_ABNORMAL             fan-failure detection: a candidate to add once FAN_CONTROL is seen working.
//   59..63 SPARE
constexpr uint64_t kMinimalAllowedMask =
    bit(kFeatureFwDataRead) | bit(kFeatureDpmGfxclk) | bit(kFeatureThrottlers) |
    bit(kFeatureFwCtf) | bit(kFeatureFanControl) | bit(kFeatureMemTempRead);

constexpr uint32_t mask_lo(uint64_t m) { return (uint32_t)(m & 0xFFFFFFFFu); }
constexpr uint32_t mask_hi(uint64_t m) { return (uint32_t)(m >> 32); }

static_assert((kMinimalAllowedMask & (bit(kFeatureDpmUclk) | bit(kFeatureDpmFclk) | bit(kFeatureDpmSocclk) | bit(kFeatureDpmLink) |
                                      bit(kFeatureDpmDcn) | bit(kFeatureDsDcfclk) | bit(kFeatureGfxoff) | bit(kFeatureDfCstate) |
                                      bit(kFeatureAthubMmhubPg))) == 0,
              "no display, memory, fabric, link, GFXOFF or hub power-gating feature on the console path");

// ---- PPSMC message ids (smu_v14_0_2_ppsmc.h); smu_v14_0.cpp static_asserts them against PPSMC:: -----------------------
constexpr uint32_t kMsgSetAllowedFeaturesMaskLow  = 0x04;
constexpr uint32_t kMsgSetAllowedFeaturesMaskHigh = 0x05;
constexpr uint32_t kMsgEnableAllSmuFeatures       = 0x06;
constexpr uint32_t kMsgEnableSmuFeaturesLow       = 0x08;
constexpr uint32_t kMsgEnableSmuFeaturesHigh      = 0x09;
constexpr uint32_t kMsgGetRunningSmuFeaturesLow   = 0x0C;
constexpr uint32_t kMsgGetRunningSmuFeaturesHigh  = 0x0D;
constexpr uint32_t kMsgRunDcBtc                   = 0x36;

// ---- the plan ----------------------------------------------------------------------------------------------------------
enum class Outcome : uint8_t {
    NoDriverTable,   // the driver table was not placed: nothing sent (upstream aborts smc_hw_setup here too)
    BtcFailed,       // RunDcBtc refused: no feature message sent (upstream aborts here too)
    EnableFailed,    // the enable message(s) were refused
    Enabled,         // the enable message(s) were accepted
};
enum class EnablePath : uint8_t {
    None,
    AllowedMask,      // PMFW took the minimal allowed mask; EnableAllSmuFeatures enabled (allowed & supported)
    SelectiveEnable,  // PMFW refused the allowed mask (it answers UnknownCmd under SCPM); EnableSmuFeaturesLow/High
                      // with the same minimal mask instead (upstream smu_cmn_feature_update_enable_state)
};
struct MinimalSetupResult {
    Outcome    outcome { Outcome::NoDriverTable };
    EnablePath path { EnablePath::None };
    bool       maskHiOk { false }, maskLoOk { false };
    bool       runningRead { false };
    uint64_t   running { 0 };     // GetRunningSmuFeaturesHigh:Low, valid when runningRead
};

inline const char *outcome_name(Outcome o) {
    switch (o) {
    case Outcome::NoDriverTable: return "no driver table";
    case Outcome::BtcFailed:     return "RunDcBtc refused";
    case Outcome::EnableFailed:  return "feature enable refused";
    case Outcome::Enabled:       return "enabled";
    }
    return "?";
}
inline const char *path_name(EnablePath p) {
    switch (p) {
    case EnablePath::None:            return "none";
    case EnablePath::AllowedMask:     return "allowed mask + EnableAllSmuFeatures";
    case EnablePath::SelectiveEnable: return "EnableSmuFeaturesLow/High (allowed mask refused)";
    }
    return "?";
}

// run_minimal_setup: the messages, in upstream order, after smu_set_driver_table_location has placed the driver table
// and run the #18 probe (which is upstream's 14.0.2 smu_setup_pptable read: TransferTableSmu2Dram(COMBO_PPTABLE)).
// `send(id, param, out)` sends one PPSMC message and answers whether PMFW said OK; `out` (may be null) gets the
// message's return value. Not sent, on purpose (upstream steps this plan leaves out):
//   set_tool_table_location / notify_memory_pool_location  - no PMSTATUSLOG buffer, no DRAM log pool;
//   write_pptable (TransferTableDram2Smu)                  - PMFW keeps its own (IFWI) pptable; upstream skips it
//                                                            under SCPM, and the driver table holds the sentinel;
//   UseDefaultPPTable, NotifyPowerSource                   - neither is in upstream 14.0.2 smc_hw_setup (the power
//                                                            source is set later, in smu_late_init);
//   update_pcie_parameters (OverridePcieParameters)        - DPM_LINK is off;
//   set_default_dpm_table onwards                          - driver-side tables, IRQs, display notifications.
// Fail closed: an allowed mask PMFW refuses never leads to EnableAllSmuFeatures, which would enable PMFW's own set
// (everything its pptable lists, display and memory clocks included).
template <typename Send>
inline MinimalSetupResult run_minimal_setup(bool driverTablePlaced, Send &&send) {
    MinimalSetupResult r;
    if (!driverTablePlaced) return r;
    if (!send(kMsgRunDcBtc, 0u, nullptr)) { r.outcome = Outcome::BtcFailed; return r; }

    const uint32_t lo = mask_lo(kMinimalAllowedMask), hi = mask_hi(kMinimalAllowedMask);
    r.maskHiOk = send(kMsgSetAllowedFeaturesMaskHigh, hi, nullptr);
    r.maskLoOk = r.maskHiOk && send(kMsgSetAllowedFeaturesMaskLow, lo, nullptr);
    bool enabled;
    if (r.maskHiOk && r.maskLoOk) {
        r.path = EnablePath::AllowedMask;
        enabled = send(kMsgEnableAllSmuFeatures, 0u, nullptr);
    } else {
        r.path = EnablePath::SelectiveEnable;
        enabled = send(kMsgEnableSmuFeaturesLow, lo, nullptr) && send(kMsgEnableSmuFeaturesHigh, hi, nullptr);
    }
    r.outcome = enabled ? Outcome::Enabled : Outcome::EnableFailed;

    // Read-only, also after a refused enable: the log then says what PMFW runs.
    uint32_t rh = 0, rl = 0;
    r.runningRead = send(kMsgGetRunningSmuFeaturesHigh, 0u, &rh) && send(kMsgGetRunningSmuFeaturesLow, 0u, &rl);
    if (r.runningRead) r.running = ((uint64_t)rh << 32) | rl;
    return r;
}

inline bool gfxclk_dpm_running(const MinimalSetupResult &r) { return r.runningRead && (r.running & bit(kFeatureDpmGfxclk)) != 0; }
// Running features PMFW reports outside the minimal mask (0 when the running mask could not be read).
inline uint64_t outside_mask(const MinimalSetupResult &r) { return r.runningRead ? (r.running & ~kMinimalAllowedMask) : 0; }

}  // namespace smu14feat
