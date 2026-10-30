// arm_token_test.cpp - ticket #16 (spec #14): the consume-once NVRAM arming token (amd/arm_token_pure.h).
//   clang++ -std=c++17 -Wall -Wextra -Werror -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
//       -I src/navi48-bringup/src -I src/navi48-bringup/src/amd src/navi48-bringup/tests/arm_token_test.cpp -o /tmp/arm_token && /tmp/arm_token .
//   (run from the repo root; the argument is the repo root the source pins read from; tests/arm_token_plant.sh plants breaks)
// Why: on macOS 26.7.2 the approved kext lives in the Auxiliary Kernel Collection and is matched at EVERY boot, so it must stay
// inert unless armed after login by a token in NVRAM (navi48-arm=stage:N) that probe deletes before it does anything else.
// Covers:
//   A1  no token (absent) is inert;
//   A2  a token probe could not delete is inert, even a valid one (a panic would come back armed);
//   A3  the grammar: exactly "stage:N", N 1..17 (amdgpu::BringupStage::Max), one optional trailing NUL; everything else refused with a reason;
//   A5  the uptime floor: a token seen less than kMinUptimeS after boot (the boot-time match of an AuxKC kext, i.e.
//       the bench was reset while a token was set) is consumed but refused, so even that boot comes back inert;
//   A4  source pins of the REAL probe: it reads navi48-arm from /options, deletes it and re-reads it BEFORE super::probe and the PCI read,
//       decides through n48arm::decide, takes the stage target from the token, no longer reads navi48bringup / navi48-stage, logs inert.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <fstream>
#include <sstream>
#include "amd/arm_token_pure.h"

