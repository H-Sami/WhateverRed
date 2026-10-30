// navi44_identity_pure.h - the Navi 44 (RX 9060 XT) identity, in one place (ticket #16, spec #14).
//
// What this card IS, as the bring-up must see it before it touches anything:
//   - the PCI identity the probe accepts (1002:7590) and the Info.plist personality string;
//   - the IP versions its on-die discovery table reports (ground truth: map #1 Notes, ticket #6);
//   - the shader-engine geometry its GC table (gc_info) reports, and the check that refuses anything else.
// Freestanding: <stdint.h> / <stddef.h> only, no allocation, no kernel calls. Compiled into the kext and the host test.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace n44id {

// ---------------------------------------------------------------------------
// PCI identity
static constexpr uint16_t kVendorId = 0x1002;
static constexpr uint16_t kDeviceId = 0x7590;            // Navi 44, RX 9060 XT
static constexpr const char *kPciPrimaryMatch = "0x75901002";   // IOPCIPrimaryMatch: device << 16 | vendor

// vendorDevice is config dword 0 (vendor in the low half, device in the high half).
static constexpr bool accepts_device(uint32_t vendorDevice) {
    return (vendorDevice & 0xffffu) == kVendorId && (vendorDevice >> 16) == kDeviceId;
}

// ---------------------------------------------------------------------------
// IP versions, as this card's on-die discovery table reports them (map #1 Notes, ticket #6).
// Linux picks gfx_v12_0 / gmc_v12_0 / mes_v12_0 from GC 12.0.0, sdma_v7_0, psp_v14_0 and smu_v14_0 (14.0.2), ih_v7_0.
struct IpVer { uint8_t major, minor, rev; };

static constexpr IpVer kGc     = { 12, 0, 0 };   // GC (hw 11)
static constexpr IpVer kSdma   = {  7, 0, 0 };   // SDMA0 / SDMA1 (hw 42 / 43)
static constexpr IpVer kPsp    = { 14, 0, 2 };   // MP0 (hw 255)
static constexpr IpVer kSmu    = { 14, 0, 2 };   // MP1 (hw 1)
static constexpr IpVer kOsssys = {  7, 0, 0 };   // OSSSYS / IH (hw 40)
static constexpr IpVer kHdp    = {  7, 0, 0 };   // HDP (hw 41)
static constexpr IpVer kNbif   = {  6, 3, 1 };   // NBIF (hw 108)
static constexpr IpVer kMmhub  = {  4, 1, 0 };   // MMHUB (hw 34)
static constexpr IpVer kDcn    = {  4, 1, 0 };   // DMU / DCN (hw 271)
static constexpr IpVer kUmc    = {  8, 14, 0 };  // UMC (hw 150), four instances

struct ExpectedIp { uint16_t hwId; const char *name; IpVer ver; };
static constexpr ExpectedIp kExpectedIps[] = {
    {  11, "GC",    kGc     }, {  34, "MMHUB", kMmhub }, { 255, "PSP",  kPsp  }, {   1, "SMU", kSmu },
    {  42, "SDMA",  kSdma   }, {  40, "IH",    kOsssys}, { 108, "NBIF", kNbif }, { 271, "DCN", kDcn },
    {  41, "HDP",   kHdp    }, { 150, "UMC",   kUmc   },
};

// The version this card's table reports for a hardware ID, or nullptr for an IP this module does not pin.
static inline const IpVer *expected_ip(uint16_t hwId) {
    for (const ExpectedIp &e : kExpectedIps) if (e.hwId == hwId) return &e.ver;
    return nullptr;
}
static constexpr bool same_ver(const IpVer &a, uint8_t major, uint8_t minor, uint8_t rev) {
    return a.major == major && a.minor == minor && a.rev == rev;
}

// ---------------------------------------------------------------------------
// Shader-engine geometry, from the GC table (gc_info) of the IP discovery binary.
// Navi 44 (RX 9060 XT): 2 SE, 2 SA per SE, 8 CU per SA, 8 RB in total (gc_info v1.3: se 2, wgp0 4, wgp1 0, rb_per_se 4, sa_per_se 2).
struct Geometry { uint32_t se, saPerSe, cuPerSa, rb; };
static constexpr Geometry kGeometry = { 2, 2, 8, 8 };

