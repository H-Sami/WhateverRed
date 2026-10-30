// navi44_identity_test.cpp - ticket #16 (spec #14): the Navi 44 identity module (amd/navi44_identity_pure.h).
//   clang++ -std=c++17 -Wall -Wextra -Werror -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
//       -I src/navi48-bringup/src -I src/navi48-bringup/src/amd src/navi48-bringup/tests/navi44_identity_test.cpp -o /tmp/navi44_identity && /tmp/navi44_identity .
//   (run from the repo root; the argument is the repo root the source pins read from; tests/navi44_identity_plant.sh plants breaks)
// Covers:
//   I1  device ID: 1002:7590 is accepted; Navi 48 (7550 / 7551), a neighbour (7591), another vendor and a byte-swapped word are refused;
//   I2  IP versions: the discovery table this card reports (ground truth, map #1 Notes / ticket #6) and the amdgpu_ip.h pins that consume them;
//   I3  gc_info: a hand-built IP discovery binary (v1.3 header, GC table v1.3) parses to 2 SE / 2 SA per SE / 8 CU per SA / 8 RB;
//       v2 binary header and GC v2 layouts; a missing, truncated, mis-identified or mis-checksummed GC table is refused with its reason;
//   I4  geometry check: the exact 2/2/8/8 is accepted; each field one above and one below is refused with that field's reason;
//   I5  source pins of the REAL code: probe, Info.plist, amdgpu_ip.h and the stage-1 path consume this module.
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include "amd/navi44_identity_pure.h"
#include "amd/amdgpu_ip.h"

static int gFail = 0, gRun = 0;
static void expect(bool ok, const char *what) { gRun++; if (!ok) { gFail++; std::printf("FAIL: %s\n", what); } }
static void expect_u(const char *what, uint64_t got, uint64_t want) {
    gRun++;
    if (got != want) { gFail++; std::printf("FAIL: %s: got %#llx want %#llx\n", what, (unsigned long long)got, (unsigned long long)want); }
}

static std::string slurp(const std::string &path) { std::ifstream f(path, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); return ss.str(); }
static size_t count_of(const std::string &s, const std::string &needle) { size_t n = 0, p = 0; while ((p = s.find(needle, p)) != std::string::npos) { n++; p += needle.size(); } return n; }
static bool has(const std::string &s, const std::string &needle) { return s.find(needle) != std::string::npos; }
// From the first occurrence of `from` to the next occurrence of `to` after it ("" when `from` is absent).
static std::string between(const std::string &s, const std::string &from, const std::string &to) {
    const size_t a = s.find(from); if (a == std::string::npos) return "";
    const size_t b = s.find(to, a + from.size()); return b == std::string::npos ? s.substr(a) : s.substr(a, b - a);
}
// The IOPCIPrimaryMatch string of one Info.plist personality.
static std::string primary_match(const std::string &plist, const std::string &personality) {
    const std::string d = between(plist, "<key>" + personality + "</key>", "</dict>");
    const std::string m = between(d, "<key>IOPCIPrimaryMatch</key>", "</string>");
    const size_t a = m.find("<string>");
    return a == std::string::npos ? "" : m.substr(a + 8);
}

static void i1_device() {
    expect(n44id::accepts_device(0x75901002u), "I1: 1002:7590 (RX 9060 XT) is accepted");
    expect(!n44id::accepts_device(0x75501002u), "I1: 1002:7550 (Navi 48 RX 9070) is refused");
    expect(!n44id::accepts_device(0x75511002u), "I1: 1002:7551 (Navi 48 R9700) is refused");
    expect(!n44id::accepts_device(0x75911002u), "I1: 1002:7591 (a neighbour ID) is refused");
    expect(!n44id::accepts_device(0x75901022u), "I1: device 7590 from another vendor (1022) is refused");
    expect(!n44id::accepts_device(0x10027590u), "I1: the byte-swapped word 7590:1002 is refused");
    expect(!n44id::accepts_device(0xffffffffu), "I1: an absent device (all ones) is refused");
    expect(std::strcmp(n44id::kPciPrimaryMatch, "0x75901002") == 0, "I1: the IOPCIPrimaryMatch string is 0x75901002");
}

