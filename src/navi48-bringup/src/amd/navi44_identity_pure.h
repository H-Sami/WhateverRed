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
    kFwUnknownName,   // firmware check: not one of this card's blob names (kFirmware)
    kFwMissing,       // firmware check: a required blob is absent
    kFwTruncated,     // firmware check: shorter than its header, or the header's size_bytes is not the file length
    kFwIpMismatch,    // firmware check: the header's ip_version major.minor is not this blob's IP
    kFwNoDescriptor,  // firmware check: a PSP container lacks a pinned sub-binary descriptor
    kFwSosTooOld,     // firmware check: the SOS sub-binary is older than kSosMinForReservation
    kFwVersionMismatch, // firmware check: a version (or the uni_mes crc32) is not the pinned one
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
    case kFwUnknownName: return "firmware name is not a Navi 44 blob";
    case kFwMissing:     return "required firmware missing";
    case kFwTruncated:   return "firmware truncated (header size_bytes is not the file length)";
    case kFwIpMismatch:  return "firmware header IP version mismatch";
    case kFwNoDescriptor: return "PSP container lacks a pinned sub-binary";
    case kFwSosTooOld:   return "SOS older than 0x3b0e0d (no reserved-tail TMR)";
    case kFwVersionMismatch: return "firmware version mismatch";
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

// ---------------------------------------------------------------------------
// Firmware (ticket #17): the blobs Linux loads on this card, and the versions they must carry.
// Names: upstream's set for GC 12.0.0 / PSP 14.0.2 / SMU 14.0.2 / SDMA 7.0.0 (the unified MES blob; no kicker blobs).
// Versions: the Linux ground truth's amdgpu_firmware_info (ticket #6 capture), all present in exactly one
// linux-firmware commit, which tools/fetch-firmware.sh pins by hash:
static constexpr const char *kLinuxFirmwareCommit = "2a12d06438f2092115b9cf53dca79258bf283f88";

// PSP v2.0 container sub-binary types (psp_firmware_header_v2_0 descriptors, amdgpu_ucode.h psp_fw_type).
static constexpr uint32_t kPspFwSos  = 1;   // in psp_14_0_2_sos.bin: PSP_FW_TYPE_PSP_SOS
static constexpr uint32_t kPspFwAsd  = 1;   // in psp_14_0_2_ta.bin:  TA_FW_TYPE_PSP_ASD
static constexpr uint32_t kPspFwHdcp = 4;   // TA_FW_TYPE_PSP_HDCP
static constexpr uint32_t kPspFwDtm  = 5;   // TA_FW_TYPE_PSP_DTM

// SOS from this version on reports FB_FW_RESERV_ADDR, so the PSP TMR sits in the reserved VRAM tail (spec #14).
static constexpr uint32_t kSosMinForReservation = 0x003b0e0du;
static constexpr uint32_t kSosVersion           = 0x003b0f0du;
static_assert(kSosVersion >= kSosMinForReservation, "the pinned SOS must be new enough for the reserved-tail TMR");

// SMU PMFW 102.70.0 (smu_v14_0 packs it as 0x00MMmmpp).
static constexpr uint32_t kSmuVersion = 0x00664600u;
static constexpr uint32_t smu_major(uint32_t v) { return (v >> 16) & 0xffu; }
static constexpr uint32_t smu_minor(uint32_t v) { return (v >> 8) & 0xffu; }
static constexpr uint32_t smu_patch(uint32_t v) { return v & 0xffu; }

