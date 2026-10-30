#!/bin/bash
#
# fetch-firmware.sh - copy the AMD firmware the Navi 44 (RX 9060 XT) bring-up kext embeds from a
# local linux-firmware checkout into src/navi48-bringup/firmware/ (gitignored; never commit it).
#
# Ticket #17: the set is pinned to ONE linux-firmware commit, the one whose versions match the Linux
# ground truth on this card (SMU 102.70.0, SOS 0x3b0f0d, ME 0xbc2, PFP 0xc12, MEC 0xd0c, ...):
#     2a12d06438f2092115b9cf53dca79258bf283f88  ("amdgpu: update PSP 14.0.2 firmware", 2026-05-07)
# Later commits carry other versions (e.g. SMU 102.71.0) and are refused: a sha256 mismatch is an error.
# The same pin, with the per-blob versions, is n44id::kFirmware in src/navi48-bringup/src/amd/navi44_identity_pure.h,
# which `make check-firmware` enforces at build time.
#
# This script does NOT use the network. Check the commit out yourself first:
#     git clone https://gitlab.com/kernel-firmware/linux-firmware.git
#     git -C linux-firmware checkout 2a12d06438f2092115b9cf53dca79258bf283f88
#   (mirror: https://git.kernel.org/pub/scm/linux/kernel/git/firmware/linux-firmware.git)
# Any directory with these blobs under amdgpu/ works (a sparse copy of just the ten files is enough).
#
# Usage:  tools/fetch-firmware.sh <path-to-linux-firmware-checkout>
#
# The blobs are AMD's, under AMD's redistribution licence (LICENSE.amdgpu in linux-firmware).
set -euo pipefail

PINNED_COMMIT=2a12d06438f2092115b9cf53dca79258bf283f88

if [ $# -ne 1 ] || [ ! -d "$1" ]; then
    echo "usage: $0 <path-to-linux-firmware-checkout at $PINNED_COMMIT>" >&2
    echo "clone it first: git clone https://gitlab.com/kernel-firmware/linux-firmware.git && git -C linux-firmware checkout $PINNED_COMMIT" >&2
    exit 2
fi
SRC="$1/amdgpu"
[ -d "$SRC" ] || { echo "fetch-firmware.sh: no amdgpu/ directory in $1" >&2; exit 2; }

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DST="$REPO/src/navi48-bringup/firmware"
mkdir -p "$DST"

# name  sha256 at linux-firmware $PINNED_COMMIT (upstream's GC 12.0.0 / PSP 14.0.2 / SMU 14.0.2 / SDMA 7.0.0
# names, the unified MES blob, no kicker blobs)
BLOBS="
gc_12_0_0_imu     aa15e5b3156bffc45e0c50bccbcd364fbd3f958531b695b7487a803d780b8328
gc_12_0_0_me      1fbb97678cba5f18590ce1e13c8d0057eaafcca7654bc578e840aac465137eb3
gc_12_0_0_mec     fb03a3077febd8b0884288bb7d86824eeffa4721f4088025c80d6dd917e22a68
gc_12_0_0_pfp     1bec9d532b2d3c1f309d44fbabacda96d5cbfad186023f13358765948fa1ac4f
gc_12_0_0_rlc     6436b582734a413456fff3d3c7195e71cc9e78a7ed31ee21c83ffd6fae1ad186
gc_12_0_0_uni_mes bf26e0300989ebd011e991d76396e2c1673e1caa502fb6d8413e387eb1456676
psp_14_0_2_sos    7b538448b57d4f9dd06b2eea90d4f86a16e65e3027cdecee8db71c2c5f1fa243
psp_14_0_2_ta     5d45532c0e75b79c72ac3a4e06a7cc36c9d4ca5dbb59d98f94106db606aae1cf
sdma_7_0_0        beaafb53993a106edd392392d5896245ae2a957c6d0f495d0002eec72ad8ad38
smu_14_0_2        1fe947dbcca7bfdb3597cdeab0fce1597ffb673ebc9332eb9072981c89d9f9a9
"

bad=0
while read -r name want; do
    [ -n "$name" ] || continue
    if [ -f "$SRC/$name.bin" ]; then
        cp "$SRC/$name.bin" "$DST/$name.bin"
    elif [ -f "$SRC/$name.bin.zst" ] && command -v zstd >/dev/null 2>&1; then
        zstd -dqf -o "$DST/$name.bin" "$SRC/$name.bin.zst"
    else
        echo "MISSING  $name.bin (not in $SRC)" >&2
        exit 1
    fi
    got="$(shasum -a 256 "$DST/$name.bin" | awk '{print $1}')"
    if [ "$got" = "$want" ]; then echo "ok       $name.bin ($(wc -c < "$DST/$name.bin" | tr -d ' ') bytes)"
    else
        echo "DIFFERS  $name.bin (sha256 $got; pinned $want) - removed" >&2
        rm -f "$DST/$name.bin"; bad=1
    fi
done <<< "$BLOBS"
if [ "$bad" -ne 0 ]; then
    echo "fetch-firmware.sh: not linux-firmware $PINNED_COMMIT (check it out: git -C $1 checkout $PINNED_COMMIT)" >&2
    exit 1
fi
cp "$1/LICENSE.amdgpu" "$DST/" 2>/dev/null || true
echo "copied the linux-firmware $PINNED_COMMIT set into $DST"