static void ver(const char *what, const n44id::IpVer *v, unsigned major, unsigned minor, unsigned rev) {
    gRun++;
    if (!v || v->major != major || v->minor != minor || v->rev != rev) {
        gFail++;
        if (v) std::printf("FAIL: %s: got %u.%u.%u want %u.%u.%u\n", what, v->major, v->minor, v->rev, major, minor, rev);
        else   std::printf("FAIL: %s: not pinned\n", what);
    }
}
static void pin(const char *what, const amdgpu::IPVersion &v, unsigned major, unsigned minor, unsigned rev) {
    gRun++;
    if (v.major != major || v.minor != minor || v.rev != rev) {
        gFail++; std::printf("FAIL: %s: got %u.%u.%u want %u.%u.%u\n", what, v.major, v.minor, v.rev, major, minor, rev);
    }
}

static void i2_versions() {
    // The discovery table on this card (ticket #6 capture), by hardware ID.
    ver("I2: GC (hw 11) is 12.0.0",        n44id::expected_ip(11),  12, 0, 0);
    ver("I2: MP0 / PSP (hw 255) is 14.0.2", n44id::expected_ip(255), 14, 0, 2);
    ver("I2: MP1 / SMU (hw 1) is 14.0.2",   n44id::expected_ip(1),   14, 0, 2);
    ver("I2: SDMA0 (hw 42) is 7.0.0",       n44id::expected_ip(42),   7, 0, 0);
    ver("I2: OSSSYS / IH (hw 40) is 7.0.0", n44id::expected_ip(40),   7, 0, 0);
    ver("I2: HDP (hw 41) is 7.0.0",         n44id::expected_ip(41),   7, 0, 0);
    ver("I2: NBIF (hw 108) is 6.3.1",       n44id::expected_ip(108),  6, 3, 1);
    ver("I2: MMHUB (hw 34) is 4.1.0",       n44id::expected_ip(34),   4, 1, 0);
    ver("I2: DMU / DCN (hw 271) is 4.1.0",  n44id::expected_ip(271),  4, 1, 0);
    ver("I2: UMC (hw 150) is 8.14.0",       n44id::expected_ip(150),  8, 14, 0);
    expect(n44id::expected_ip(12) == nullptr, "I2: an unpinned hardware ID (12) has no expected version");
    expect(n44id::same_ver(n44id::kGc, 12, 0, 0) && !n44id::same_ver(n44id::kGc, 12, 0, 1), "I2: same_ver tells 12.0.0 from Navi 48's 12.0.1");
    // The compile-time pins the PSP / GFX / SDMA code reads (amdgpu_ip.h): Navi 44's, not Navi 48's.
    pin("I2: kIP_GFX is 12.0.0 (Navi 48: 12.0.1)",  amdgpu::kIP_GFX,  12, 0, 0);
    pin("I2: kIP_GMC is 12.0.0",                    amdgpu::kIP_GMC,  12, 0, 0);
    pin("I2: kIP_SDMA is 7.0.0 (Navi 48: 7.0.1)",   amdgpu::kIP_SDMA,  7, 0, 0);
    pin("I2: kIP_PSP is 14.0.2 (Navi 48: 14.0.3)",  amdgpu::kIP_PSP,  14, 0, 2);
    pin("I2: kIP_SMU is 14.0.2 (Navi 48: 14.0.3)",  amdgpu::kIP_SMU,  14, 0, 2);
    pin("I2: kIP_MES is 12.0.0 (Navi 48: 12.0.1)",  amdgpu::kIP_MES,  12, 0, 0);
    pin("I2: kIP_NBIO is 6.3.1 (Navi 48: 7.11.0)",  amdgpu::kIP_NBIO,  6, 3, 1);
    pin("I2: kIP_IH is 7.0.0",                      amdgpu::kIP_IH,    7, 0, 0);
    pin("I2: kIP_HDP is 7.0.0",                     amdgpu::kIP_HDP,   7, 0, 0);
}

