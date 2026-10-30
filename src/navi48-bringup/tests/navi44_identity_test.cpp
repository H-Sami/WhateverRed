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
//   I5  source pins of the REAL code: probe, Info.plist, amdgpu_ip.h and the stage-1 path consume this module;
//   I6  firmware (ticket #17): the Navi 44 name set, each blob's pinned version (SMU 102.70.0, SOS >= 0x3b0e0d), the set check,
//       external_rev_id; N44_FIRMWARE_DIR=<dir> also checks a real firmware directory (never committed);
//   I7  source pins: fw_table, stagePSP, the Makefile gate, fetch-firmware.sh, hosttest and the SOC / SMU deltas use this set.
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

// A synthetic firmware file: common_firmware_header (size_bytes = file length, ip_version, ucode_version, crc32),
// then for a PSP container the descriptor count and {type, version, offset, size} descriptors.
struct Desc { uint32_t type, version; };
static std::vector<uint8_t> fw_blob(uint16_t ipMajor, uint16_t ipMinor, uint32_t ucode, uint32_t crc = 0,
                                    std::vector<Desc> descs = {}, size_t pad = 256) {
    std::vector<uint8_t> b(36 + 16 * descs.size() + pad, 0);
    auto w16 = [&](size_t o, uint32_t v) { b[o] = uint8_t(v); b[o + 1] = uint8_t(v >> 8); };
    auto w32 = [&](size_t o, uint32_t v) { w16(o, v & 0xffff); w16(o + 2, v >> 16); };
    w32(0, uint32_t(b.size())); w32(4, 36); w16(8, 2); w16(10, 0); w16(12, ipMajor); w16(14, ipMinor);
    w32(16, ucode); w32(20, 64); w32(24, 0x100); w32(28, crc);
    w32(32, uint32_t(descs.size()));
    for (size_t i = 0; i < descs.size(); i++) { w32(36 + 16 * i, descs[i].type); w32(40 + 16 * i, descs[i].version); }
    return b;
}
static uint32_t chk(const char *name, const std::vector<uint8_t> &b) { return n44id::check_firmware(name, b.data(), b.size()); }

