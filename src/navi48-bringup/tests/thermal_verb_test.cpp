// thermal_verb_test.cpp - ticket #18 (spec #14): the read-only thermal verb and the SMU driver table it needs.
//   clang++ -std=c++17 -Wall -Wextra -Werror -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
//       src/navi48-bringup/tests/thermal_verb_test.cpp -o /tmp/thermal_verb && /tmp/thermal_verb .
//   (run from the repo root; the argument is the repo root the source pins read from)
// The verb is the user client's Metrics selector (11, `navi48test metrics`): junction (hotspot), edge and fan
// RPM from the SMU metrics table, the path upstream SMU 14.0.2 reads them by (smu_v14_0_2_ppt.c
// smu_v14_0_2_get_smu_metrics_data: METRICS_TEMPERATURE_HOTSPOT / _EDGE, METRICS_CURR_FANSPEED). Source pins:
//   T1  the verb writes no GPU state: doMetrics calls only smu_read_metrics + smu_log_metrics; smu_read_metrics
//       sends only GetDriverIfVersion and TransferTableSmu2Dram(TABLE_SMU_METRICS) (each helper sends exactly its
//       one query), writes no register except the MM_INDEX read window (RVRAM32_via_mm, which never writes
//       MM_DATA), no VRAM, no clock / feature / power message;
//   T2  the verb refuses (NotReady) until the SMU is online AND the SMU has been told where the driver table is,
//       so PMFW is never asked to DMA a table to an address it was never given;
//   T3  SMUInit tells the SMU where the driver table is even when smc_hw_setup is skipped (stage 9 default),
//       with only SetDriverDramAddrHigh/Low (upstream smu_v14_0_set_driver_table_location) - no feature,
//       clock or power message - and smc_hw_setup's own SetDriverDramAddr marks the table too;
//   T4  the dispatch maps selector 11 to doMetrics, and navi48test's `metrics` prints the lines the harness
//       thermal watch parses (tools/acceptance/lib.sh crit_thermal_watch).
#include <cctype>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

static int gFail = 0, gRun = 0;
static void expect(bool ok, const char *what) { gRun++; if (!ok) { gFail++; std::printf("FAIL: %s\n", what); } }

static std::string slurp(const std::string &path) { std::ifstream f(path, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static bool has(const std::string &s, const std::string &needle) { return s.find(needle) != std::string::npos; }
static size_t count_of(const std::string &s, const std::string &needle) { size_t n = 0, p = 0; while ((p = s.find(needle, p)) != std::string::npos) { n++; p += needle.size(); } return n; }
// The brace-balanced body of the first definition whose head is `head` (the text up to the "{"): "" if absent.
static std::string body(const std::string &s, const std::string &head) {
    size_t a = s.find(head); if (a == std::string::npos) return "";
    size_t b = s.find('{', a); if (b == std::string::npos) return "";
    int depth = 0;
    for (size_t i = b; i < s.size(); i++) {
        if (s[i] == '{') depth++;
        else if (s[i] == '}' && --depth == 0) return s.substr(b, i - b + 1);
    }
    return "";
}
// Every identifier in `s` that is followed by "(" (a call or a macro use), in order.
static std::vector<std::string> calls(const std::string &s) {
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '(') continue;
        size_t e = i; while (e > 0 && s[e - 1] == ' ') e--;
        size_t b = e; while (b > 0 && (isalnum((unsigned char)s[b - 1]) || s[b - 1] == '_' || s[b - 1] == ':')) b--;
        if (b < e) out.push_back(s.substr(b, e - b));
    }
    return out;
}
static bool only_calls(const std::string &s, const std::vector<std::string> &allowed, std::string *bad) {
    for (const std::string &c : calls(s)) {
        bool ok = false;
        for (const std::string &a : allowed) if (c == a) ok = true;
        if (!ok) { if (bad) *bad = c; return false; }
    }
    return true;
}