// ---------------------------------------------------------------------------
// A hand-built IP discovery binary: only the fields the GC parse reads. Layout (Linux discovery.h):
//   binary_header: u32 signature 0x28211407, u16 version_major, u16 version_minor, u16 checksum, u16 size,
//                  table_info[] at +12 (v1) / +16 (v2): u16 offset, u16 checksum, u16 size, u16 padding; [0] IP_DISCOVERY, [1] GC.
//   GC table:      u32 table_id 0x4347 ("GC"), u16 version_major, u16 version_minor, u32 size, then u32 fields.
static void put16(std::vector<uint8_t> &b, size_t o, uint16_t v) { b[o] = v & 0xff; b[o + 1] = v >> 8; }
static void put32(std::vector<uint8_t> &b, size_t o, uint32_t v) { for (int i = 0; i < 4; i++) b[o + i] = (v >> (8 * i)) & 0xff; }
static uint16_t sum16(const std::vector<uint8_t> &b, size_t o, size_t n) { uint16_t s = 0; for (size_t i = 0; i < n; i++) s = uint16_t(s + b[o + i]); return s; }

static constexpr size_t kGcAt = 0x3e8;        // where this card's binary keeps its GC table
// gc_info v1.x as this card reports it: 2 SE, wgp0 4 / wgp1 0 per SA, 4 RB per SE, 16 GL2C, 2 SA per SE.
static std::vector<uint8_t> blob_v1(uint16_t binMajor = 1, uint16_t gcMinor = 3, uint32_t se = 2, uint32_t wgp0 = 4, uint32_t wgp1 = 0,
                                    uint32_t rbPerSe = 4, uint32_t saPerSe = 2) {
    const uint32_t gcSize = 164;
    std::vector<uint8_t> b(kGcAt + gcSize + 16, 0);
    put32(b, 0, 0x28211407u); put16(b, 4, binMajor); put16(b, 6, 3); put16(b, 10, uint16_t(b.size()));
    const size_t tl = binMajor >= 2 ? 16 : 12;
    put16(b, tl + 8 + 0, uint16_t(kGcAt)); put16(b, tl + 8 + 4, uint16_t(gcSize));
    put32(b, kGcAt + 0, 0x4347u); put16(b, kGcAt + 4, 1); put16(b, kGcAt + 6, gcMinor); put32(b, kGcAt + 8, gcSize);
    put32(b, kGcAt + 12, se); put32(b, kGcAt + 16, wgp0); put32(b, kGcAt + 20, wgp1); put32(b, kGcAt + 24, rbPerSe);
    put32(b, kGcAt + 28, 16);                   // gl2c
    put32(b, kGcAt + 56, 32);                   // wave size
    put32(b, kGcAt + 72, 1);                    // sc per se
    put32(b, kGcAt + 76, saPerSe);
    put16(b, tl + 8 + 2, sum16(b, kGcAt, gcSize));
    put16(b, 8, sum16(b, 10, b.size() - 10));
    return b;
}
// gc_info v2.x: se, cu_per_sh, sh_per_se, rb_per_se.
static std::vector<uint8_t> blob_gc_v2(uint32_t se, uint32_t cuPerSh, uint32_t shPerSe, uint32_t rbPerSe) {
    const uint32_t gcSize = 96;
    std::vector<uint8_t> b(0x200 + gcSize, 0);
    put32(b, 0, 0x28211407u); put16(b, 4, 1); put16(b, 6, 1); put16(b, 10, uint16_t(b.size()));
    put16(b, 12 + 8, 0x200); put16(b, 12 + 12, uint16_t(gcSize));
    put32(b, 0x200, 0x4347u); put16(b, 0x204, 2); put16(b, 0x206, 0); put32(b, 0x208, gcSize);
    put32(b, 0x200 + 12, se); put32(b, 0x200 + 16, cuPerSh); put32(b, 0x200 + 20, shPerSe); put32(b, 0x200 + 24, rbPerSe);
    put16(b, 12 + 10, sum16(b, 0x200, gcSize));
    return b;
}
static void reseal_gc(std::vector<uint8_t> &b, size_t tl = 12) {
    const size_t at = size_t(b[tl + 8]) | size_t(b[tl + 9]) << 8;
    const uint32_t sz = uint32_t(b[at + 8]) | uint32_t(b[at + 9]) << 8;
    put16(b, tl + 10, sum16(b, at, sz));
}