enum Reason : uint32_t {
    kOk = 0,
    kBadBinary,       // no binary, too short, or not the 0x28211407 signature
    kNoGcTable,       // the table list has no GC entry
    kGcTruncated,     // the GC table runs past the binary, or is too short for the fields read
    kGcBadId,         // the GC entry does not start with table_id "GC" (0x4347)
    kGcBadChecksum,   // the 16-bit byte sum of the GC table is not the table list's checksum
    kGcBadVersion,    // a gc_info major version other than 1 or 2
    kSeMismatch,      // geometry check: shader engines
    kSaMismatch,      // geometry check: shader arrays per SE
    kCuMismatch,      // geometry check: CU per SA
    kRbMismatch,      // geometry check: render backends
};
static inline const char *reason_name(uint32_t r) {
    switch (r) {
    case kOk:            return "ok";
    case kBadBinary:     return "bad discovery binary";
    case kNoGcTable:     return "no GC table";
    case kGcTruncated:   return "GC table truncated";
    case kGcBadId:       return "GC table id is not GC";
    case kGcBadChecksum: return "GC table checksum mismatch";
    case kGcBadVersion:  return "unknown gc_info version";
    case kSeMismatch:    return "shader engine count mismatch";
    case kSaMismatch:    return "shader arrays per SE mismatch";
    case kCuMismatch:    return "CU per SA mismatch";
    case kRbMismatch:    return "render backend count mismatch";
    default:             return "unknown";
    }
}

struct GcInfo { uint32_t reason; uint16_t verMajor, verMinor; Geometry geo; };

static constexpr uint32_t kBinarySignature = 0x28211407u;
static constexpr uint32_t kGcTableId       = 0x4347u;     // "GC"
static constexpr size_t   kGcTableIndex    = 1;           // table_info[1]

static inline uint32_t rd16(const uint8_t *p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8; }
static inline uint32_t rd32(const uint8_t *p) { return rd16(p) | rd16(p + 2) << 16; }

// bin points at the discovery binary header; len is how many bytes are readable from there.
// Linux amdgpu_discovery_get_gfx_info: v1 cu_per_sh = 2 x (wgp0 + wgp1), sh_per_se = sa_per_se; v2 reads them directly.
static inline GcInfo parse_gc_info(const uint8_t *bin, size_t len) {
    GcInfo g = { kBadBinary, 0, 0, { 0, 0, 0, 0 } };
    if (!bin || len < 12 || rd32(bin) != kBinarySignature) return g;
    const size_t tl = rd16(bin + 4) >= 2 ? 16 : 12;
    const size_t ti = tl + kGcTableIndex * 8;
    if (ti + 8 > len) { g.reason = kBadBinary; return g; }
    const size_t off = rd16(bin + ti);
    const uint32_t want = rd16(bin + ti + 2);
    if (off == 0) { g.reason = kNoGcTable; return g; }
    if (off + 12 > len) { g.reason = kGcTruncated; return g; }
    const uint8_t *gc = bin + off;
    if (rd32(gc) != kGcTableId) { g.reason = kGcBadId; return g; }
    const uint32_t size = rd32(gc + 8);
    if (size < 12 || size > len - off) { g.reason = kGcTruncated; return g; }
    uint32_t sum = 0;
    for (uint32_t i = 0; i < size; i++) sum = (sum + gc[i]) & 0xffffu;
    if (sum != want) { g.reason = kGcBadChecksum; return g; }
    g.verMajor = uint16_t(rd16(gc + 4)); g.verMinor = uint16_t(rd16(gc + 6));
    if (g.verMajor == 1) {
        if (size < 80) { g.reason = kGcTruncated; return g; }
        const uint32_t se = rd32(gc + 12), wgp0 = rd32(gc + 16), wgp1 = rd32(gc + 20), rbPerSe = rd32(gc + 24), sa = rd32(gc + 76);
        g.geo = { se, sa, 2 * (wgp0 + wgp1), se * rbPerSe };
    } else if (g.verMajor == 2) {
        if (size < 28) { g.reason = kGcTruncated; return g; }
        const uint32_t se = rd32(gc + 12), cu = rd32(gc + 16), sh = rd32(gc + 20), rbPerSe = rd32(gc + 24);
        g.geo = { se, sh, cu, se * rbPerSe };
    } else {
        g.reason = kGcBadVersion; return g;
    }
    g.reason = kOk;
    return g;
}

// The discovered geometry against this card's: kOk, or the first field that differs (SE, SA, CU, RB in that order).
static constexpr uint32_t check_geometry(const Geometry &g) {
    return g.se      != kGeometry.se      ? kSeMismatch
         : g.saPerSe != kGeometry.saPerSe ? kSaMismatch
         : g.cuPerSa != kGeometry.cuPerSa ? kCuMismatch
         : g.rb      != kGeometry.rb      ? kRbMismatch
         : kOk;
}

} // namespace n44id
