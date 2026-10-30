// smu_features_test.cpp - ticket #23 (spec #14): stage 9's minimal SMU feature setup on the Navi 44 path.
//   clang++ -std=c++17 -Wall -Wextra -Werror -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
//       -I src/navi48-bringup/src/amd src/navi48-bringup/tests/smu_features_test.cpp -o /tmp/smu_features && /tmp/smu_features .
//   (run from the repo root; the argument is the repo root the source pins read from)
// The expected values below are upstream's, typed in from the Linux sources (torvalds/linux master d89e1d18),
// never computed from the module under test:
//   bit numbers   drivers/gpu/drm/amd/pm/swsmu/inc/pmfw_if/smu14_driver_if_v14_0.h (FEATURE_*_BIT)
//   message ids   drivers/gpu/drm/amd/pm/swsmu/inc/pmfw_if/smu_v14_0_2_ppsmc.h (PPSMC_MSG_*)
//   order         drivers/gpu/drm/amd/pm/swsmu/amdgpu_smu.c smu_smc_hw_setup; smu_v14_0.c smu_v14_0_set_allowed_mask
//                 (High, then Low); smu_cmn.c smu_cmn_get_enabled_mask (14.0.2 has no GetEnabledSmuFeatures: High,
//                 then Low) and smu_cmn_feature_update_enable_state (EnableSmuFeaturesLow, then High).
// Checks:
//   F1  the minimal allowed-feature mask, bit by bit: in = FW_DATA_READ, DPM_GFXCLK, THROTTLERS, FW_CTF,
//       FAN_CONTROL, MEM_TEMP_READ; out = everything else, in particular every display, memory-clock, fabric,
//       link, power-gating and deep-sleep bit that can disturb the firmware console's scanout.
//   F2  the message plan, through a fake mailbox: RunDcBtc, SetAllowedFeaturesMaskHigh/Low(minimal),
//       EnableAllSmuFeatures, GetRunningSmuFeaturesHigh/Low - and nothing else (no pptable write, no
//       power-source, PCIe, clock or display message).
//   F3  fail closed: no driver table -> no message at all; RunDcBtc refused -> no feature message; an allowed
//       mask PMFW refuses never leads to EnableAllSmuFeatures (that would enable PMFW's own, full set), only to
//       EnableSmuFeaturesLow/High with the same minimal mask; a refused enable stops there.
//   F4  the verdict: GFXCLK DPM running or not, and any running feature outside the minimal mask.
//   F5  the kext uses the plan: stage 9 on the default (Navi 44) path places the driver table (the #18
//       sentinel + probe) and then runs the minimal plan through the real mailbox; navi48-smu-min=0 keeps the
//       old driver-table-only stage; the plan's ids are PPSMC's.
//   F6  the Navi 48 / navi48-smu-full=1 path is unchanged: smu_smc_hw_setup still sends its messages in the
//       old order with the all-ones mask, and smuFullSetup is still chosen the old way.
#include "smu_features_pure.h"
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int gFail = 0, gRun = 0;
static void expect(bool ok, const std::string &what) { gRun++; if (!ok) { gFail++; std::printf("FAIL: %s\n", what.c_str()); } }

