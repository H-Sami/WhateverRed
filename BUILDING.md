# Building the bring-up kext

> **WARNING.** Navi48Bringup.kext writes GPU registers directly (PCIe BARs, PSP, SMU, RLC, SDMA, CP,
> MES, display engine). It is experimental, it has only been run on one x86_64 test PC with one
> RX 9070 XT, and a wrong build or a different card can hang or restart the machine or corrupt
> display state. Do not run it on a machine you cannot afford to restart or recover. You run
> it entirely at your own risk; there is no warranty (see LICENSE).

## Prerequisites

- macOS with the Xcode Command Line Tools (clang, make, codesign, python3).
- MacKernelSDK, taken from the RDNA4FB clone (not vendored here):

  ```
  git clone https://github.com/somestupidgirl/RDNA4FB
  ```

  and pass its MacKernelSDK directory as MKSDK below.
- AMD firmware from linux-firmware (not in this repo, AMD's own licence):

  ```
  git clone https://gitlab.com/kernel-firmware/linux-firmware.git
  git -C linux-firmware checkout 2a12d06438f2092115b9cf53dca79258bf283f88
  tools/fetch-firmware.sh <path-to-that-checkout>
  ```

  This only copies files (no network) into `src/navi48-bringup/firmware/`, which is
  gitignored. The kext embeds exactly these ten files from linux-firmware's `amdgpu/`
  (the Navi 44 / RX 9060 XT set: GC 12.0.0, PSP 14.0.2, SMU 14.0.2, SDMA 7.0.0):

  ```
  gc_12_0_0_imu.bin  gc_12_0_0_me.bin  gc_12_0_0_mec.bin  gc_12_0_0_pfp.bin
  gc_12_0_0_rlc.bin  gc_12_0_0_uni_mes.bin
  psp_14_0_2_sos.bin  psp_14_0_2_ta.bin
  sdma_7_0_0.bin  smu_14_0_2.bin
  ```

  The set is pinned to linux-firmware commit `2a12d06438f2092115b9cf53dca79258bf283f88`,
  whose versions match Linux on this card (SMU 102.70.0, SOS 0x3b0f0d). `tools/fetch-firmware.sh`
  refuses any copy whose sha256 differs, and `make` runs `check-firmware` first: a host
  checker built from `src/amd/navi44_identity_pure.h` (`n44id::kFirmware`) that stops the
  build on a missing blob or a version other than the pinned one. `make hosttest` then
  runs the firmware-extraction host test against the same blobs.

## Build

```
make -C src/navi48-bringup MKSDK=/path/to/RDNA4FB/MacKernelSDK all
```

The result is `src/navi48-bringup/build/Navi48Bringup.kext` (x86_64, ad-hoc signed, min macOS
11 ABI). Check it with: `dwarfdump --uuid build/Navi48Bringup.kext/Contents/MacOS/Navi48Bringup`
and `nm -u -arch x86_64 <that binary>` (only kernel, IOKit and KPI symbols).

## Optional pieces that are NOT in this repository

The shader cache (`src/shadercache/shadercache.c` is only its parser) and four gfx1201 blit
kernels serve the older route that substitutes shaders inside Apple's own AMDRadeonX6000
accelerator. They derive from Apple's shader binaries and captures and are therefore left
out. The native route (Mesa RADV plus the Metal bundle in `tools/native`) does not use them.
When `firmware/shadercache.bin` and `firmware/shader_blit_*.bin` are absent the Makefile embeds
zero-length blobs under the same symbols. The cache then refuses to open ("sc_open ...
REFUSED ... substitution stays OFF") and every blit-kernel substitution is refused, so
nothing is substituted or armed. Likewise the translator self-tests in `src/xlat12` need
captured Apple IB streams that are not shipped; without them the self-tests report a failure
and the render-translation arms refuse. Everything else, including the compute test shaders
(`src/navi48-bringup/shaders/store_magic*.s` with their assembled `.bin`), is in the tree.

Boot-args and loading are described in the source comments; this file only covers building.