static void i6_firmware() {
    using namespace n44id;
    // The name set: upstream's Navi 44 blobs, and nothing else.
    const char *want[] = { "gc_12_0_0_imu", "gc_12_0_0_me", "gc_12_0_0_mec", "gc_12_0_0_pfp", "gc_12_0_0_rlc", "gc_12_0_0_uni_mes",
                           "psp_14_0_2_sos", "psp_14_0_2_ta", "sdma_7_0_0", "smu_14_0_2" };
    expect_u("I6: the firmware set has ten blobs", kFirmwareCount, 10);
    for (const char *n : want) expect(find_firmware(n) != nullptr, n);
    for (const char *n : { "gc_12_0_1_me", "gc_12_0_1_uni_mes", "psp_14_0_3_sos", "psp_14_0_3_ta", "smu_14_0_3", "sdma_7_0_1",
                           "gc_12_0_0_mes", "gc_12_0_0_mes1", "gc_12_0_0_toc", "psp_14_0_2_sos_kicker", "gc_12_0_0_rlc_kicker", "", "gc_12_0_0" })
        expect(find_firmware(n) == nullptr, "I6: a Navi 48, split-MES, kicker or partial name is not in the set");
    expect(find_firmware(nullptr) == nullptr, "I6: a null name is not in the set");
    expect(std::strlen(kLinuxFirmwareCommit) == 40 && std::strcmp(kLinuxFirmwareCommit, "2a12d06438f2092115b9cf53dca79258bf283f88") == 0,
           "I6: the linux-firmware commit is pinned by its full hash");

    // Ucode-version blobs: the ground-truth version passes; one off, the wrong IP, or a truncated file is refused.
    expect_u("I6: gc_12_0_0_me ME 0xbc2 passes", chk("gc_12_0_0_me", fw_blob(12, 0, 0xbc2)), kOk);
    expect_u("I6: gc_12_0_0_me 0xbc3 is a version mismatch", chk("gc_12_0_0_me", fw_blob(12, 0, 0xbc3)), kFwVersionMismatch);
    expect_u("I6: gc_12_0_0_pfp PFP 0xc12 passes", chk("gc_12_0_0_pfp", fw_blob(12, 0, 0xc12)), kOk);
    expect_u("I6: gc_12_0_0_mec MEC 0xd0c passes", chk("gc_12_0_0_mec", fw_blob(12, 0, 0xd0c)), kOk);
    expect_u("I6: gc_12_0_0_rlc 0x00bde160 passes", chk("gc_12_0_0_rlc", fw_blob(12, 0, 0x00bde160)), kOk);
    expect_u("I6: gc_12_0_0_imu 0x0c2c2500 passes", chk("gc_12_0_0_imu", fw_blob(12, 0, 0x0c2c2500)), kOk);
    expect_u("I6: sdma_7_0_0 0x00798e96 passes", chk("sdma_7_0_0", fw_blob(7, 0, 0x00798e96)), kOk);
    expect_u("I6: gc_12_0_0_me with ip 11.0 is an IP mismatch", chk("gc_12_0_0_me", fw_blob(11, 0, 0xbc2)), kFwIpMismatch);
    expect_u("I6: sdma_7_0_0 with ip 7.1 is an IP mismatch", chk("sdma_7_0_0", fw_blob(7, 1, 0x00798e96)), kFwIpMismatch);
    {
        std::vector<uint8_t> b = fw_blob(12, 0, 0xbc2);
        expect_u("I6: a 16-byte file is truncated", n44id::check_firmware("gc_12_0_0_me", b.data(), 16), kFwTruncated);
        expect_u("I6: a file shorter than its size_bytes is truncated", n44id::check_firmware("gc_12_0_0_me", b.data(), b.size() - 1), kFwTruncated);
        expect_u("I6: no data is truncated", n44id::check_firmware("gc_12_0_0_me", nullptr, 0), kFwTruncated);
    }
    expect_u("I6: a good blob under a Navi 48 name is an unknown name", chk("gc_12_0_1_me", fw_blob(12, 0, 0xbc2)), kFwUnknownName);

    // SMU 102.70.0; linux-firmware's later 102.71.0 is refused.
    expect(smu_major(kSmuVersion) == 102 && smu_minor(kSmuVersion) == 70 && smu_patch(kSmuVersion) == 0, "I6: the pinned SMU is 102.70.0");
    expect_u("I6: smu_14_0_2 0x00664600 (102.70.0) passes", chk("smu_14_0_2", fw_blob(14, 0, 0x00664600)), kOk);
    expect_u("I6: smu_14_0_2 0x00664700 (102.71.0) is a version mismatch", chk("smu_14_0_2", fw_blob(14, 0, 0x00664700)), kFwVersionMismatch);

    // uni_mes: ucode_version is 0, so the crc32 is pinned.
    expect_u("I6: gc_12_0_0_uni_mes with crc32 0x1a2b3f08 passes", chk("gc_12_0_0_uni_mes", fw_blob(12, 0, 0, 0x1a2b3f08)), kOk);
    expect_u("I6: gc_12_0_0_uni_mes with another crc32 is a mismatch", chk("gc_12_0_0_uni_mes", fw_blob(12, 0, 0, 0x1a2b3f09)), kFwVersionMismatch);

    // SOS: the descriptor of type 1 is 0x3b0f0d (>= 0x3b0e0d).
    const std::vector<Desc> sosOk = { { 3, 0x703b0001 }, { 2, 0x3b0f0d }, { 1, 0x3b0f0d }, { 4, 0 } };
    expect(kSosMinForReservation == 0x3b0e0d && kSosVersion >= kSosMinForReservation, "I6: the pinned SOS is at least 0x3b0e0d");
    expect_u("I6: psp_14_0_2_sos with SOS 0x3b0f0d passes", chk("psp_14_0_2_sos", fw_blob(14, 0, 0x3b0f0d, 0, sosOk)), kOk);
    expect_u("I6: SOS 0x3b0e0c is too old", chk("psp_14_0_2_sos", fw_blob(14, 0, 0x3b0f0d, 0, { { 1, 0x3b0e0c } })), kFwSosTooOld);
    expect_u("I6: SOS 0x3b0e0d meets the floor but is not the pinned version", chk("psp_14_0_2_sos", fw_blob(14, 0, 0x3b0f0d, 0, { { 1, 0x3b0e0d } })), kFwVersionMismatch);
    expect_u("I6: a SOS container without an SOS descriptor is refused", chk("psp_14_0_2_sos", fw_blob(14, 0, 0x3b0f0d, 0, { { 2, 0x3b0f0d } })), kFwNoDescriptor);
    {
        std::vector<uint8_t> b = fw_blob(14, 0, 0x3b0f0d, 0, sosOk, 0);
        b[32] = 200;   // a descriptor count running past the file
        expect_u("I6: a descriptor count past the file is truncated", n44id::check_firmware("psp_14_0_2_sos", b.data(), b.size()), kFwTruncated);
    }
    expect_u("I6: psp_14_0_2_sos with ip 14.3 is an IP mismatch", chk("psp_14_0_2_sos", fw_blob(14, 3, 0x3b0f0d, 0, sosOk)), kFwIpMismatch);

    // TA: ASD, HDCP and DTM each pinned.
    const std::vector<Desc> taOk = { { 1, 0x21000110 }, { 4, 0x17000050 }, { 5, 0x1200001d } };
    expect_u("I6: psp_14_0_2_ta with ASD/HDCP/DTM passes", chk("psp_14_0_2_ta", fw_blob(14, 0, 0, 0, taOk)), kOk);
    expect_u("I6: TA with HDCP 0x17000051 is a mismatch", chk("psp_14_0_2_ta", fw_blob(14, 0, 0, 0, { { 1, 0x21000110 }, { 4, 0x17000051 }, { 5, 0x1200001d } })), kFwVersionMismatch);
    expect_u("I6: TA without DTM is refused", chk("psp_14_0_2_ta", fw_blob(14, 0, 0, 0, { { 1, 0x21000110 }, { 4, 0x17000050 } })), kFwNoDescriptor);
    expect_u("I6: TA ASD 0x21000110 is not mistaken for an old SOS", chk("psp_14_0_2_ta", fw_blob(14, 0, 0, 0, { { 1, 0x00000001 }, { 4, 0x17000050 }, { 5, 0x1200001d } })), kFwVersionMismatch);

    // The whole set, through the loader seam.
    std::vector<std::pair<std::string, std::vector<uint8_t>>> good = {
        { "gc_12_0_0_imu", fw_blob(12, 0, 0x0c2c2500) }, { "gc_12_0_0_me", fw_blob(12, 0, 0xbc2) }, { "gc_12_0_0_mec", fw_blob(12, 0, 0xd0c) },
        { "gc_12_0_0_pfp", fw_blob(12, 0, 0xc12) }, { "gc_12_0_0_rlc", fw_blob(12, 0, 0x00bde160) }, { "gc_12_0_0_uni_mes", fw_blob(12, 0, 0, 0x1a2b3f08) },
        { "psp_14_0_2_sos", fw_blob(14, 0, 0x3b0f0d, 0, sosOk) }, { "psp_14_0_2_ta", fw_blob(14, 0, 0, 0, taOk) },
        { "sdma_7_0_0", fw_blob(7, 0, 0x00798e96) }, { "smu_14_0_2", fw_blob(14, 0, 0x00664600) },
    };
    auto loader = [](std::vector<std::pair<std::string, std::vector<uint8_t>>> &set) {
        return [&set](const char *n, const uint8_t **d, size_t *len) {
            for (auto &e : set) if (e.first == n) { *d = e.second.data(); *len = e.second.size(); return true; }
            return false;
        };
    };
    FwSetResult r = check_firmware_set(loader(good));
    expect_u("I6: the pinned set passes", r.reason, kOk);
    auto withSmu = good; withSmu[9].second = fw_blob(14, 0, 0x00664700);
    r = check_firmware_set(loader(withSmu));
    expect(r.reason == kFwVersionMismatch && r.index < kFirmwareCount && std::strcmp(kFirmware[r.index].name, "smu_14_0_2") == 0,
           "I6: the set with linux-firmware tip's SMU fails on smu_14_0_2");
    auto renamed = good; renamed[1].first = "gc_12_0_1_me";
    r = check_firmware_set(loader(renamed));
    expect(r.reason == kFwMissing && r.index < kFirmwareCount && std::strcmp(kFirmware[r.index].name, "gc_12_0_0_me") == 0,
           "I6: the set with a Navi 48 name instead of gc_12_0_0_me reports gc_12_0_0_me missing");
    for (uint32_t x : { kFwUnknownName, kFwMissing, kFwTruncated, kFwIpMismatch, kFwNoDescriptor, kFwSosTooOld, kFwVersionMismatch })
        expect(std::strcmp(reason_name(x), "unknown") != 0, "I6: each firmware reason is named");

    // SOC: external_rev_id = rev + 0x40 (Linux soc24.c for GC 12.0.0).
    expect_u("I6: external_rev_id(0x00) is 0x40", external_rev_id(0x00), 0x40);
    expect_u("I6: external_rev_id(0xc0) is 0x100 (no 8-bit wrap)", external_rev_id(0xc0), 0x100);

    // Optional: a real firmware directory (never committed), e.g. src/navi48-bringup/firmware.
    if (const char *dir = std::getenv("N44_FIRMWARE_DIR")) {
        std::vector<std::pair<std::string, std::vector<uint8_t>>> real;
        for (const char *n : want) {
            const std::string s = slurp(std::string(dir) + "/" + n + ".bin");
            if (!s.empty()) real.push_back({ n, std::vector<uint8_t>(s.begin(), s.end()) });
        }
        r = check_firmware_set(loader(real));
        std::printf("note: %s: %s%s%s\n", dir, reason_name(r.reason), r.index < kFirmwareCount ? " at " : "", r.index < kFirmwareCount ? kFirmware[r.index].name : "");
        expect_u("I6: the firmware directory passes the check", r.reason, kOk);
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

// I7: the real code consumes the firmware set (ticket #17), and the SOC / SMU deltas Linux applies on Navi 44 hold.
static void i7_fw_pins(const std::string &root) {
    using namespace n44id;
    const std::string K = root + "/src/navi48-bringup/";
    const std::string fwt = slurp(K + "src/amd/fw_table.cpp");
    const std::string b = slurp(K + "src/Navi48Bringup.cpp");
    const std::string mk = slurp(K + "Makefile");
    const std::string ck = slurp(K + "hosttest/check_firmware.cpp");
    const std::string ht = slurp(K + "hosttest/main.cpp");
    const std::string fetch = slurp(root + "/tools/fetch-firmware.sh");
    const std::string smu = slurp(K + "src/amd/smu_v14_0.cpp");
    const std::string init = slurp(K + "src/amd/amdgpu_init.cpp");
    expect(!fwt.empty() && !mk.empty() && !ck.empty() && !ht.empty() && !fetch.empty() && !smu.empty() && !init.empty(),
           "I7: the firmware sources are readable (run from the repo root)");

    // fw_table.cpp: every blob of the set is embedded and looked up under its Navi 44 name; no Navi 48 name is left.
    for (const FwSpec &f : kFirmware) {
        const std::string n = f.name;
        expect(has(fwt, "extern const uint8_t fw_" + n + "[];") && has(fwt, "extern const size_t  fw_" + n + "_size;"), ("I7: fw_table.cpp declares fw_" + n).c_str());
        expect(has(fwt, "FwBlob{ fw_" + n + ", fw_" + n + "_size, \"" + n + "\","), ("I7: fw_table.cpp has the " + n + " row").c_str());
        expect(has(fetch, "\n" + n + " "), ("I7: tools/fetch-firmware.sh copies " + n).c_str());
    }
    for (const char *old : { "gc_12_0_1", "psp_14_0_3_", "smu_14_0_3", "sdma_7_0_1" }) {
        expect(!has(fwt, old) && !has(b, old) && !has(mk, old) && !has(ht, old) && !has(fetch, old), "I7: no Navi 48 firmware name in fw_table / Navi48Bringup / Makefile / hosttest / fetch-firmware");
    }
    expect(has(b, "psp.parseContainer(fw_psp_14_0_2_sos, fw_psp_14_0_2_sos_size)"), "I7: stagePSP parses psp_14_0_2_sos");

    // The build gate: check-firmware runs the identity module's checker before any object, and the set lives only there.
    expect(has(mk, "$(OBJS): | check-firmware") && has(mk, "check-firmware: $(BUILD)/check-firmware") && has(mk, "@$(BUILD)/check-firmware firmware"),
           "I7: make check-firmware runs the host checker on firmware/ before any object");
    expect(has(mk, "hosttest/check_firmware.cpp src/amd/navi44_identity_pure.h"), "I7: the checker rebuilds when the identity module changes");
    expect(!has(mk, "FW_REQUIRED"), "I7: the Makefile keeps no firmware name list of its own");
    expect(has(ck, "#include \"navi44_identity_pure.h\"") && has(ck, "for (const n44id::FwSpec &f : n44id::kFirmware)") && has(ck, "n44id::check_firmware(f,") && has(ck, "return 1;"),
           "I7: the checker walks n44id::kFirmware, checks each blob and fails");
    expect(has(fetch, std::string("PINNED_COMMIT=") + kLinuxFirmwareCommit), "I7: tools/fetch-firmware.sh pins the module's linux-firmware commit");
    expect(has(fetch, "rm -f \"$DST/$name.bin\"; bad=1") && !has(fetch, "copied anyway"), "I7: tools/fetch-firmware.sh refuses a sha256 mismatch");
    for (const char *n : { "\"sdma_7_0_0.bin\"", "\"gc_12_0_0_rlc.bin\"", "\"gc_12_0_0_imu.bin\"", "\"gc_12_0_0_uni_mes.bin\"",
                           "\"gc_12_0_0_pfp.bin\"", "\"gc_12_0_0_me.bin\"", "\"gc_12_0_0_mec.bin\"", "\"smu_14_0_2.bin\"" })
        expect(has(ht, n), n);

    // SOC / SMU, as Linux runs Navi 44: external_rev_id = rev + 0x40, no JPEG_DPG power gating, no energy accumulator,
    // and EnableAllSmuFeatures only once, in smc_hw_setup (no reset-path EnableAllSmuFeatures / FEATURE_PWR_GFX).
    const std::string pr = between(b, "IOService *Navi48Bringup::probe(IOService *provider, SInt32 *score) {", "\n}\n");
    expect(has(pr, "n44id::external_rev_id(pciRev)") && has(pr, "\"Navi48,ExternalRevId\"") && has(pr, "configRead8(kIOPCIConfigRevisionID)"),
           "I7: probe publishes Navi48,ExternalRevId through n44id::external_rev_id");
    for (const std::string *s : { &smu, &init, &b }) {
        expect(!has(*s, "JPEG_DPG") && !has(*s, "pg_flags"), "I7: no JPEG_DPG power gating");
        expect(!has(*s, "EnergyAcc") && !has(*s, "energy_acc"), "I7: no energy accumulator");
        expect(!has(*s, "PWR_GFX"), "I7: no FEATURE_PWR_GFX enable");
    }
    expect_u("I7: EnableAllSmuFeatures is sent once", count_of(smu, "smu_send_msg(dev, PPSMC::EnableAllSmuFeatures)") + count_of(init, "EnableAllSmuFeatures") + count_of(b, "PPSMC::EnableAllSmuFeatures"), 1);
    expect(has(between(smu, "smu_smc_hw_setup(DeviceContext &dev, uint64_t driver_table_mc)", "\n}\n"), "smu_send_msg(dev, PPSMC::EnableAllSmuFeatures)"),
           "I7: that one EnableAllSmuFeatures is in smu_smc_hw_setup");
}

int main(int argc, char **argv) {
    const std::string root = argc > 1 ? argv[1] : ".";
    i1_device(); i2_versions(); i3_gc_info(); i4_geometry(); i5_pins(root); i6_firmware(); i7_fw_pins(root);
    std::printf("navi44_identity_test: %d checks, %d failed\n", gRun, gFail);
    return gFail == 0 ? 0 : 1;
}