static void i3_gc_info() {
    using namespace n44id;
    {
        const std::vector<uint8_t> b = blob_v1();
        const GcInfo g = parse_gc_info(b.data(), b.size());
        expect(g.reason == kOk, "I3: this card's v1.3 binary with a GC v1.3 table parses");
        expect_u("I3: GC table version major", g.verMajor, 1); expect_u("I3: GC table version minor", g.verMinor, 3);
        expect_u("I3: 2 shader engines", g.geo.se, 2);
        expect_u("I3: 2 shader arrays per SE (gc_num_sa_per_se)", g.geo.saPerSe, 2);
        expect_u("I3: 8 CU per SA (2 x (wgp0 4 + wgp1 0))", g.geo.cuPerSa, 8);
        expect_u("I3: 8 render backends (2 SE x 4 per SE)", g.geo.rb, 8);
    }
    {
        const std::vector<uint8_t> b = blob_v1(1, 3, 4, 2, 2, 2, 1);
        const GcInfo g = parse_gc_info(b.data(), b.size());
        expect(g.reason == kOk && g.geo.se == 4 && g.geo.saPerSe == 1 && g.geo.cuPerSa == 8 && g.geo.rb == 8,
               "I3: another v1 table parses to its own numbers (4 SE, 1 SA, 2 x (2 + 2) = 8 CU, 4 x 2 = 8 RB): nothing is hard-coded");
    }
    {
        const std::vector<uint8_t> b = blob_v1(2);
        const GcInfo g = parse_gc_info(b.data(), b.size());
        expect(g.reason == kOk && g.geo.se == 2 && g.geo.cuPerSa == 8, "I3: a v2 binary header (table list at +16) finds the GC table");
    }
    {
        const std::vector<uint8_t> b = blob_gc_v2(2, 8, 2, 4);
        const GcInfo g = parse_gc_info(b.data(), b.size());
        expect(g.reason == kOk && g.verMajor == 2, "I3: a GC v2 table parses");
        expect(g.geo.se == 2 && g.geo.saPerSe == 2 && g.geo.cuPerSa == 8 && g.geo.rb == 8, "I3: GC v2: se, cu_per_sh, sh_per_se, se x rb_per_se");
    }
    {
        std::vector<uint8_t> b = blob_v1(); put32(b, 0, 0x28211408u);
        expect_u("I3: a wrong binary signature is refused", parse_gc_info(b.data(), b.size()).reason, kBadBinary);
        expect_u("I3: a null binary is refused", parse_gc_info(nullptr, 4096).reason, kBadBinary);
        expect_u("I3: a binary shorter than its header is refused", parse_gc_info(b.data(), 11).reason, kBadBinary);
    }
    {
        std::vector<uint8_t> b = blob_v1(); put16(b, 12 + 8, 0);
        expect_u("I3: no GC table (offset 0) is refused", parse_gc_info(b.data(), b.size()).reason, kNoGcTable);
    }
    {
        const std::vector<uint8_t> b = blob_v1();
        expect_u("I3: a GC table cut off by the buffer is refused", parse_gc_info(b.data(), kGcAt + 100).reason, kGcTruncated);
        std::vector<uint8_t> c = blob_v1(); put32(c, kGcAt + 8, 40); reseal_gc(c);
        expect_u("I3: a v1 GC table too short to hold gc_num_sa_per_se is refused", parse_gc_info(c.data(), c.size()).reason, kGcTruncated);
    }
    {
        std::vector<uint8_t> b = blob_v1(); put32(b, kGcAt, 0x4348u); reseal_gc(b);
        expect_u("I3: a table that is not 'GC' is refused", parse_gc_info(b.data(), b.size()).reason, kGcBadId);
    }
    {
        std::vector<uint8_t> b = blob_v1(); b[kGcAt + 12] ^= 1;   // se 2 -> 3 without resealing
        expect_u("I3: a GC table whose checksum does not match is refused", parse_gc_info(b.data(), b.size()).reason, kGcBadChecksum);
    }
    {
        std::vector<uint8_t> b = blob_v1(); put16(b, kGcAt + 4, 3); reseal_gc(b);
        expect_u("I3: a GC table version this parser does not know (3.x) is refused", parse_gc_info(b.data(), b.size()).reason, kGcBadVersion);
    }
    // Optional: this card's own capture, when a path is given (never committed: it lives in the private notes).
    if (const char *p = std::getenv("N44_DISCOVERY_BIN")) {
        std::ifstream f(p, std::ios::binary); std::stringstream ss; ss << f.rdbuf(); const std::string s = ss.str();
        const GcInfo g = parse_gc_info(reinterpret_cast<const uint8_t *>(s.data()), s.size());
        std::printf("note: %s: reason %s, GC %u.%u, %u/%u/%u/%u\n", p, reason_name(g.reason), g.verMajor, g.verMinor, g.geo.se, g.geo.saPerSe, g.geo.cuPerSa, g.geo.rb);
        expect(g.reason == kOk && check_geometry(g.geo) == kOk, "I3: the captured discovery binary parses and its geometry is accepted");
    }
}