static int gFail = 0, gRun = 0;
static void expect(bool ok, const char *what) { gRun++; if (!ok) { gFail++; std::printf("FAIL: %s\n", what); } }
static void expect_u(const char *what, uint64_t got, uint64_t want) {
    gRun++;
    if (got != want) { gFail++; std::printf("FAIL: %s: got %llu want %llu\n", what, (unsigned long long)got, (unsigned long long)want); }
}
static std::string slurp(const std::string &path) { std::ifstream f(path, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static size_t count_of(const std::string &s, const std::string &needle) { size_t n = 0, p = 0; while ((p = s.find(needle, p)) != std::string::npos) { n++; p += needle.size(); } return n; }
static bool has(const std::string &s, const std::string &needle) { return s.find(needle) != std::string::npos; }
static std::string between(const std::string &s, const std::string &from, const std::string &to) {
    const size_t a = s.find(from); if (a == std::string::npos) return "";
    const size_t b = s.find(to, a + from.size()); return b == std::string::npos ? s.substr(a) : s.substr(a, b - a);
}

using namespace n48arm;
// decide() on a C string as NVRAM holds it (no NUL), present and consumed unless said otherwise, 10 min after boot.
static const uint64_t kLate = 600;
static Decision tok(const char *s, bool consumed = true, uint64_t up = kLate) {
    return decide(true, reinterpret_cast<const uint8_t *>(s), std::strlen(s), consumed, up);
}
static Decision tok_bytes(const char *s, size_t len) { return decide(true, reinterpret_cast<const uint8_t *>(s), len, true, kLate); }

static void a1_absent() {
    const Decision d = decide(false, nullptr, 0, false, kLate);
    expect(!d.armed, "A1: no token is not armed");
    expect_u("A1: no token reports kAbsent", d.reason, kAbsent);
    expect_u("A1: no token has no stage", d.stage, 0);
    const Decision e = decide(false, reinterpret_cast<const uint8_t *>("stage:9"), 7, true, kLate);
    expect(!e.armed && e.reason == kAbsent, "A1: bytes without 'present' are not a token");
}

static void a2_not_consumed() {
    const Decision d = tok("stage:9", false);
    expect(!d.armed, "A2: a valid token that could not be deleted does not arm");
    expect_u("A2: it reports kNotConsumed", d.reason, kNotConsumed);
    expect_u("A2: and no stage", d.stage, 0);
    expect_u("A2: a malformed undeletable token also reports kNotConsumed", tok("junk", false).reason, kNotConsumed);
}

static void a3_grammar() {
    Decision d = tok("stage:1");
    expect(d.armed && d.reason == kArmed, "A3: stage:1 arms");
    expect_u("A3: stage:1 targets stage 1", d.stage, 1);
    d = tok("stage:9");
    expect(d.armed, "A3: stage:9 arms"); expect_u("A3: stage:9 targets stage 9", d.stage, 9);
    d = tok("stage:17");
    expect(d.armed, "A3: stage:17 (ComputeDispatch) arms"); expect_u("A3: stage:17 targets 17", d.stage, 17);
    d = tok_bytes("stage:5\0", 8);
    expect(d.armed && d.stage == 5, "A3: one trailing NUL is accepted (an OSString/C-string copy)");
    expect_u("A3: the top stage is 17 (ComputeDispatch)", kStageMax, 17);

    expect_u("A3: stage:18 is past the ladder", tok("stage:18").reason, kStageOutOfRange);
    expect_u("A3: stage:0 is refused (no read-only arming)", tok("stage:0").reason, kStageOutOfRange);
    expect_u("A3: stage:99 is refused", tok("stage:99").reason, kStageOutOfRange);
    expect_u("A3: stage:017 (three digits) is malformed", tok("stage:017").reason, kMalformed);
    expect_u("A3: stage:09 (a leading zero) is malformed", tok("stage:09").reason, kMalformed);
    expect_u("A3: an empty value is malformed", tok_bytes("", 0).reason, kMalformed);
    expect_u("A3: a lone NUL is malformed", tok_bytes("\0", 1).reason, kMalformed);
    expect_u("A3: '1' (boot-arg style) is malformed", tok("1").reason, kMalformed);
    expect_u("A3: 'stage:' with no number is malformed", tok("stage:").reason, kMalformed);
    expect_u("A3: 'stage=9' is malformed", tok("stage=9").reason, kMalformed);
    expect_u("A3: 'Stage:9' (case) is malformed", tok("Stage:9").reason, kMalformed);
    expect_u("A3: 'stage:9 ' (trailing space) is malformed", tok("stage:9 ").reason, kMalformed);
    expect_u("A3: 'stage:9\\n' is malformed", tok("stage:9\n").reason, kMalformed);
    expect_u("A3: ' stage:9' is malformed", tok(" stage:9").reason, kMalformed);
    expect_u("A3: 'stage:-1' is malformed", tok("stage:-1").reason, kMalformed);
    expect_u("A3: 'stage:9x' is malformed", tok("stage:9x").reason, kMalformed);
    expect_u("A3: two NULs are malformed", tok_bytes("stage:9\0\0", 9).reason, kMalformed);
    expect_u("A3: a NUL inside is malformed", tok_bytes("stage:\0" "9", 8).reason, kMalformed);
    expect_u("A3: a token longer than any valid one is malformed", tok("stage:9stage:9stage:9stage:9").reason, kMalformed);
    expect_u("A3: a null pointer with a length is malformed", decide(true, nullptr, 7, true, kLate).reason, kMalformed);
    for (const char *bad : { "stage:18", "stage:0", "junk", "stage:09" }) {
        const Decision r = tok(bad);
        expect(!r.armed && r.stage == 0, "A3: a refused token never carries a stage");
    }
    expect(std::strcmp(reason_name(kArmed), "armed") == 0, "A3: kArmed is named armed");
    expect(has(reason_name(kAbsent), "no navi48-arm"), "A3: kAbsent names the missing token");
    expect(has(reason_name(kNotConsumed), "could not be deleted"), "A3: kNotConsumed says the delete failed");
    expect(has(reason_name(kStageOutOfRange), "1..17"), "A3: kStageOutOfRange names the range");
    expect(has(reason_name(kMalformed), "stage:N"), "A3: kMalformed names the grammar");
    expect(std::strcmp(kNvramKey, "navi48-arm") == 0, "A3: the NVRAM variable is navi48-arm");
}

static void a5_uptime_floor() {
    expect_u("A5: the floor is 90 s (boot-time AuxKC matching is well inside it; the harness arms after 120 s)", kMinUptimeS, 90);
    Decision d = tok("stage:1", true, 0);
    expect(!d.armed, "A5: a valid token at boot (uptime 0) does not arm");
    expect_u("A5: it reports kTooEarly", d.reason, kTooEarly);
    expect_u("A5: and no stage", d.stage, 0);
    expect_u("A5: 89 s is still too early", tok("stage:1", true, 89).reason, kTooEarly);
    d = tok("stage:1", true, 90);
    expect(d.armed && d.stage == 1, "A5: 90 s arms");
    expect_u("A5: an undeletable token at boot still reports kNotConsumed first", tok("stage:1", false, 0).reason, kNotConsumed);
    expect_u("A5: absent at boot is kAbsent (the normal boot)", decide(false, nullptr, 0, false, 0).reason, kAbsent);
    expect(has(reason_name(kTooEarly), "90 s"), "A5: kTooEarly names the floor");
}

static void a4_pins(const std::string &root) {
    const std::string K = root + "/src/navi48-bringup/";
    const std::string b = slurp(K + "src/Navi48Bringup.cpp");
    const std::string suites = slurp(root + "/tools/conductor/suites.sh");
    expect(!b.empty(), "A4: Navi48Bringup.cpp is readable (run from the repo root)");
    expect(has(b, "#include \"amd/arm_token_pure.h\""), "A4: Navi48Bringup.cpp includes the arming module");
    expect(has(b, "static_assert(n48arm::kStageMax == static_cast<uint32_t>(amdgpu::BringupStage::Max)"),
           "A4: the kext asserts the token's top stage is the ladder's last (amdgpu_init.h is not host-compilable)");

    const std::string pr = between(b, "IOService *Navi48Bringup::probe(IOService *provider, SInt32 *score) {", "\n}\n");
    const size_t consume = pr.find("consumeArmToken()"), sup = pr.find("super::probe("), cfg = pr.find("configRead32(");
    expect(consume != std::string::npos && sup != std::string::npos && consume < sup, "A4: probe consumes the token before super::probe");
    expect(consume != std::string::npos && cfg != std::string::npos && consume < cfg, "A4: probe consumes the token before it reads PCI config");
    expect(has(pr, "if (!arm.armed) return nullptr;"), "A4: probe returns nullptr unless armed");
    expect(has(pr, "targetStage = arm.stage;"), "A4: the stage target comes from the token");
    expect(!has(pr, "PE_parse_boot_argn(\"navi48bringup\""), "A4: the navi48bringup boot-arg no longer arms");
    expect_u("A4: nothing parses navi48-stage into targetStage any more", count_of(b, "PE_parse_boot_argn(\"navi48-stage\", &targetStage"), 0);

    const std::string ct = between(b, "static n48arm::Decision consumeArmToken() {", "\n}\n");
    expect(!ct.empty(), "A4: consumeArmToken exists");
    const size_t from = ct.find("IORegistryEntry::fromPath(\"/options\", gIODTPlane)"), rd = ct.find("copyProperty(n48arm::kNvramKey)"),
                 rm = ct.find("removeProperty("), again = ct.rfind("copyProperty(n48arm::kNvramKey)"), dec = ct.find("n48arm::decide(");
    expect(from != std::string::npos, "A4: the token is read from /options in gIODTPlane");
    expect(has(ct, "metaCast(\"IODTNVRAM\")"), "A4: /options must be the IODTNVRAM");
    expect(rd != std::string::npos && rm != std::string::npos && rd < rm, "A4: the token is read, then deleted");
    expect(again != std::string::npos && rm < again && again != rd, "A4: after the delete it is read again (consumed = gone)");
    expect(dec != std::string::npos && again < dec, "A4: the decision is made after the consume");
    expect(has(ct, "n48arm::decide(present, buf, len, consumed, uptimeS)"), "A4: decide gets what was read, whether it is gone, and the uptime");
    const size_t mat = ct.find("mach_absolute_time()"), ns = ct.find("absolutetime_to_nanoseconds(");
    expect(mat != std::string::npos && ns != std::string::npos && ns < dec, "A4: the uptime is the kernel's (mach_absolute_time), taken before the decision");
    expect(has(ct, "uptimeS = ns / 1000000000ULL;"), "A4: the uptime is passed in whole seconds");
    expect(has(ct, "N48LOG(\"probe: not armed — inert (%s; arm after login with nvram navi48-arm=stage:N)\""),
           "A4: the no-token boot logs the exact inert line the harness's inert-at-boot looks for");
    expect(has(ct, "token consumed"), "A4: the consume is logged");
    expect(has(suites, "$T/arm_token_test.cpp"), "A4: tools/conductor/suites.sh runs this test");
}

int main(int argc, char **argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    a1_absent(); a2_not_consumed(); a3_grammar(); a5_uptime_floor(); a4_pins(root);
    std::printf("arm_token_test: %d checks, %d failed\n", gRun, gFail);
    return gFail == 0 ? 0 : 1;
}