enum FwCheck : uint8_t {
    kByUcodeVersion,   // common_firmware_header.ucode_version (offset 16)
    kByDescriptors,    // PSP v2.0 container: each pinned descriptor {type, version}
    kByCrc32,          // common_firmware_header.crc32 (offset 28): uni_mes carries ucode_version 0
};
struct FwDesc { uint32_t type, version; };
struct FwSpec {
    const char *name;           // firmware/<name>.bin, symbol fw_<name>
    uint8_t ipMajor, ipMinor;   // common_firmware_header.ip_version_major / _minor
    FwCheck how;
    uint32_t value;             // kByUcodeVersion / kByCrc32
    uint8_t nDesc;              // kByDescriptors
    FwDesc desc[3];
};
static constexpr FwSpec kFirmware[] = {
    { "gc_12_0_0_imu",     12, 0, kByUcodeVersion, 0x0c2c2500u, 0, {} },
    { "gc_12_0_0_me",      12, 0, kByUcodeVersion, 0x00000bc2u, 0, {} },
    { "gc_12_0_0_mec",     12, 0, kByUcodeVersion, 0x00000d0cu, 0, {} },
    { "gc_12_0_0_pfp",     12, 0, kByUcodeVersion, 0x00000c12u, 0, {} },
    { "gc_12_0_0_rlc",     12, 0, kByUcodeVersion, 0x00bde160u, 0, {} },
    { "gc_12_0_0_uni_mes", 12, 0, kByCrc32,        0x1a2b3f08u, 0, {} },   // reports MES 0x8b only at runtime
    { "psp_14_0_2_sos",    14, 0, kByDescriptors,  0,           1, { { kPspFwSos, kSosVersion } } },
    { "psp_14_0_2_ta",     14, 0, kByDescriptors,  0,           3, { { kPspFwAsd, 0x21000110u }, { kPspFwHdcp, 0x17000050u }, { kPspFwDtm, 0x1200001du } } },
    { "sdma_7_0_0",         7, 0, kByUcodeVersion, 0x00798e96u, 0, {} },
    { "smu_14_0_2",        14, 0, kByUcodeVersion, kSmuVersion, 0, {} },
};
static constexpr size_t kFirmwareCount = sizeof(kFirmware) / sizeof(kFirmware[0]);

static constexpr bool str_eq(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static inline const FwSpec *find_firmware(const char *name) {
    if (!name) return nullptr;
    for (const FwSpec &f : kFirmware) if (str_eq(f.name, name)) return &f;
    return nullptr;
}

// One blob against its pin: kOk or the first thing wrong (name, length, IP, SOS floor, version).
static inline uint32_t check_firmware(const FwSpec &f, const uint8_t *d, size_t len) {
    if (!d || len < 32 || rd32(d) != len) return kFwTruncated;
    if (rd16(d + 12) != f.ipMajor || rd16(d + 14) != f.ipMinor) return kFwIpMismatch;
    if (f.how == kByUcodeVersion) return rd32(d + 16) == f.value ? kOk : kFwVersionMismatch;
    if (f.how == kByCrc32)        return rd32(d + 28) == f.value ? kOk : kFwVersionMismatch;
    if (len < 36) return kFwTruncated;
    const uint32_t count = rd32(d + 32);
    if (count > (len - 36) / 16) return kFwTruncated;
    for (uint8_t i = 0; i < f.nDesc; i++) {
        bool found = false; uint32_t got = 0;
        for (uint32_t k = 0; k < count && !found; k++) {
            const uint8_t *e = d + 36 + 16 * k;
            if (rd32(e) == f.desc[i].type) { found = true; got = rd32(e + 4); }
        }
        if (!found) return kFwNoDescriptor;
        if (f.desc[i].type == kPspFwSos && str_eq(f.name, "psp_14_0_2_sos") && got < kSosMinForReservation) return kFwSosTooOld;
        if (got != f.desc[i].version) return kFwVersionMismatch;
    }
    return kOk;
}
static inline uint32_t check_firmware(const char *name, const uint8_t *d, size_t len) {
    const FwSpec *f = find_firmware(name);
    return f ? check_firmware(*f, d, len) : kFwUnknownName;
}

// The whole set. load(name, &data, &len) returns false for an absent blob. index names the first failing kFirmware row.
struct FwSetResult { uint32_t reason; size_t index; };
template <class Load>
static inline FwSetResult check_firmware_set(Load load) {
    for (size_t i = 0; i < kFirmwareCount; i++) {
        const uint8_t *d = nullptr; size_t len = 0;
        if (!load(kFirmware[i].name, &d, &len)) return { kFwMissing, i };
        const uint32_t r = check_firmware(kFirmware[i], d, len);
        if (r != kOk) return { r, i };
    }
    return { kOk, kFirmwareCount };
}

// ---------------------------------------------------------------------------
// SOC (Linux soc24.c, GC 12.0.0): external_rev_id = rev_id + 0x40.
static constexpr uint32_t external_rev_id(uint8_t pciRevision) { return uint32_t(pciRevision) + 0x40u; }

} // namespace n44id