static void i4_geometry() {
    using namespace n44id;
    expect_u("I4: the exact 2/2/8/8 is accepted", check_geometry(Geometry{ 2, 2, 8, 8 }), kOk);
    expect_u("I4: SE 3 is refused as an SE mismatch", check_geometry(Geometry{ 3, 2, 8, 8 }), kSeMismatch);
    expect_u("I4: SE 1 is refused as an SE mismatch", check_geometry(Geometry{ 1, 2, 8, 8 }), kSeMismatch);
    expect_u("I4: SA 3 is refused as an SA mismatch", check_geometry(Geometry{ 2, 3, 8, 8 }), kSaMismatch);
    expect_u("I4: SA 1 is refused as an SA mismatch", check_geometry(Geometry{ 2, 1, 8, 8 }), kSaMismatch);
    expect_u("I4: CU 9 is refused as a CU mismatch", check_geometry(Geometry{ 2, 2, 9, 8 }), kCuMismatch);
    expect_u("I4: CU 7 is refused as a CU mismatch", check_geometry(Geometry{ 2, 2, 7, 8 }), kCuMismatch);
    expect_u("I4: RB 9 is refused as an RB mismatch", check_geometry(Geometry{ 2, 2, 8, 9 }), kRbMismatch);
    expect_u("I4: RB 7 is refused as an RB mismatch", check_geometry(Geometry{ 2, 2, 8, 7 }), kRbMismatch);
    expect_u("I4: Navi 48's 4/2/8/16 is refused (SE first)", check_geometry(Geometry{ 4, 2, 8, 16 }), kSeMismatch);
    expect_u("I4: all zero is refused", check_geometry(Geometry{ 0, 0, 0, 0 }), kSeMismatch);
    expect(std::strcmp(reason_name(kOk), "ok") == 0, "I4: kOk is named ok");
    expect(std::strstr(reason_name(kSeMismatch), "shader engine") != nullptr, "I4: an SE refusal names the shader engines");
    expect(std::strstr(reason_name(kSaMismatch), "shader arrays") != nullptr, "I4: an SA refusal names the shader arrays");
    expect(std::strstr(reason_name(kCuMismatch), "CU") != nullptr, "I4: a CU refusal names the CUs");
    expect(std::strstr(reason_name(kRbMismatch), "render backend") != nullptr, "I4: an RB refusal names the render backends");
    expect(std::strcmp(reason_name(kRbMismatch), reason_name(kCuMismatch)) != 0 && std::strcmp(reason_name(kSeMismatch), reason_name(kSaMismatch)) != 0,
           "I4: each field's reason is distinct");
    {   // through the parser: a card whose gc_info is one field off is refused for that field
        const std::vector<uint8_t> b = blob_v1(1, 3, 2, 4, 0, 4, 3);
        const GcInfo g = parse_gc_info(b.data(), b.size());
        expect(g.reason == kOk && check_geometry(g.geo) == kSaMismatch, "I4: a parsed gc_info with sa_per_se 3 is refused as an SA mismatch");
        const std::vector<uint8_t> c = blob_v1(1, 3, 2, 4, 1, 4, 2);
        expect(check_geometry(parse_gc_info(c.data(), c.size()).geo) == kCuMismatch, "I4: a parsed gc_info with wgp1 1 (10 CU) is refused as a CU mismatch");
        const std::vector<uint8_t> d = blob_v1(1, 3, 2, 4, 0, 5, 2);
        expect(check_geometry(parse_gc_info(d.data(), d.size()).geo) == kRbMismatch, "I4: a parsed gc_info with 5 RB per SE (10 RB) is refused as an RB mismatch");
        const std::vector<uint8_t> e = blob_v1();
        expect(check_geometry(parse_gc_info(e.data(), e.size()).geo) == kOk, "I4: this card's parsed gc_info is accepted");
    }
}

