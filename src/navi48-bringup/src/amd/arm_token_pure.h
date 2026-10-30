// arm_token_pure.h - the consume-once NVRAM arming token (ticket #16, spec #14).
//
// Why: on macOS 26.7.2 a third-party kext loads only from the Auxiliary Kernel Collection after a one-time
// approval, and from there IOKit matches it at EVERY boot. So the bring-up cannot rely on "never loaded at
// boot". Instead the kext stays INERT unless it is armed after login: the harness writes the NVRAM variable
// `navi48-arm=stage:N` and makes IOKit re-run matching; probe reads it, DELETES it before anything else and
// arms only if it is gone afterwards (consume-once), so a panic or reset always comes back unarmed.
// The navi48bringup / navi48-stage boot-args no longer arm: a boot-arg survives a reboot, which is exactly
// what must not arm a kext that is matched at boot.
// Uptime floor: a token seen less than kMinUptimeS after boot is consumed but REFUSED. The boot-time match of the
// AuxKC kext happens well inside it, so a token left behind (the bench reset while armed, or a token set by hand
// whose re-match never ran) is deleted at the next boot without arming it. The harness arms after 120 s of uptime.
//
// Grammar: exactly "stage:N", N = 1..17 in decimal with no leading zero (amdgpu::BringupStage, 17 =
// ComputeDispatch), optionally followed by ONE NUL. Anything else is refused (fail closed).
// Freestanding: <stdint.h> / <stddef.h> only, no allocation, no kernel calls. Compiled into the kext and the host test.
#pragma once
#include <stdint.h>
#include <stddef.h>

namespace n48arm {

static constexpr const char *kNvramKey = "navi48-arm";
static constexpr uint32_t kStageMin = 1;
static constexpr uint32_t kStageMax = 17;          // amdgpu::BringupStage::Max (static_assert in Navi48Bringup.cpp)
static constexpr size_t   kMaxTokenLen = 9;        // "stage:17" + one NUL; probe copies at most kCopyLen bytes
static constexpr size_t   kCopyLen = 32;           // > kMaxTokenLen, so a longer variable still reads as too long
static constexpr uint64_t kMinUptimeS = 90;        // seconds since boot before a token may arm

enum Reason : uint32_t {
    kArmed = 0,
    kAbsent,           // no navi48-arm variable (the normal boot)
    kNotConsumed,      // the variable was there but is still there after the delete: never arm
    kMalformed,        // not "stage:N"
    kStageOutOfRange,  // "stage:N" with N outside 1..17
    kTooEarly,         // seen less than kMinUptimeS after boot: a token left over from before a reset
};
static inline const char *reason_name(uint32_t r) {
    switch (r) {
    case kArmed:           return "armed";
    case kAbsent:          return "no navi48-arm token in NVRAM";
    case kNotConsumed:     return "the token could not be deleted from NVRAM";
    case kMalformed:       return "malformed token (want stage:N)";
    case kStageOutOfRange: return "stage out of range (want 1..17)";
    case kTooEarly:        return "token seen within 90 s of boot (left over from before a reset)";
    default:               return "unknown";
    }
}

struct Decision { bool armed; uint32_t stage; uint32_t reason; };

// present:  the variable existed when probe read it;  value/len: its bytes (as copied, at most kCopyLen);
// consumed: after probe deleted it, a second read found nothing;  uptime_s: whole seconds since boot.
static inline Decision decide(bool present, const uint8_t *value, size_t len, bool consumed, uint64_t uptime_s) {
    const Decision no = { false, 0, kMalformed };
    if (!present) return { false, 0, kAbsent };
    if (!consumed) return { false, 0, kNotConsumed };
    if (uptime_s < kMinUptimeS) return { false, 0, kTooEarly };
    if (!value || len == 0 || len > kMaxTokenLen) return no;
    if (value[len - 1] == 0) len--;                 // one trailing NUL
    static const char kPrefix[] = "stage:";
    const size_t p = sizeof(kPrefix) - 1;
    if (len < p + 1 || len > p + 2) return no;
    for (size_t i = 0; i < p; i++) if (value[i] != static_cast<uint8_t>(kPrefix[i])) return no;
    uint32_t n = 0;
    for (size_t i = p; i < len; i++) {
        if (value[i] < '0' || value[i] > '9') return no;
        n = n * 10 + (value[i] - '0');
    }
    if (len == p + 2 && value[p] == '0') return no;  // no leading zero
    if (n < kStageMin || n > kStageMax) return { false, 0, kStageOutOfRange };
    return { true, n, kArmed };
}

} // namespace n48arm
