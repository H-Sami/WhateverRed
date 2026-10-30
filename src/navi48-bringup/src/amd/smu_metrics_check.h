// smu_metrics_check.h - ticket #18: can a decoded SMU metrics sample be a real reading from a powered GPU?
//
// Pure (no kernel types) so tests/thermal_verb_test.cpp exercises it on the host. smu_read_metrics calls it on
// every sample; any bit it returns makes the sample implausible and the thermal verb reports it as such
// (Navi48Metrics.values_plausible = 0, implausible_why = the bits). The bench run of 2026-10-30 showed why the
// lower bounds matter: PMFW 102.70.0 answered TransferTableSmu2Dram(TABLE_SMU_METRICS) OK but the table stayed
// all zero, and the old check (upper bounds only) called edge 0 C, hotspot 0 C, PCIe gen 0 x0 plausible.
#ifndef SMU_METRICS_CHECK_H
#define SMU_METRICS_CHECK_H

#include <stdint.h>
#include "../Navi48UserClientABI.h"

namespace amdgpu {

// Fill pattern for the driver table before PMFW is asked to write it (smu_set_driver_table_location). A table
// that still reads back as this pattern was never written, which tells "PMFW wrote nothing where we read" apart
// from "PMFW wrote zeros". PMFW only reads the driver table on TransferTableDram2Smu, which nothing at stage 9
// sends, so the pattern is never consumed as data.
constexpr uint32_t kSMUDriverTableSentinel = 0x5E17AB1Eu;

constexpr uint32_t kMetricsMinTempC     = 5;     // a powered GPU in a room is never below this
constexpr uint32_t kMetricsMaxTempC     = 125;   // nor above this
constexpr uint32_t kMetricsMaxClockMHz  = 4000;
constexpr uint32_t kMetricsMaxPcieGen   = 5;     // upstream LINK_SPEED_MAX (smu_v14_0_2_ppt.c treats PcieRate 1-based)
constexpr uint32_t kMetricsMaxPcieWidth = 32;

// The fields the check reads, already decoded, plus what the raw table looked like.
struct SmuMetricsSample {
    uint32_t gfx_mhz, mem_mhz;
    uint32_t edge_c, hotspot_c;
    uint32_t gfx_busy_pct, mem_busy_pct;
    uint32_t pcie_gen, pcie_width;
    bool     table_zero;        // every dword of the table read zero
    bool     table_untouched;   // every dword of the table still kSMUDriverTableSentinel
};

// NAVI48_METRICS_WHY_* bits; 0 = plausible.
inline uint32_t smu_metrics_implausible(const SmuMetricsSample &s)
{
    uint32_t why = 0;
    if (s.table_zero) why |= NAVI48_METRICS_WHY_TABLE_ZERO;
    if (s.table_untouched) why |= NAVI48_METRICS_WHY_UNTOUCHED;
    if (s.edge_c < kMetricsMinTempC || s.hotspot_c < kMetricsMinTempC) why |= NAVI48_METRICS_WHY_TEMP_LOW;
    if (s.edge_c > kMetricsMaxTempC || s.hotspot_c > kMetricsMaxTempC) why |= NAVI48_METRICS_WHY_TEMP_HIGH;
    if (s.hotspot_c < s.edge_c) why |= NAVI48_METRICS_WHY_HOT_BELOW_EDGE;
    if (s.gfx_mhz > kMetricsMaxClockMHz || s.mem_mhz > kMetricsMaxClockMHz) why |= NAVI48_METRICS_WHY_CLOCK;
    if (s.gfx_busy_pct > 100 || s.mem_busy_pct > 100) why |= NAVI48_METRICS_WHY_ACTIVITY;
    if (s.pcie_gen < 1 || s.pcie_gen > kMetricsMaxPcieGen || s.pcie_width < 1 || s.pcie_width > kMetricsMaxPcieWidth)
        why |= NAVI48_METRICS_WHY_PCIE_LINK;
    return why;
}

// Short names for the bits, in bit order, for the kext log.
inline const char *smu_metrics_why_name(uint32_t bit)
{
    switch (bit) {
    case NAVI48_METRICS_WHY_TABLE_ZERO:     return "table all zero";
    case NAVI48_METRICS_WHY_UNTOUCHED:      return "table never written (still the sentinel)";
    case NAVI48_METRICS_WHY_TEMP_LOW:       return "edge/hotspot below 5 C";
    case NAVI48_METRICS_WHY_TEMP_HIGH:      return "edge/hotspot above 125 C";
    case NAVI48_METRICS_WHY_HOT_BELOW_EDGE: return "hotspot below edge";
    case NAVI48_METRICS_WHY_CLOCK:          return "clock above 4000 MHz";
    case NAVI48_METRICS_WHY_ACTIVITY:       return "activity above 100%";
    case NAVI48_METRICS_WHY_PCIE_LINK:      return "no live PCIe link";
    default:                                return "?";
    }
}

} // namespace amdgpu

#endif // SMU_METRICS_CHECK_H