static void i5_pins(const std::string &root) {
    const std::string K = root + "/src/navi48-bringup/";
    const std::string b = slurp(K + "src/Navi48Bringup.cpp");
    const std::string hpp = slurp(K + "src/Navi48Bringup.hpp");
    const std::string plist = slurp(K + "Info.plist");
    const std::string ip = slurp(K + "src/amd/amdgpu_ip.h");
    const std::string peer = slurp(K + "src/apple/Navi48AccelPeer.cpp");
    const std::string suites = slurp(root + "/tools/conductor/suites.sh");
    expect(!b.empty() && !plist.empty() && !ip.empty() && !peer.empty(), "I5: the sources are readable (run from the repo root)");

    // probe: the identity module decides, and nothing else names a device ID.
    const std::string pr = between(b, "IOService *Navi48Bringup::probe(IOService *provider, SInt32 *score) {", "\n}\n");
    expect(has(pr, "if (!n44id::accepts_device(vendorDevice)) {"), "I5: probe accepts through n44id::accepts_device");
    expect(!has(pr, "0x7550") && !has(pr, "0x7551") && !has(pr, "0x7590") && !has(pr, "0x1002"), "I5: probe names no device or vendor ID literal of its own");
    expect(has(pr, "not Navi 44, ignoring") && has(pr, "probe: Navi 44 RX 9060 XT"), "I5: probe logs Navi 44 (accept and refuse)");
    expect(has(b, "#include \"amd/navi44_identity_pure.h\""), "I5: Navi48Bringup.cpp includes the identity module");

    // Info.plist: the bring-up personality matches the module's string; the Apple / accelerator personalities never match this card.
    expect(primary_match(plist, "Navi48Bringup") == n44id::kPciPrimaryMatch, "I5: the Navi48Bringup personality's IOPCIPrimaryMatch is n44id::kPciPrimaryMatch");
    for (const char *other : { "Navi48AccelPeer", "Navi48AppleAccelExperiment", "Navi48AppleHWServices" }) {
        const std::string m = primary_match(plist, other);
        expect(!m.empty() && !has(m, "7590"), "I5: the post-ladder / Apple personalities do not match 7590 (Navi48AppleHWServices has no boot-arg gate)");
    }
    expect_u("I5: exactly one personality matches 7590", count_of(plist, "0x75901002"), 1);

    // amdgpu_ip.h: every pin comes from the module.
    expect(has(ip, "#include \"navi44_identity_pure.h\""), "I5: amdgpu_ip.h includes the identity module");
    for (const char *line : { "kIP_GFX  = ip_of(n44id::kGc);", "kIP_GMC  = ip_of(n44id::kGc);", "kIP_SDMA = ip_of(n44id::kSdma);",
                              "kIP_PSP  = ip_of(n44id::kPsp);", "kIP_SMU  = ip_of(n44id::kSmu);", "kIP_MES  = ip_of(n44id::kGc);",
                              "kIP_NBIO = ip_of(n44id::kNbif);", "kIP_IH   = ip_of(n44id::kOsssys);", "kIP_HDP  = ip_of(n44id::kHdp);" })
        expect(has(ip, line), line);
    expect_u("I5: amdgpu_ip.h has no literal IPVersion pin left", count_of(ip, "constexpr IPVersion kIP_") - count_of(ip, "= ip_of(n44id::"), 0);

    // Stage 1: start() gates everything after the IP survey on the identity check.
    const std::string st = between(b, "bool Navi48Bringup::start(IOService *provider) {", "\nvoid Navi48Bringup::stop(");
    const size_t gate = st.find("if (stageIPDiscovery()) {"), sip = st.find("surveyIPs();"), arm = st.find("armInterrupts();"),
                 psp = st.find("stagePSP();"), spsp = st.find("surveyPSP();"), stg = st.find("N48LOG(\"stages: navi48-arm stage:%u");
    expect(gate != std::string::npos && sip != std::string::npos && sip < gate, "I5: start() runs the stage-1 gate after the IP survey");
    expect(stg != std::string::npos && stg < gate, "I5: the token's stage target (set in probe, ticket #16) is logged before the stage-1 gate (it publishes the target)");
    expect(gate != std::string::npos && arm != std::string::npos && psp != std::string::npos && spsp != std::string::npos && gate < spsp && gate < arm && gate < psp,
           "I5: surveyPSP, armInterrupts and stagePSP run only after the stage-1 gate");
    expect_u("I5: start() has one stage-1 gate", count_of(st, "stageIPDiscovery()"), 1);
    expect(has(hpp, "bool stageIPDiscovery();"), "I5: the header declares stageIPDiscovery");
    const std::string s1 = between(b, "bool Navi48Bringup::stageIPDiscovery() {", "\n}\n");
    expect(has(s1, "n44id::parse_gc_info(onDieDisc + ipDiscovery.binaryOffset(), n48::kDiscTmrSize - ipDiscovery.binaryOffset())"),
           "I5: stage 1 parses gc_info from the on-die binary");
    expect(has(s1, "n44id::check_geometry(gi.geo)"), "I5: stage 1 checks the discovered geometry");
    for (const char *key : { "\"Navi48,GeometrySE\"", "\"Navi48,GeometrySAPerSE\"", "\"Navi48,GeometryCUPerSA\"", "\"Navi48,GeometryRB\"",
                             "\"Navi48,Geometry\"", "\"Navi48,GeometryCheck\"", "\"Navi48,StageReached\"", "\"Navi48,StageReachedName\"",
                             "\"Navi48,StageFailed\"", "\"Navi48,StageTarget\"" })
        expect(has(s1, key), key);
    expect(has(s1, "amdgpu::stage_name(amdgpu::BringupStage::IPDiscovery)"), "I5: stage 1 names its stage through amdgpu::stage_name");
    expect(has(between(b, "void Navi48Bringup::surveyIPs() {", "\n}\n"), "n44id::expected_ip("), "I5: surveyIPs compares each IP with the expected version");

    // The kmutil link fix and the suite.
    expect_u("I5: Navi48AccelPeer calls no strnstr (not exported on 26.7.2)", count_of(peer, "strnstr("), 0);
    expect(has(suites, "$T/navi44_identity_test.cpp"), "I5: tools/conductor/suites.sh runs this test");
}

int main(int argc, char **argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    i1_device(); i2_versions(); i3_gc_info(); i4_geometry(); i5_pins(root);
    std::printf("navi44_identity_test: %d checks, %d failed\n", gRun, gFail);
    return gFail == 0 ? 0 : 1;
}
