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
//   T5  fail closed (bench 2026-10-30: an all-zero table passed the old upper-bound-only check): the kext's
//       plausibility check (amd/smu_metrics_check.h, run here on the host) rejects an all-zero or never-written
//       table, edge / hotspot below 5 C, and a PCIe link of gen 0 / x0; smu_read_metrics uses it on every sample
//       and the verb hands the reasons out; navi48test runs its own range check and prints the verdict line.
//   T6  the driver table is filled with a sentinel and HDP-flushed before PMFW is told where it is, and one
//       read-only TransferTableSmu2Dram(TABLE_COMBO_PPTABLE) - the transfer upstream smu_smc_hw_setup sends
//       before any metrics read (smu_setup_pptable -> smu_cmn_get_combo_pptable) - shows whether PMFW writes
//       the slot at all; the slot is re-filled after. Still no feature, clock or power message.
//   T7  the THM register verb (selector 19) only reads: IP discovery's THM block (hw id 3), four registers at
//       the thm_14_0_2_offset.h offsets, no register write, no SMU message; navi48test's `metrics` (the only verb
//       the sudo rule allows) calls only the Metrics and ThermalRegs selectors.
#include "../src/amd/smu_metrics_check.h"
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
// `s` with comments and string / char literals blanked, so prose in them never reads as a call.
static std::string code_only(const std::string &in) {
    std::string s = in;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '/') { while (i < s.size() && s[i] != '\n') s[i++] = ' '; }
        else if (s[i] == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            while (i < s.size() && !(s[i] == '*' && i + 1 < s.size() && s[i + 1] == '/')) s[i++] = ' ';
            if (i + 1 < s.size()) { s[i] = ' '; s[i + 1] = ' '; i++; }
        } else if (s[i] == '"' || s[i] == '\'') {
            const char q = s[i];
            for (i++; i < s.size() && s[i] != q; i++) { if (s[i] == '\\' && i + 1 < s.size()) s[i++] = ' '; s[i] = ' '; }
        }
    }
    return s;
}
// Every identifier in `s` that is followed by "(" (a call or a macro use; not a keyword), in order.
static std::vector<std::string> calls(const std::string &in) {
    const std::string s = code_only(in);
    std::vector<std::string> out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] != '(') continue;
        size_t e = i; while (e > 0 && s[e - 1] == ' ') e--;
        size_t b = e; while (b > 0 && (isalnum((unsigned char)s[b - 1]) || s[b - 1] == '_' || s[b - 1] == ':')) b--;
        const std::string id = s.substr(b, e - b);
        if (b < e && id != "if" && id != "for" && id != "while" && id != "switch" && id != "return") out.push_back(id);
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
                           "reinterpret_cast<uint32_t *>", "sizeof", "RVRAM32_via_mm", "smu_metrics_implausible"}, &bad),
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

    // T5: fail closed. The pure check first, on the samples that matter.
    using amdgpu::SmuMetricsSample;
    using amdgpu::smu_metrics_implausible;
    const SmuMetricsSample idle {200, 96, 35, 37, 0, 0, 4, 16, false, false};
    expect(smu_metrics_implausible(idle) == 0, "T5 a 35/37 C idle at PCIe gen 4 x16 is plausible");
    const SmuMetricsSample bench {0, 0, 0, 0, 0, 0, 0, 0, true, false};   // 2026-10-30, kext 36EA2F95: every field 0
    const uint32_t bw = smu_metrics_implausible(bench);
    expect(bw != 0, "T5 the bench's all-zero sample is implausible");
    expect((bw & NAVI48_METRICS_WHY_TABLE_ZERO) && (bw & NAVI48_METRICS_WHY_TEMP_LOW) && (bw & NAVI48_METRICS_WHY_PCIE_LINK),
           "T5 ... for three independent reasons: zero table, temps below 5 C, no PCIe link");
    SmuMetricsSample s = idle; s.table_zero = true;
    expect(smu_metrics_implausible(s) == NAVI48_METRICS_WHY_TABLE_ZERO, "T5 a zero table alone is enough");
    s = idle; s.table_untouched = true;
    expect(smu_metrics_implausible(s) == NAVI48_METRICS_WHY_UNTOUCHED, "T5 a never-written (sentinel) table alone is enough");
    s = idle; s.edge_c = 4;
    expect(smu_metrics_implausible(s) == NAVI48_METRICS_WHY_TEMP_LOW, "T5 edge 4 C is implausible");
    s = idle; s.edge_c = 5; s.hotspot_c = 5;
    expect(smu_metrics_implausible(s) == 0, "T5 edge and hotspot of exactly 5 C pass");
    s = idle; s.hotspot_c = 0; s.edge_c = 0;
    expect(smu_metrics_implausible(s) & NAVI48_METRICS_WHY_TEMP_LOW, "T5 hotspot 0 C is implausible even when the rest looks alive");
    s = idle; s.hotspot_c = 126;
    expect(smu_metrics_implausible(s) == NAVI48_METRICS_WHY_TEMP_HIGH, "T5 hotspot 126 C is implausible");
    s = idle; s.hotspot_c = 125;
    expect(smu_metrics_implausible(s) == 0, "T5 hotspot 125 C passes the plausibility check (the watch's 95 C limit is separate)");
    s = idle; s.hotspot_c = 30;
    expect(smu_metrics_implausible(s) == NAVI48_METRICS_WHY_HOT_BELOW_EDGE, "T5 hotspot below edge is implausible");
    s = idle; s.pcie_gen = 0;
    expect(smu_metrics_implausible(s) == NAVI48_METRICS_WHY_PCIE_LINK, "T5 PCIe gen 0 is implausible");
    s = idle; s.pcie_width = 0;
    expect(smu_metrics_implausible(s) == NAVI48_METRICS_WHY_PCIE_LINK, "T5 PCIe x0 is implausible");
    s = idle; s.pcie_gen = 6;
    expect(smu_metrics_implausible(s) == NAVI48_METRICS_WHY_PCIE_LINK, "T5 PCIe gen 6 is implausible (LINK_SPEED_MAX 5)");
    s = idle; s.pcie_gen = 1; s.pcie_width = 1;
    expect(smu_metrics_implausible(s) == 0, "T5 PCIe gen 1 x1 passes");
    s = idle; s.gfx_mhz = 4001;
    expect(smu_metrics_implausible(s) == NAVI48_METRICS_WHY_CLOCK, "T5 a 4001 MHz clock is implausible");
    s = idle; s.mem_busy_pct = 101;
    expect(smu_metrics_implausible(s) == NAVI48_METRICS_WHY_ACTIVITY, "T5 101 % activity is implausible");
    // A table PMFW never wrote decodes to the sentinel's halves: implausible however the fields fall.
    const uint32_t sh = amdgpu::kSMUDriverTableSentinel & 0xFFFFu, sl = amdgpu::kSMUDriverTableSentinel >> 16;
    const SmuMetricsSample sent {amdgpu::kSMUDriverTableSentinel, amdgpu::kSMUDriverTableSentinel, sh, sl, sh, sl,
                                 amdgpu::kSMUDriverTableSentinel & 0xFFu, (amdgpu::kSMUDriverTableSentinel >> 8) & 0xFFu, false, true};
    expect(smu_metrics_implausible(sent) & NAVI48_METRICS_WHY_UNTOUCHED, "T5 a sentinel table is flagged as never written");
    for (uint32_t b = 1; b <= NAVI48_METRICS_WHY_PCIE_LINK; b <<= 1)
        expect(std::string(amdgpu::smu_metrics_why_name(b)) != "?", "T5 every reason bit has a log name");

    // ... and the kext and tool use it.
    const std::string rmc = code_only(rm);
    expect(has(rmc, "smu_metrics_implausible(") && has(rmc, "out->values_plausible = (out->implausible_why == 0)"),
           "T5 smu_read_metrics' verdict is exactly the shared check (no other path to 'plausible')");
    expect(has(rmc, "kSMUDriverTableSentinel") && has(rmc, "table_zero") && has(rmc, "table_untouched"),
           "T5 smu_read_metrics classifies the raw table (all zero / never written) before decoding");
    expect(has(smu, "#include \"smu_metrics_check.h\""), "T5 smu_v14_0.cpp uses the shared check");
    expect(has(code_only(doM), "out.implausible_why") && has(code_only(doM), "res.implausible_why"), "T5 the verb hands the reasons out");
    const std::string cm = body(tool, "static int cmd_metrics(int repeat)");
    expect(!cm.empty(), "T5 navi48test cmd_metrics found");
    expect(has(tool, "\"range check: %s\"") || has(tool, "printf(\"range check: "), "T5 navi48test prints a range-check verdict line on every sample");
    expect(has(cm, "metrics_tool_check("), "T5 navi48test runs its own range check, not only the kext's");
    const std::string tc = body(tool, "static uint32_t metrics_tool_check(");
    expect(has(tc, "< 5") && has(tc, "pcie_gen") && has(tc, "pcie_width") && has(tc, "temp_hotspot_c") && has(tc, "temp_edge_c"),
           "T5 navi48test's check covers low temps and the PCIe link");

    // T6: the driver table is sentinel-filled, HDP-flushed, and probed with one read-only transfer.
    const std::string locc = code_only(loc);
    expect(has(locc, "bar0_memset_vram(dev, dev.vramBase + kSMUDriverTableVRAMOffset, kSMUDriverTableSentinel, kSMUDriverTableSize)"),
           "T6 smu_set_driver_table_location fills the slot with the sentinel");
    const size_t fill = locc.find("bar0_memset_vram("), flush = locc.find("amdgpu_hdp_flush(dev)"), tell = locc.find("PPSMC::SetDriverDramAddrHigh");
    expect(fill != std::string::npos && flush != std::string::npos && fill < flush && flush < tell,
           "T6 ... and HDP-flushes it before telling PMFW where it is");
    expect(has(locc, "smu_driver_table_probe(dev)") && locc.find("smu_driver_table_probe(dev)") > locc.find("dev.smuDriverTable = true"),
           "T6 the probe runs only once the table is placed");
    const std::string pr = body(smu, "smu_driver_table_probe(const DeviceContext &dev)");
    expect(!pr.empty(), "T6 smu_driver_table_probe found");
    bad.clear();
    expect(only_calls(pr, {"smu_transfer_table_smu_to_dram", "RVRAM32_via_mm", "bar0_memset_vram", "amdgpu_hdp_flush", "SMU_LOG",
                           "smu_driver_table_classify"}, &bad),
           ("T6 the probe only transfers SMU->DRAM, reads, re-fills and flushes (found " + bad + ")").c_str());
    expect(has(pr, "smu_transfer_table_smu_to_dram(dev, kSMUTableComboPPTable)") && count_of(pr, "smu_transfer_table_smu_to_dram(") == 1,
           "T6 the probe's one transfer is TABLE_COMBO_PPTABLE, SMU to DRAM");
    expect(has(smu, "constexpr uint32_t kSMUTableComboPPTable = 1;") || has(slurp(K + "src/amd/amdgpu_smu.h"), "constexpr uint32_t kSMUTableComboPPTable   = 1;"),
           "T6 TABLE_COMBO_PPTABLE is 1 (smu14_driver_if_v14_0.h)");
    const size_t xfer = pr.find("smu_transfer_table_smu_to_dram("), refill = pr.rfind("bar0_memset_vram(");
    expect(refill != std::string::npos && xfer < refill && pr.rfind("amdgpu_hdp_flush(dev)") > refill,
           "T6 the probe re-fills the sentinel and flushes after the transfer, so the first metrics read starts clean");
    for (const char *m : {"TransferTableDram2Smu", "smu_transfer_table_dram_to_smu", "smu_send_msg"})
        expect(!has(code_only(pr), m), (std::string("T6 the probe sends no ") + m).c_str());

    // T7: the THM register verb only reads.
    expect(has(abi, "kNavi48SelThermalRegs     = 19,") && has(abi, "struct Navi48ThermalRegs"), "T7 selector 19 and its struct are in the ABI");
    expect(has(uc, "case kNavi48SelThermalRegs:") && has(uc, "return doThermalRegs(args);"), "T7 selector 19 dispatches to doThermalRegs");
    const std::string tr = body(uc, "IOReturn Navi48UserClient::doThermalRegs(");
    expect(!tr.empty(), "T7 doThermalRegs found");
    bad.clear();
    expect(only_calls(tr, {"sizeof", "ipDisc", "findIp", "regReadIp", "memcpy"}, &bad),
           ("T7 doThermalRegs only looks THM up and reads registers (found " + bad + ")").c_str());
    for (const char *off : {"kThmTconCurTmp = 0x00", "kThmCgMultThermalStatus = 0x0f", "kThmCgTachCtrl = 0x14", "kThmCgTachStatus = 0x15"})
        expect(has(uc, off), (std::string("T7 THM offset from thm_14_0_2_offset.h: ") + off).c_str());
    expect(has(slurp(K + "src/ipdiscovery.hpp"), "HwThm    = 3,"), "T7 THM is discovery hw id 3 (soc15_hw_ip.h THM_HWID)");
    expect(count_of(code_only(tr), "regReadIp(IpDiscovery::HwThm, 0, 0, ") == 4, "T7 four reads, all from THM instance 0 segment 0");
    const std::string bri = slurp(K + "src/Navi48Bringup.cpp");
    const std::string rri = body(bri, "uint32_t Navi48Bringup::regReadIp(uint16_t hwId, uint8_t inst, uint8_t seg, uint32_t dword)");
    const std::string rr32 = body(bri, "uint32_t Navi48Bringup::regRead32(uint32_t byteOffset) const");
    expect(has(rri, "return regRead32(off);") && !has(rri, "regWrite") && !has(rri, "= v") , "T7 regReadIp only reads");
    expect(has(rr32, "return rmmio[byteOffset / 4];") && count_of(rr32, "=") == 0, "T7 regRead32 is a bare MMIO load");
    bad.clear();
    const std::string thm = body(tool, "static void metrics_print_thm(void)");
    expect(!thm.empty() && has(cm, "metrics_print_thm();"), "T7 navi48test metrics prints the THM registers on every sample");
    const std::vector<std::string> sels = [&] { std::vector<std::string> v; const std::string c = code_only(cm + thm);
        for (size_t p = 0; (p = c.find("kNavi48Sel", p)) != std::string::npos; ) { size_t e = p; while (e < c.size() && (isalnum((unsigned char)c[e]) || c[e] == '_')) e++; v.push_back(c.substr(p, e - p)); p = e; }
        return v; }();
    bool onlyRead = !sels.empty();
    for (const std::string &x : sels) if (x != "kNavi48SelMetrics" && x != "kNavi48SelThermalRegs") { onlyRead = false; bad = x; }
    expect(onlyRead, ("T7 navi48test metrics calls only the Metrics and ThermalRegs selectors (found " + bad + ")").c_str());
    expect(has(thm, "kNavi48SelThermalRegs"), "T7 navi48test metrics reads the THM registers too");
    expect(has(tool, "\"thm     tcon %3d C  ctf %3u C  fan %s rpm  UNVERIFIED (TCON_CUR_TMP 0x%08x CG_MULT_THERMAL_STATUS 0x%08x CG_TACH_CTRL 0x%08x CG_TACH_STATUS 0x%08x)\\n\""),
           "T7 navi48test prints the thm line crit_thermal_watch parses, marked UNVERIFIED with the raw registers");

    std::printf("thermal_verb: %d checks, %d failed\n", gRun, gFail);
    return gFail ? 1 : 0;
}