int main(int argc, char **argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    const std::string K = root + "/src/navi48-bringup/";
    const std::string smu = slurp(K + "src/amd/smu_v14_0.cpp");
    const std::string regs = slurp(K + "src/amd/amdgpu_regs.h");
    const std::string uc = slurp(K + "src/Navi48UserClient.cpp");
    const std::string abi = slurp(K + "src/Navi48UserClientABI.h");
    const std::string tool = slurp(root + "/tools/pc/navi48test.c");
    const std::string suites = slurp(root + "/tools/conductor/suites.sh");
    expect(!smu.empty() && !regs.empty() && !uc.empty() && !abi.empty() && !tool.empty(), "sources readable (run from the repo root)");

    // T1: the verb writes no GPU state.
    const std::string doM = body(uc, "IOReturn Navi48UserClient::doMetrics(");
    expect(!doM.empty(), "T1 doMetrics found");
    std::string bad;
    expect(only_calls(doM, {"sizeof", "deviceContext", "amdgpu::smu_read_metrics", "amdgpu::smu_log_metrics", "memcpy"}, &bad),
           ("T1 doMetrics calls only smu_read_metrics / smu_log_metrics (found " + bad + ")").c_str());
    const std::string rm = body(smu, "smu_read_metrics(const DeviceContext &dev, SmuMetricsResult *out)");
    expect(!rm.empty(), "T1 smu_read_metrics found");
    bad.clear();
    expect(only_calls(rm, {"smu_get_driver_if_version", "SMU_LOG", "smu_driver_table_mc", "smu_transfer_table_smu_to_dram",
                           "reinterpret_cast<uint32_t *>", "sizeof", "RVRAM32_via_mm"}, &bad),
           ("T1 smu_read_metrics sends only the two queries and reads VRAM through RVRAM32_via_mm (found " + bad + ")").c_str());
    expect(has(rm, "smu_transfer_table_smu_to_dram(dev, kSMUTableMetrics)"), "T1 the one transfer is TABLE_SMU_METRICS, SMU to DRAM");
    const std::string tt = body(smu, "smu_transfer_table_smu_to_dram(const DeviceContext &dev, uint32_t table_id)");
    expect(has(tt, "PPSMCTable::TransferTableSmu2Dram") && count_of(tt, "smu_send_msg") == 1, "T1 smu_transfer_table_smu_to_dram sends only TransferTableSmu2Dram");
    const std::string gv = body(smu, "smu_get_driver_if_version(const DeviceContext &dev, uint32_t *outVer)");
    expect(has(gv, "PPSMC::GetDriverIfVersion") && count_of(gv, "smu_send_msg") == 1, "T1 smu_get_driver_if_version sends only GetDriverIfVersion");
    const std::string rv = body(regs, "static inline uint32_t RVRAM32_via_mm(");
    expect(count_of(rv, "WREG32(") == 2 && has(rv, "WREG32(ctx, mmMM_INDEX_HI") && has(rv, "WREG32(ctx, mmMM_INDEX,")
           && !has(rv, "WREG32(ctx, mmMM_DATA") && has(rv, "return RREG32(ctx, mmMM_DATA)"),
           "T1 RVRAM32_via_mm writes only the MM_INDEX window and reads MM_DATA");

    // T2: refuse until the SMU is online and knows the driver table.
    const size_t gate = rm.find("if (!dev.smuOnline || !dev.smuDriverTable)");
    expect(gate != std::string::npos, "T2 smu_read_metrics refuses unless smuOnline && smuDriverTable");
    expect(gate != std::string::npos && gate < rm.find("smu_get_driver_if_version(") && has(rm.substr(gate, 300), "return kIOReturnNotReady"),
           "T2 the refusal comes before the first SMU message and answers NotReady");
    expect(has(regs, "bool smuDriverTable { false };"), "T2 DeviceContext::smuDriverTable starts false");

    // T3: SMUInit places the driver table without changing clocks or power.
    const std::string loc = body(smu, "smu_set_driver_table_location(DeviceContext &dev)");
    expect(!loc.empty(), "T3 smu_set_driver_table_location found");
    expect(has(loc, "PPSMC::SetDriverDramAddrHigh") && has(loc, "PPSMC::SetDriverDramAddrLow") && count_of(loc, "smu_send_msg") == 2,
           "T3 smu_set_driver_table_location sends only SetDriverDramAddrHigh/Low");
    expect(has(loc, "dev.smuDriverTable = true"), "T3 smu_set_driver_table_location marks the table");
    const std::string hw = body(smu, "smu_hw_init(DeviceContext &dev, bool runSmcHwSetup)");
    const size_t skip = hw.find("if (!runSmcHwSetup)");
    expect(skip != std::string::npos && has(body(hw.substr(skip), "if (!runSmcHwSetup)"), "smu_set_driver_table_location(dev)"),
           "T3 the skipped-setup path of smu_hw_init places the driver table");
    for (const char *m : {"EnableAllSmuFeatures", "SetAllowedFeaturesMask", "SetSoft", "RunDcBtc", "UseDefaultPPTable", "NotifyPowerSource", "DisableAllSmuFeatures"})
        expect(!has(loc, m), (std::string("T3 smu_set_driver_table_location sends no ") + m).c_str());
    const std::string hs = body(smu, "smu_smc_hw_setup(DeviceContext &dev, uint64_t driver_table_mc)");
    expect(has(hs, "dev.smuDriverTable = (driver_table_mc == smu_driver_table_mc(dev))"),
           "T3 smc_hw_setup marks the table only when it is the slot smu_read_metrics reads");

    // T4: the dispatch and the tool's output the harness parses.
    expect(has(abi, "kNavi48SelMetrics") && has(uc, "doMetrics(args)"), "T4 the Metrics selector dispatches to doMetrics");
    expect(has(tool, "\"temps   edge %3u C  hotspot %3u C  mem %3u C   fan %u rpm (%u%%)\\n\""), "T4 navi48test metrics prints the temps line crit_thermal_watch parses");
    expect(has(tool, "values are UNVERIFIED (range check: %s)"), "T4 navi48test metrics prints the range-check verdict crit_thermal_watch reads");
    expect(has(suites, "run thermal_verb "), "T4 the host suite runs this test");

    std::printf("thermal_verb: %d checks, %d failed\n", gRun, gFail);
    return gFail ? 1 : 0;
}