static std::string slurp(const std::string &path) { std::ifstream f(path, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static bool has(const std::string &s, const std::string &needle) { return s.find(needle) != std::string::npos; }
static size_t count_of(const std::string &s, const std::string &needle) { size_t n = 0, p = 0; while ((p = s.find(needle, p)) != std::string::npos) { n++; p += needle.size(); } return n; }
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
// The positions of `needles` in `s`, each searched after the previous one: true when all are found in that order.
static bool in_order(const std::string &s, const std::vector<std::string> &needles) {
    size_t p = 0;
    for (const std::string &n : needles) { p = s.find(n, p); if (p == std::string::npos) return false; p += n.size(); }
    return true;
}

// ---- the fake mailbox -------------------------------------------------------------------------------------------------
struct Msg { uint32_t id, param; };
struct FakeSmu {
    std::vector<Msg> sent;
    std::vector<uint32_t> refuse;        // message ids PMFW answers with something other than OK
    uint32_t runningLo = 0, runningHi = 0;
    bool operator()(uint32_t id, uint32_t param, uint32_t *out) {
        sent.push_back({id, param});
        for (uint32_t r : refuse) if (r == id) return false;
        if (out) *out = (id == 0x0C) ? runningLo : (id == 0x0D) ? runningHi : 0;
        return true;
    }
};
static bool same(const std::vector<Msg> &got, const std::vector<Msg> &want) {
    if (got.size() != want.size()) return false;
    for (size_t i = 0; i < got.size(); i++) if (got[i].id != want[i].id || got[i].param != want[i].param) return false;
    return true;
}
static std::string show(const std::vector<Msg> &v) {
    std::string s; char b[32];
    for (const Msg &m : v) { std::snprintf(b, sizeof b, " 0x%02x(0x%08x)", m.id, m.param); s += b; }
    return s;
}
static bool sent_id(const std::vector<Msg> &v, uint32_t id) { for (const Msg &m : v) if (m.id == id) return true; return false; }

// Upstream PPSMC ids (smu_v14_0_2_ppsmc.h).
constexpr uint32_t kSetAllowedLo = 0x4, kSetAllowedHi = 0x5, kEnableAll = 0x6, kEnableLo = 0x8, kEnableHi = 0x9,
                   kRunningLo = 0xC, kRunningHi = 0xD, kRunDcBtc = 0x36;
// The minimal mask, typed in: bits 0, 1, 23, 28, 29 low and 46 (= high bit 14).
constexpr uint32_t kMinLo = 0x30800003u, kMinHi = 0x00004000u;

static void f1_mask() {
    using namespace smu14feat;
    expect(kMinimalAllowedMask == ((uint64_t)kMinHi << 32 | kMinLo), "F1 the minimal mask is 0x00004000_0x30800003");
    expect(mask_lo(kMinimalAllowedMask) == kMinLo && mask_hi(kMinimalAllowedMask) == kMinHi, "F1 its 32-bit halves");
    struct Bit { int n; const char *name; bool in; };
    const Bit bits[] = {
        {0, "FW_DATA_READ", true}, {1, "DPM_GFXCLK", true}, {2, "DPM_GFX_POWER_OPTIMIZER", false}, {3, "DPM_UCLK", false},
        {4, "DPM_FCLK", false}, {5, "DPM_SOCCLK", false}, {6, "DPM_LINK", false}, {7, "DPM_DCN", false},
        {8, "VMEMP_SCALING", false}, {9, "VDDIO_MEM_SCALING", false}, {10, "DS_GFXCLK", false}, {11, "DS_SOCCLK", false},
        {12, "DS_FCLK", false}, {13, "DS_LCLK", false}, {14, "DS_DCFCLK", false}, {15, "DS_UCLK", false},
        {16, "GFX_ULV", false}, {17, "FW_DSTATE", false}, {18, "GFXOFF", false}, {19, "BACO", false},
        {20, "MM_DPM", false}, {21, "SOC_MPCLK_DS", false}, {22, "BACO_MPCLK_DS", false}, {23, "THROTTLERS", true},
        {24, "SMARTSHIFT", false}, {25, "GTHR", false}, {26, "ACDC", false}, {27, "VR0HOT", false},
        {28, "FW_CTF", true}, {29, "FAN_CONTROL", true}, {30, "GFX_DCS", false}, {31, "GFX_READ_MARGIN", false},
        {32, "LED_DISPLAY", false}, {33, "GFXCLK_SPREAD_SPECTRUM", false}, {34, "OUT_OF_BAND_MONITOR", false},
        {35, "OPTIMIZED_VMIN", false}, {36, "GFX_IMU", false}, {37, "BOOT_TIME_CAL", false}, {38, "GFX_PCC_DFLL", false},
        {39, "SOC_CG", false}, {40, "DF_CSTATE", false}, {41, "GFX_EDC", false}, {42, "BOOT_POWER_OPT", false},
        {43, "CLOCK_POWER_DOWN_BYPASS", false}, {44, "DS_VCN", false}, {45, "BACO_CG", false}, {46, "MEM_TEMP_READ", true},
        {47, "ATHUB_MMHUB_PG", false}, {48, "SOC_PCC", false}, {49, "EDC_PWRBRK", false}, {50, "SOC_EDC_XVMIN", false},
        {51, "GFX_PSM_DIDT", false}, {52, "APT_ALL_ENABLE", false}, {53, "APT_SQ_THROTTLE", false}, {54, "APT_PF_DCS", false},
        {55, "GFX_EDC_XVMIN", false}, {56, "GFX_DIDT_XVMIN", false}, {57, "FAN_ABNORMAL", false},
        {58, "CLOCK_STRETCH_COMPENSATOR", false}, {59, "SPARE_59", false}, {60, "SPARE_60", false}, {61, "SPARE_61", false},
        {62, "SPARE_62", false}, {63, "SPARE_63", false},
    };
    for (const Bit &b : bits)
        expect(((kMinimalAllowedMask >> b.n) & 1) == (b.in ? 1u : 0u), std::string("F1 bit ") + std::to_string(b.n) + " " + b.name + (b.in ? " is allowed" : " is left off"));
    // The named constants are upstream's bit numbers.
    expect(kFeatureFwDataRead == 0 && kFeatureDpmGfxclk == 1 && kFeatureDpmUclk == 3 && kFeatureDpmFclk == 4 && kFeatureDpmSocclk == 5 &&
           kFeatureDpmLink == 6 && kFeatureDpmDcn == 7 && kFeatureDsDcfclk == 14 && kFeatureGfxoff == 18 && kFeatureThrottlers == 23 &&
           kFeatureFwCtf == 28 && kFeatureFanControl == 29 && kFeatureDfCstate == 40 && kFeatureMemTempRead == 46 && kFeatureAthubMmhubPg == 47,
           "F1 the feature bit constants match smu14_driver_if_v14_0.h");
}

static void f2_plan() {
    FakeSmu smu; smu.runningLo = kMinLo; smu.runningHi = kMinHi;
    const smu14feat::MinimalSetupResult r = smu14feat::run_minimal_setup(true, smu);
    const std::vector<Msg> want = {{kRunDcBtc, 0}, {kSetAllowedHi, kMinHi}, {kSetAllowedLo, kMinLo}, {kEnableAll, 0}, {kRunningHi, 0}, {kRunningLo, 0}};
    expect(same(smu.sent, want), "F2 the plan is RunDcBtc, SetAllowedFeaturesMaskHigh/Low(minimal), EnableAllSmuFeatures, GetRunningSmuFeaturesHigh/Low; got" + show(smu.sent));
    expect(r.outcome == smu14feat::Outcome::Enabled && r.path == smu14feat::EnablePath::AllowedMask, "F2 ... and reports features enabled through the allowed mask");
    expect(r.runningRead && r.running == ((uint64_t)kMinHi << 32 | kMinLo), "F2 ... with the running features PMFW reported");
}

static void f3_fail_closed() {
    {   FakeSmu smu;
        const smu14feat::MinimalSetupResult r = smu14feat::run_minimal_setup(false, smu);
        expect(smu.sent.empty() && r.outcome == smu14feat::Outcome::NoDriverTable, "F3 no driver table: no message at all; got" + show(smu.sent)); }
    {   FakeSmu smu; smu.refuse = {kRunDcBtc};
        const smu14feat::MinimalSetupResult r = smu14feat::run_minimal_setup(true, smu);
        expect(same(smu.sent, {{kRunDcBtc, 0}}) && r.outcome == smu14feat::Outcome::BtcFailed, "F3 RunDcBtc refused: no feature message; got" + show(smu.sent)); }
    {   FakeSmu smu; smu.refuse = {kSetAllowedHi}; smu.runningLo = kMinLo; smu.runningHi = kMinHi;
        const smu14feat::MinimalSetupResult r = smu14feat::run_minimal_setup(true, smu);
        const std::vector<Msg> want = {{kRunDcBtc, 0}, {kSetAllowedHi, kMinHi}, {kEnableLo, kMinLo}, {kEnableHi, kMinHi}, {kRunningHi, 0}, {kRunningLo, 0}};
        expect(same(smu.sent, want), "F3 SetAllowedFeaturesMaskHigh refused: EnableSmuFeaturesLow/High(minimal) instead, no Low mask; got" + show(smu.sent));
        expect(r.outcome == smu14feat::Outcome::Enabled && r.path == smu14feat::EnablePath::SelectiveEnable, "F3 ... reported as enabled selectively"); }
    {   FakeSmu smu; smu.refuse = {kSetAllowedLo};
        smu14feat::run_minimal_setup(true, smu);
        const std::vector<Msg> want = {{kRunDcBtc, 0}, {kSetAllowedHi, kMinHi}, {kSetAllowedLo, kMinLo}, {kEnableLo, kMinLo}, {kEnableHi, kMinHi}, {kRunningHi, 0}, {kRunningLo, 0}};
        expect(same(smu.sent, want), "F3 SetAllowedFeaturesMaskLow refused: EnableSmuFeaturesLow/High(minimal); got" + show(smu.sent)); }
    {   FakeSmu smu; smu.refuse = {kSetAllowedHi, kSetAllowedLo, kEnableLo};
        const smu14feat::MinimalSetupResult r = smu14feat::run_minimal_setup(true, smu);
        expect(!sent_id(smu.sent, kEnableHi) && r.outcome == smu14feat::Outcome::EnableFailed, "F3 EnableSmuFeaturesLow refused: no High, reported failed; got" + show(smu.sent)); }
    {   FakeSmu smu; smu.refuse = {kEnableAll};
        const smu14feat::MinimalSetupResult r = smu14feat::run_minimal_setup(true, smu);
        expect(r.outcome == smu14feat::Outcome::EnableFailed && !sent_id(smu.sent, kEnableLo) && !sent_id(smu.sent, kEnableHi),
               "F3 EnableAllSmuFeatures refused: reported failed, nothing else enabled; got" + show(smu.sent)); }
    // Whatever PMFW answers, the plan never sends EnableAllSmuFeatures without an accepted minimal mask.
    for (int m = 0; m < 64; m++) {
        FakeSmu smu;
        const uint32_t ids[] = {kRunDcBtc, kSetAllowedHi, kSetAllowedLo, kEnableAll, kEnableLo, kEnableHi};
        for (int i = 0; i < 6; i++) if (m & (1 << i)) smu.refuse.push_back(ids[i]);
        smu14feat::run_minimal_setup(true, smu);
        size_t ea = smu.sent.size(), hi = smu.sent.size(), lo = smu.sent.size();
        for (size_t i = 0; i < smu.sent.size(); i++) {
            if (smu.sent[i].id == kEnableAll && ea == smu.sent.size()) ea = i;
            if (smu.sent[i].id == kSetAllowedHi && hi == smu.sent.size()) hi = i;
            if (smu.sent[i].id == kSetAllowedLo && lo == smu.sent.size()) lo = i;
        }
        const bool maskOk = hi < ea && lo < ea && !(m & 2) && !(m & 4);
        expect(ea == smu.sent.size() || maskOk, "F3 refusal set " + std::to_string(m) + ": EnableAllSmuFeatures only after an accepted minimal mask; got" + show(smu.sent));
        for (const Msg &x : smu.sent)
            expect(x.id == kRunDcBtc || x.id == kSetAllowedHi || x.id == kSetAllowedLo || x.id == kEnableAll || x.id == kEnableLo ||
                   x.id == kEnableHi || x.id == kRunningHi || x.id == kRunningLo,
                   "F3 refusal set " + std::to_string(m) + ": only the plan's messages; got" + show(smu.sent));
        for (const Msg &x : smu.sent)
            if (x.id == kSetAllowedHi || x.id == kEnableHi) expect(x.param == kMinHi, "F3 every high mask is the minimal one");
            else if (x.id == kSetAllowedLo || x.id == kEnableLo) expect(x.param == kMinLo, "F3 every low mask is the minimal one");
    }
}

static void f4_verdict() {
    {   FakeSmu smu; smu.runningLo = kMinLo; smu.runningHi = kMinHi;
        const smu14feat::MinimalSetupResult r = smu14feat::run_minimal_setup(true, smu);
        expect(smu14feat::gfxclk_dpm_running(r) && smu14feat::outside_mask(r) == 0, "F4 all minimal features running: GFXCLK DPM on, nothing outside the mask"); }
    {   FakeSmu smu; smu.runningLo = 0x30800001u; smu.runningHi = 0;   // FW_DATA_READ, THROTTLERS, FW_CTF, FAN_CONTROL but no GFXCLK DPM
        const smu14feat::MinimalSetupResult r = smu14feat::run_minimal_setup(true, smu);
        expect(!smu14feat::gfxclk_dpm_running(r), "F4 bit 1 clear: GFXCLK DPM reported off"); }
    {   FakeSmu smu; smu.runningLo = kMinLo | 0x88u; smu.runningHi = kMinHi | 0x8000u;  // + DPM_UCLK, DPM_DCN, ATHUB_MMHUB_PG
        const smu14feat::MinimalSetupResult r = smu14feat::run_minimal_setup(true, smu);
        expect(smu14feat::outside_mask(r) == ((uint64_t)0x8000u << 32 | 0x88u), "F4 running features outside the minimal mask are reported"); }
    {   FakeSmu smu; smu.refuse = {kRunningHi}; smu.runningLo = kMinLo;
        const smu14feat::MinimalSetupResult r = smu14feat::run_minimal_setup(true, smu);
        expect(!r.runningRead && !smu14feat::gfxclk_dpm_running(r), "F4 an unreadable running mask never reads as GFXCLK DPM on"); }
}

static void f5_kext(const std::string &root) {
    const std::string K = root + "/src/navi48-bringup/";
    const std::string smu = slurp(K + "src/amd/smu_v14_0.cpp"), hdr = slurp(K + "src/amd/amdgpu_smu.h"), ip = slurp(K + "src/amd/amdgpu_ip.h");
    const std::string init = slurp(K + "src/amd/amdgpu_init.cpp"), inith = slurp(K + "src/amd/amdgpu_init.h"), b = slurp(K + "src/Navi48Bringup.cpp");
    const std::string lib = slurp(root + "/tools/acceptance/lib.sh"), suites = slurp(root + "/tools/conductor/suites.sh");
    expect(!smu.empty() && !hdr.empty() && !ip.empty() && !init.empty() && !b.empty() && !lib.empty(), "F5 sources readable (run from the repo root)");

    const std::string hw = code_only(body(smu, "smu_hw_init(DeviceContext &dev, bool runSmcHwSetup, bool minimalFeatures)"));
    expect(!hw.empty(), "F5 smu_hw_init(dev, runSmcHwSetup, minimalFeatures) found");
    const std::string skip = body(hw.substr(hw.find("if (!runSmcHwSetup)") == std::string::npos ? hw.size() : hw.find("if (!runSmcHwSetup)")), "if (!runSmcHwSetup)");
    expect(in_order(skip, {"smu_set_driver_table_location(dev)", "if (minimalFeatures)", "smu_minimal_hw_setup(dev)"}),
           "F5 the non-full path places the driver table (sentinel + probe) first, then runs the minimal setup when asked");
    expect(count_of(hw, "smu_minimal_hw_setup(") == 1 && count_of(hw, "smu_smc_hw_setup(") == 1, "F5 smu_hw_init runs either the minimal or the full setup, each once");
    const std::string mh = code_only(body(smu, "smu_minimal_hw_setup(DeviceContext &dev)"));
    expect(has(mh, "smu14feat::run_minimal_setup(dev.smuDriverTable, ") && has(mh, "smu_send_msg_with_param(dev, "),
           "F5 smu_minimal_hw_setup runs the plan through the real mailbox, gated on the placed driver table");
    for (const char *m : {"PPSMC::", "SetSoft", "UseDefaultPPTable", "NotifyPowerSource", "TransferTableDram2Smu", "smu_transfer_table", "WREG32", "bar0_memset_vram"})
        expect(!has(mh, m), std::string("F5 smu_minimal_hw_setup sends nothing of its own: no ") + m);
    expect(has(smu, "#include \"smu_features_pure.h\""), "F5 smu_v14_0.cpp uses the pure plan");
    for (const char *a : {"smu14feat::kMsgRunDcBtc == PPSMC::RunDcBtc", "smu14feat::kMsgSetAllowedFeaturesMaskLow == PPSMC::SetAllowedFeaturesMaskLow",
                          "smu14feat::kMsgSetAllowedFeaturesMaskHigh == PPSMC::SetAllowedFeaturesMaskHigh", "smu14feat::kMsgEnableAllSmuFeatures == PPSMC::EnableAllSmuFeatures",
                          "smu14feat::kMsgEnableSmuFeaturesLow == PPSMC::EnableSmuFeaturesLow", "smu14feat::kMsgEnableSmuFeaturesHigh == PPSMC::EnableSmuFeaturesHigh",
                          "smu14feat::kMsgGetRunningSmuFeaturesLow == PPSMC::GetRunningSmuFeaturesLow", "smu14feat::kMsgGetRunningSmuFeaturesHigh == PPSMC::GetRunningSmuFeaturesHigh"})
        expect(has(smu, std::string("static_assert(") + a), std::string("F5 static_assert(") + a + ")");
    expect(has(ip, "constexpr uint32_t EnableSmuFeaturesLow       = 0x08;") && has(ip, "constexpr uint32_t EnableSmuFeaturesHigh      = 0x09;"),
           "F5 PPSMC::EnableSmuFeaturesLow/High are 0x8/0x9 (smu_v14_0_2_ppsmc.h)");
    expect(has(smu, "case PPSMC::EnableSmuFeaturesLow:") && has(smu, "case PPSMC::EnableSmuFeaturesHigh:"), "F5 the mailbox log names the selective-enable messages");
    // The log lines the bench is read by.
    expect(has(mh, "SMU_LOG(") && has(body(smu, "smu_minimal_hw_setup(DeviceContext &dev)"), "smc_min: running features = 0x%08x_0x%08x"), "F5 the minimal setup logs the running features");
    expect(has(body(smu, "smu_minimal_hw_setup(DeviceContext &dev)"), "smc_min: GFXCLK DPM %s"), "F5 ... and whether GFXCLK DPM runs");

    // Stage 9 wiring.
    expect(has(inith, "bool smuMinimalFeatures { true };"), "F5 BringupContext::smuMinimalFeatures defaults on");
    expect(has(init, "smu_hw_init(dev, ctx.smuFullSetup, ctx.smuMinimalFeatures)"), "F5 SMUInit passes the full and minimal choices to smu_hw_init");
    expect(has(b, "PE_parse_boot_argn(\"navi48-smu-min\", &smuMin, sizeof(smuMin));") && has(b, "ctx.smuMinimalFeatures = (smuMin != 0);") && has(b, "uint32_t smuMin = 1;"),
           "F5 navi48-smu-min (default 1) sets smuMinimalFeatures");
    expect(has(lib, " navi48-smu-min"), "F5 the acceptance harness allows the navi48-smu-min ladder boot-arg");
    expect(has(hdr, "kern_return_t smu_hw_init(DeviceContext &dev, bool runSmcHwSetup, bool minimalFeatures);"), "F5 smu_hw_init's declaration has no defaults");
    expect(has(suites, "run smu_features "), "F5 the suite runner lists this suite");
}

static void f6_navi48(const std::string &root) {
    const std::string K = root + "/src/navi48-bringup/";
    const std::string smu = slurp(K + "src/amd/smu_v14_0.cpp"), b = slurp(K + "src/Navi48Bringup.cpp");
    const std::string hs = code_only(body(smu, "smu_smc_hw_setup(DeviceContext &dev, uint64_t driver_table_mc)"));
    expect(!hs.empty(), "F6 smu_smc_hw_setup found");
    expect(in_order(hs, {"smu_get_driver_if_version(dev, &if_ver)", "PPSMC::SetDriverDramAddrHigh, hi", "PPSMC::SetDriverDramAddrLow, lo",
                         "smu_send_msg(dev, PPSMC::UseDefaultPPTable)", "smu_send_msg(dev, PPSMC::RunDcBtc)",
                         "PPSMC::NotifyPowerSource,", "PPSMC::SetAllowedFeaturesMaskLow,", "0xFFFFFFFFu", "PPSMC::SetAllowedFeaturesMaskHigh,", "0xFFFFFFFFu",
                         "smu_send_msg(dev, PPSMC::EnableAllSmuFeatures)", "PPSMC::GetRunningSmuFeaturesLow,", "PPSMC::GetRunningSmuFeaturesHigh,"}),
           "F6 smu_smc_hw_setup keeps the Navi 48 order: IF version, driver table, UseDefaultPPTable, RunDcBtc, NotifyPowerSource(1), all-ones mask Low/High, EnableAll, running Low/High");
    expect(has(body(smu, "smu_smc_hw_setup(DeviceContext &dev, uint64_t driver_table_mc)"), "PPSMC::NotifyPowerSource, /*AC*/ 1, nullptr"), "F6 NotifyPowerSource still says AC = 1");
    expect(count_of(hs, "smu_send_msg") == 10 && !has(hs, "smu14feat") && !has(hs, "EnableSmuFeatures"), "F6 ... with no other message and no minimal mask");
    const std::string hw = code_only(body(smu, "smu_hw_init(DeviceContext &dev, bool runSmcHwSetup, bool minimalFeatures)"));
    const size_t full = hw.find("smu_smc_hw_setup(dev,");
    expect(full != std::string::npos && full > hw.find("if (!runSmcHwSetup)") && full > hw.find("return kIOReturnSuccess;"),
           "F6 runSmcHwSetup (navi48-smu-full=1 / stage >= RLCInit) still runs smu_smc_hw_setup(dev, 0) after the non-full branch returns");
    expect(has(b, "ctx.smuFullSetup = (PE_parse_boot_argn(\"navi48-smu-full\", &full, sizeof(full)) && full != 0) ||\n"
                  "\t                   (target >= (uint32_t)amdgpu::BringupStage::RLCInit &&\n"
                  "\t                    !(PE_parse_boot_argn(\"navi48-smu-basic\", &basic, sizeof(basic)) && basic != 0));"),
           "F6 smuFullSetup is chosen as before (navi48-smu-full=1, or a target >= RLCInit without navi48-smu-basic=1)");
}

int main(int argc, char **argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    f1_mask(); f2_plan(); f3_fail_closed(); f4_verdict(); f5_kext(root); f6_navi48(root);
    std::printf("smu_features: %d checks, %d failed\n", gRun, gFail);
    return gFail ? 1 : 0;
}
