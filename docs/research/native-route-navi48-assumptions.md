# The native route and where it assumes Navi 48

Research for H-Sami/WhateverRed#4, dated 2026-10-29.

**Question:** Which parts of Cobalt64 does the desktop path run, and where does each part assume Navi 48? The goal is to port to the RX 9060 XT (Navi 44, PCI `1002:7590`, gfx1200, GC 12.0.0, 2 SE / 32 CU, 128-bit 16 GB GDDR6). The owner has one 2560x1440 DisplayPort monitor on an ASUS board.

**Source:** this repository at the tree imported from Cobalt64, kext 0.0.664 and Metal bundle build 20 (`CHANGES.md`). Every claim cites `path:line` in this tree. Linux-side facts about Navi 44 (its IP versions and firmware names) are marked **(Linux)**. They come from upstream amdgpu, not from this repo, and should be confirmed by the Navi 44 vs Navi 48 research ticket.

"Native route" means Metal on the macOS port of RADV, running on our own kext. It does not use Apple's `AMDRadeonX6000` to drive the GPU. The substitution / Phase-4 route is out of scope (`GLOSSARY.md:104`).

---

## 1. Layer diagram

```
 WindowServer / apps (Metal)
        |  MTLDevice from the Metal plug-in named by the IOAccelerator's MetalPluginName
        v
 Navi48Metal.bundle  (tools/native/navi48metal, class Navi48Device)          [user space]
   |- libn48xlate.dylib   AIR -> SPIR-V in process  (third-party/metal2vulkan-n48, LGPL)
   |- spvcache/*.spv      pre-translated SPIR-V     (NOT in the tree)
   '- libvulkan_radeon.dylib  RADV + 9 Darwin patches (mesa-patches/, Mesa f5cb8ee0)
        |  IOServiceOpen("Navi48Bringup" | "Navi48Accelerator", type 'N48N')
        v
 N48N native user client  (src/navi48-bringup/src/Navi48NativeABI.h, ABI 1.11/1.12)   [kernel]
        |
 Navi48Bringup.kext  (OpenCore-injected, matches 1002:7550/7551)
   probe -> start: BAR5 map, RCC_CONFIG_MEMSIZE, on-die IP discovery, survey
   -> stagePSP -> runStages 1..17 (IH, GMC, PSP/SOS, TMR, FW load, SMU, IMU, RLC, CP, MES,
      GFX, SDMA, PM4 test, compute dispatch) -> native S1b self-test
   -> dcn/ (n48dcn attach, DMUB commands, disp2 HDMI chains)  + src/dcn41 register layer
   -> on demand (N48N selector 19): Navi48MetalNub  ("n48.metal.ops")
   -> Navi48DisplayNub x2 (navi48-fb2, for the two HDMI monitors only)
   -> amd/native_disp.cpp: pipe adopt / AGDC publish / fbname / pipearm on Apple's display machine
   [linked but idle on this route: apple/AppleHardwareHook, AppleTtlHook, Navi48Ttl,
    Navi48AccelPeer, src/xlat12, src/shadercache]
        |
 Navi48Accel.kext (aux KC, tools/native/navi48accel)
   Navi48Accelerator  : IOAcceleratorFamily2 subclass on Navi48MetalNub, MetalPluginName=Navi48Metal
   Navi48Framebuffer/2: IOGraphicsFamily framebuffers on Navi48DisplayNub index 1/2
        |
 RDNA4FB.kext (external, MODIFIED build that also registers as AMDRDNA4FB)
   the DP console framebuffer = display instance 0 / OTG0; the native route attaches to it
        |
 Apple IOAcceleratorFamily2 display machine/pipe + AppleGraphicsDeviceControl (AGDC)
        |
 tools/pc/arm-gpu-desktop.sh (1 DP) or tools/pc/autoarm (3 displays) at the login window:
   n48nub publish -> pipeadopt -> pipeagdc 1 -> fbname 1 -> pipearm 1 -> pipereload -> killall WindowServer
```

### 1.1 Each layer

**Bring-up kext (`src/navi48-bringup`)**
- `probe()` does nothing unless `navi48bringup=1` is set (`src/navi48-bringup/src/Navi48Bringup.cpp:7329`).
- It then refuses any device other than `0x7550`/`0x7551` (`Navi48Bringup.cpp:7333-7337`).
- `start()` runs these steps (`Navi48Bringup.cpp:7342-7490`):
  - maps BAR5 and reads VRAM size (`probeMemSize`, `Navi48Bringup.cpp:470-484`);
  - loads the on-die IP discovery table from 64 KiB below the top of VRAM (`Navi48Bringup.cpp:486-500`, `src/navi48-bringup/src/regs.hpp:60`);
  - surveys IPs, PSP and SMU;
  - runs `stagePSP` (`Navi48Bringup.cpp:571-610`), which builds the device context through `fill_ip_base_table` (`Navi48Bringup.cpp:616-637`);
  - runs the stage ladder `runStages` (`Navi48Bringup.cpp:6918` onward).
- The stage enum runs from 1 (IPDiscovery) to 17 (ComputeDispatch) (`src/navi48-bringup/src/amd/amdgpu_init.h:33-52`).
- After the ladder, the native S1b gate runs `native_s1b_run` (`Navi48Bringup.cpp:6866-6900`, ~7108-7112).
- `newUserClient` returns the native client for type `N48N` and the legacy client for every other type (`Navi48Bringup.cpp` ~7139).

**N48N ABI.** The ABI is `src/navi48-bringup/src/Navi48NativeABI.h`. QueryInfo returns the live `GB_ADDR_CONFIG` and the GC version (`Navi48NativeABI.h:185`). M6 adds selectors 22-26 (`CHANGES.md:10-20`). The Mesa side carries a mirrored copy of this header (`mesa-patches/README.txt:135-137`).

**Metal nub and the aux kext.** `Navi48MetalNub` is published only on request through N48N selector 19, and only when `navi48-metal=1` is set on a native boot that passed S1b (`src/navi48-bringup/src/Navi48MetalNub.hpp:1-10`). The aux kext `Navi48Accel` (`tools/native/navi48accel/Info.plist`) matches it as `Navi48Accelerator`, which names the Metal plug-in `Navi48Metal` / `Navi48Device`. The bundle's sandbox gate opens either `Navi48Bringup` or `Navi48Accelerator` (`tools/native/navi48metal/n48_gate.h:63-65`).

**Display.**
- The DP monitor is lit by RDNA4FB. It is display instance 0 / OTG0 (`tools/native/navi48metal/n48_m6route.h:9`).
- The native kext finds RDNA4FB under either name, `RDNA4FB` or `AMDRDNA4FB` (`src/navi48-bringup/src/amd/native_disp.cpp:395-405`).
- It then walks Apple's IOAcceleratorFamily2 object graph through raw field offsets: accelerator -> display machine -> pipe -> framebuffer (`native_disp.cpp:406-433`). The offsets are `kAccelProvider 0x368`, `kAccelDm 0x378`, `kDmPipes 0x88`, `kDmCount 0x108`, `kPipeAccel/Dm/Fb 0x88/0x90/0x98` (`src/navi48-bringup/src/amd/native_disp_pure.h:122-133`). The comments next to these offsets give addresses inside Apple's binary.
- It publishes `IOAccelDisplayPipeCapabilities` on the accelerator (`native_disp.cpp:486-488`) and records the adopted pipe as "Navi48DisplayPipe on RDNA4FB" (`native_disp.cpp:512`).
- AGDC publishing lives in `src/navi48-bringup/src/apple/DisplayPipeGuard.cpp`, gated by `navi48-agdc` (`DisplayPipeGuard.cpp:2013`). Its framebuffer allowlist is also `RDNA4FB`/`AMDRDNA4FB` (`DisplayPipeGuard.cpp:68-84`).

**Apple hooks: not needed on the native route.**
- `AppleHardwareHook`, `AppleTtlHook`, `Navi48Ttl` and `Navi48AccelPeer` serve the Phase-4 accel experiment. That experiment is gated by `navi48-accel-experiment` (`Navi48Bringup.cpp:7395-7480`, `Navi48AccelPeer.cpp:99`).
- With `navi48-native=1`, the S1b gate refuses `navi48-accel-experiment`, `navi48-boot-chain`, the sdma/srbm/vmfrag tests, `navi48-eop-bridge` and `navi48-shader-cache` (`Navi48Bringup.cpp:6866-6900`).
- The hook code is still compiled into the kext: the Makefile builds every `src/apple/*.cpp` (`src/navi48-bringup/Makefile:20`). Its Info.plist personalities `Navi48AppleAccelExperiment` and `Navi48AppleHWServices` are still shipped (`src/navi48-bringup/Info.plist:38-76`).
- What the native route does still take from Apple is display plumbing: IOAcceleratorFamily2, IOGraphicsFamily and AGDC. It does not use the `AMDRadeonX6000` hooks.

**xlat12 and shadercache.** `src/xlat12`, `src/shadercache` and `src/dcn41` are always linked into the kext (`src/navi48-bringup/Makefile:59-70`, `:99`, `:136-166`). `AppleHardwareHook.cpp:53` calls shadercache. Only the Apple-accelerator route uses xlat12 and shadercache. Their Apple-derived inputs are replaced by zero-length stubs (`BUILDING.md:157-167`, `Makefile:43-55`).

**RADV.**
- Mesa is pinned to `f5cb8ee032adabef599ae892ec4e42d796b84da9` (`mesa-patches/README.txt:120`) and carries nine patches (`CHANGES.md`).
- Patch 0001 adds a Darwin `ac_drm` backend. It answers device-info queries from the canned `gfx1201` table (`mesa-patches/0001-*.patch` ~2930, ~3243) and invents a PCI identity `1002:7550` rev C0 (`0001` ~4343-4348).
- Patch 0002 connects the backend to the kext through `IOServiceMatching("Navi48Bringup")` (`0002` ~1236).
- Patch 0008 adds `Navi48Accelerator` as the service for sandboxed apps.

**metal2vulkan-n48.** This is an LGPL-3.0 crate, built against metal2vulkan `43c46ac8` plus one patch (`third-party/metal2vulkan-n48/README.txt`). It has no GPU-specific code. The bundle loads it through `tools/native/navi48metal/n48_xlate.h` (`CHANGES.md:36-43`).

**autoarm / arm-gpu-desktop.**
- `tools/pc/arm-gpu-desktop.sh` is the single-DP recipe: "Arm the GPU (Metal) desktop on the DP" (`arm-gpu-desktop.sh:2`). Its steps are `n48nub publish` -> `pipeadopt` -> `pipeagdc 1` -> `fbname 1` -> `pipearm 1` -> `pipereload` -> `killall -9 WindowServer` (`arm-gpu-desktop.sh:9-22`).
- `tools/pc/autoarm/n48-autoarm.sh` is the three-display M6 version. It first lights the two HDMI monitors through DMUB and disp2 chains (`n48-autoarm.sh:185-212`), then calls `fbhold`/`fbpublish` (`:215-232`), then runs the same nub/adopt/agdc/arm sequence (`:234-268`).

### 1.2 Bring-up order and boot-arg gates

| # | Step | Gate (boot-arg) | Where |
|---|------|-----------------|-------|
| 0 | OpenCore injects the kext from the ESP | config.plist (not in tree) | `tools/pc/esp-kext.sh:5,36`, `tools/pc/esp-config.sh:10-13` |
| 1 | probe | `navi48bringup=1` and device 7550/7551 | `Navi48Bringup.cpp:7329-7337` |
| 2 | survey, SMU survey | `navi48-smu` | `Navi48Bringup.cpp:540` |
| 3 | stage ladder | `navi48-stage=N` (17 for the desktop), `navi48-psp`, `navi48-interrupts` (default 1) | `Navi48Bringup.cpp:7369,7379,571-610` |
| 4 | ladder options | `navi48-smu-full`/`smu-basic`, `mes-nonfatal`, `cp-legacy-rb`, `idle-clamp`, `kcq`, `hsa-abi`, `doorbell-legacy`, `gart-high`, `pdb0` | `Navi48Bringup.cpp:6923-6961` |
| 5 | native S1b self-test | `navi48-native=1` (refuses the Apple-path args) | `Navi48Bringup.cpp:6866-6900` |
| 6 | N48N client policy | `navi48-metal-ws`, `navi48-multisession`, `navi48-apps`, `navi48-appvis`, `navi48-g1`/`g5` | `Navi48NativeClient.cpp:64`, `native_s1c.cpp:1849-2062` |
| 7 | Metal nub | `navi48-metal=1` (+ `navi48-metal-fact`) | `Navi48MetalNub.cpp:38,214` |
| 8 | display verbs (adopt/agdc/arm) | `navi48-metal-disp=1`, `navi48-agdc`, `navi48-fbwc` | `native_disp.cpp:572`, `DisplayPipeGuard.cpp:2013,2539` |
| 9 | DMUB commands, HDMI chains, extra framebuffers | `navi48-dmubcmd`, `navi48-disp2`, `navi48-fb2`, `navi48-row120` | `dcn/navi48_dcn.cpp:195,210,2121`, `Navi48DisplayNub.cpp:55` |
| 10 | multi-display | `navi48-m6`, `navi48-m6flip`, `navi48-m6flip1` | `native_disp.cpp:584-604` |
| 11 | aux kext loads (user must click Allow) | none | `tools/native/navi48accel/Info.plist` |
| 12 | arm and restart WindowServer, 0 users | none | `tools/pc/arm-gpu-desktop.sh:8-22` |

The desktop boot-args come from a test that reads a config file missing from this tree (`src/navi48-bringup/tests/native_disp_test.cpp:1473-1475`):

`-v keepsyms=1 npci=0x2000 alcid=7 msgbuf=1048576 navi48bringup=1 navi48-stage=17 navi48-smu-full=1 navi48-native=1 navi48-metal=1 navi48-metal-disp=1 amfi_get_out_of_my_way=1 navi48-dmubcmd=1 navi48-disp2=1 navi48-fb2=1`

With one DP monitor, the HDMI-only pieces are not needed: `navi48-disp2`, `navi48-fb2`, `navi48-m6*`, and the autoarm chains. `navi48-dmubcmd` may still be needed; that is to be confirmed.

---

## 2. Navi 48 assumptions

Milestones: **B** = bring-up / compute test (stage 17), **D** = display, **R** = RADV, **W** = GPU-composited WindowServer desktop.

| # | Assumption | Where | Navi 44 reality | Blocks |
|---|-----------|-------|-----------------|--------|
| 1 | probe accepts only `0x7550`/`0x7551` | `src/navi48-bringup/src/Navi48Bringup.cpp:7333-7337` | `0x7590` | B (everything) |
| 2 | All four kext personalities match `0x75501002 0x75511002` | `src/navi48-bringup/Info.plist:32,47,69,86` | add `0x75901002` | B |
| 3 | Required firmware is `gc_12_0_1_{imu,me,mec,pfp,rlc,uni_mes}`, `psp_14_0_3_{sos,ta}`, `sdma_7_0_1`, `smu_14_0_3` | `src/navi48-bringup/Makefile:27-28`, `src/navi48-bringup/src/amd/fw_table.cpp:47-126`, `BUILDING.md:124-143` | **(Linux)** `gc_12_0_0_*`, `psp_14_0_2_*`, `sdma_7_0_0`, `smu_14_0_2` | B |
| 4 | SOS is parsed from the embedded `fw_psp_14_0_3_sos` | `Navi48Bringup.cpp:586` | psp_14_0_2 | B |
| 5 | IP version table pinned at compile time: GFX 12.0.1, MES 12.0.1, SDMA 7.0.1, PSP 14.0.3, SMU 14.0.3, GMC 12.0.0, NBIO 7.11.0, IH/HDP 7.0.0 | `src/navi48-bringup/src/amd/amdgpu_ip.h:48-56` | GC 12.0.0, SDMA 7.0.0, PSP/SMU 14.0.2 **(Linux)** | B |
| 6 | GC must be 12.0.1. A mismatch only logs a warning that "the hardcoded Navi 48 caps may not apply" | `src/navi48-bringup/src/amd/gfx_v12_0.cpp:397-401` | GC 12.0.0 | B (log only) |
| 7 | PSP path keyed on `kIP_PSP.major == 14` | `src/navi48-bringup/src/amd/psp_v14_0.cpp:733` | 14 as well, so no change needed | none |
| 8 | SMU driver-IF `0x2E`, which Linux shares between 14.0.2 and 14.0.3; a mismatch does not abort | `src/navi48-bringup/src/amd/amdgpu_smu.h:295-300,370-372`, `smu_v14_0.cpp:444-452` | same | none, likely |
| 9 | Die geometry hard-coded: 4 SE x 2 SA x 8 CU = 64 CU, 4 RB/SE = 16 RB, 8 HW contexts | `src/navi48-bringup/src/amd/gfx_v12_0.cpp:370-376`, `amdgpu_gfx.h:246-259` | 2 SE / 32 CU (RBs to be confirmed) | B. Harvest masks have the wrong width; a mismatch is only logged (`gfx_v12_0.cpp:445-460`) |
| 10 | CU bitmap array sized `[4][2]` | `src/navi48-bringup/src/amd/amdgpu_gfx.h:278-280` | fits (2x2), no overflow | none |
| 11 | Compute test programs `COMPUTE_STATIC_THREAD_MGMT_SE0..SE3` ("Navi 48 has 4 SEs") | `src/navi48-bringup/src/amd/compute_test.cpp:84-86` | SE2/SE3 do not exist; probably harmless | B (verify) |
| 12 | Live `GB_ADDR_CONFIG` expected to be `0x08200545` (log-only in the kext) | `src/navi48-bringup/src/amd/amdgpu_gfx.h:85-86`, `gfx_v12_0.cpp:129` | different (fewer SEs/pipes) | none in the kext |
| 13 | **Mesa refuses to open the device** unless live `GB_ADDR_CONFIG == 0x08200545`. The override is `RADV_DARWIN_ALLOW_ADDRCFG=1`. Patch 0007 keeps this check | `mesa-patches/0002-*.patch` ~385-393, ~1885-1886; `0007-*.patch:37` | fails | R |
| 14 | Mesa uses the canned `gfx1201` device table (geometry, CU masks, VA layout) and reports "AMD Radeon RX 9070 XT" | `mesa-patches/0001-*.patch` ~2884-2885, ~2930 | needs a gfx1200 / Navi 44 entry; check whether Mesa's `amdgpu_devices.c` has one | R |
| 15 | Mesa invents PCI `1002:7550` rev C0 | `mesa-patches/0001-*.patch` ~4343-4348 | `7590` | R |
| 16 | Mesa's mock and QueryInfo fallback return the gfx1201 `GB_ADDR_CONFIG` | `mesa-patches/0002-*.patch` ~2428, ~2457; `0003-*.patch:406,663` | wrong values | R |
| 17 | DCN segment bases must equal `0x12 / 0xc0 / 0x34c0 / 0x9000 / 0x2403c00`, or every display write is refused | `src/dcn41/dcn41.h:55-59`, `src/dcn41/dcn41_core.c:11-25`, `src/navi48-bringup/src/dcn/navi48_dcn.cpp:2848`, `dcn/navi48_disp2.h:120,142-144,295` | both chips are DCN 4.0.1, so the bases are probably equal, but this must be checked on the card's own discovery table | D |
| 18 | The DCN write allowlist and mode-trial tables are generated for those bases | `src/navi48-bringup/src/dcn/navi48_modetrial_tables.h:4`, `src/dcn41/dcn41_allow_ranges.h` | as row 17 | D |
| 19 | Connector map taken from the developer's board ROM: `DP/ddc0/hpd1, DP/ddc1/hpd2, HDMI/ddc2/hpd3, HDMI/ddc3/hpd4`. SINK-A DP = UNIPHY_B/DIGB, B = UNIPHY_C/DIGC, C = UNIPHY_D/DIGD | `tools/dcn41/gen_modes.py:31-38` | ASUS 9060 XT board layout differs | D |
| 20 | `dcn41_modes.tsv` holds the developer's three monitors' EDID modes only: DP 2560x1440 @60/120/144/165 and 1280x1440; HDMI 1920x1080 @60/120; HDMI 3840x2160 @60/120 | `src/dcn41/dcn41_modes.tsv:3-11`, generated from `edid-capture.txt` (`tools/dcn41/gen_modes.py:7-10,27`) | the owner's 1440p DP has different timings | D (for anything the kext programs) |
| 21 | `SYMCLKB` is "the LIVE DP's symbol clock", so writes to it are forbidden | `src/navi48-bringup/src/dcn/navi48_disp2.h:286` | the DP may sit on a different PHY | D |
| 22 | Display instance mapping: 0 = DP (RDNA4FB, OTG0), 1 = monitor A (OTG1), 2 = monitor B (OTG2); EDID keys `DDC2`/`DDC3` | `src/navi48-bringup/src/amd/native_m6_pure.h:22`, `native_fb_pure.h:34`, `tools/native/navi48metal/n48_m6route.h:9` | only instance 0 exists | D/W (use the non-M6 path) |
| 23 | autoarm expects the DP `OTG0_H_TOTAL` to be `0xa9f` (1440p60), falling back to `0x897` (1080p), and drives monitor B then monitor A over HDMI | `tools/pc/autoarm/n48-autoarm.sh:26,147-150,185-212,247` | use `arm-gpu-desktop.sh` | W |
| 24 | Hard-coded paths `~/navi48-staging/...`, `/Users/testuser/...`, `~/n48-metal/n48nub` | `tools/pc/autoarm/n48-autoarm.sh:19-21`, `tools/pc/arm-gpu-desktop.sh:8-10`, `esp-config.sh:13`, `esp-kext.sh:36` | local paths | W (tooling) |
| 25 | VRAM size read live from `RCC_CONFIG_MEMSIZE` (discovery offset or fallback `0x378c`); sanity range 1-64 GiB | `Navi48Bringup.cpp:470-484`, `regs.hpp:27-29` | 16 GiB works | none |
| 26 | The GFXHUB PDB0 alias covers MC [0, 16 GiB) with two 8 GiB leaves | `src/navi48-bringup/src/amd/amdgpu_gmc.h:66-80` | 16 GB is fine; an 8 GB card would not be | none (16 GB) |
| 27 | High-VRAM tail reserve of 512 MiB is sized for "this card's" TMR (~0x3f860_0000) and discovery table | `src/navi48-bringup/src/amd/amdgpu_gmc.h:53-63` | 16 GB with the same layout is likely fine | B (verify) |
| 28 | BAR0 mapped up to 1 GiB; ReBAR planning | `src/navi48-bringup/src/amd/rebar_pure.h:19-20,80-85` | not chip-specific | none |
| 29 | PCIe link speed/width | no assumption found (grep found no gen/width checks in the kext) | x8 on Navi 44 is fine | none |
| 30 | Shader blobs named `*_gfx1201` and the Apple shader cache | `src/navi48-bringup/Makefile:43-55`, `BUILDING.md:157-167` | used only on the Apple route | none (native) |
| 31 | IP register bases come from on-die discovery, not constants | `src/navi48-bringup/src/amd/amdgpu_discovery.cpp:36-52` | portable | none |
| 32 | Apple IOAcceleratorFamily2 field offsets (`0x368/0x378/0x88/0x108/0x98`) and aux-kext vtable headers are tied to the developer's macOS build | `src/navi48-bringup/src/amd/native_disp_pure.h:122-133`, `tools/native/navi48accel/Makefile:16,22`, `tools/native/navi48accel/verify.sh` (26.6.2 / 25G83) | depends on the macOS build, not the GPU | W |
| 33 | RDNA4FB (external) must support the card and be the modified `AMDRDNA4FB`-capable build | `DisplayPipeGuard.cpp:68-84,1532`, `native_disp.cpp:395-405` | unknown whether RDNA4FB matches `7590` | D/W |

The Metal bundle itself has no Navi 48 constants. A grep of `tools/native/navi48metal` for 7550, gfx1201, 9070, 2560 and 1440 finds only names. The bundle inherits whatever device RADV reports.

---

## 3. What the tree is missing, and whether it builds from a clean clone

### 3.1 Referenced but absent

| Missing | Referenced by |
|---------|---------------|
| `variants/configs/*.plist` (all boot configs, including the desktop one) | `src/navi48-bringup/tests/native_disp_test.cpp:1473,1565,1845`; `tools/pc/esp-config.sh:10-13` (`~/navi48-staging/configs`) |
| `INSTALL.md` | `tools/native/navi48metal/build.sh:46`; `BUILDING.md:169` says boot-args are "described in the source comments" |
| `notes/` (DISPLAY-DESIGN.md, REBAR.md, re/), `NATIVE-S1C-ABI.md` | `tools/dcn41/gen_modes.py:33`, `rebar_pure.h:20`, `amdgpu_gmc.h:66`, `mesa-patches/0002-*.patch:1289` |
| `edid-capture.txt`, `framebuffer-pre.txt` (developer EDIDs and AtomBIOS dump) | `tools/dcn41/gen_modes.py:27,31` |
| `src/RDNA4FB` (modified RDNA4FB plus its MacKernelSDK) | `src/navi48-bringup/Makefile:11`, `tools/native/navi48accel/Makefile:10`, `DisplayPipeGuard.cpp:72-73,1532`, `BUILDING.md:117-123` |
| AMD firmware (`src/navi48-bringup/firmware/`, generated `src/fw/fw_*.c`) | `Makefile:27-28,114-119`, `.gitignore`; fetched by `tools/fetch-firmware.sh` |
| `tools/native/navi48metal/spvcache/*.spv`, `*.meta.json` | `tools/native/navi48metal/build.sh:31` |
| `tools/ioaccel-layout/generated/IOAccelFamily2_decl.h`, `model.json`, `forwarded.json`, `slots.json` | `tools/native/navi48accel/Makefile:16,22` |
| `shadercache.bin`, `shader_blit_*_gfx1201` blobs, xlat12 IB captures | `BUILDING.md:157-167`, `Makefile:43-55` (stubbed) |
| Apple-derived fixtures, notes | `CHANGES.md:72-73` |

### 3.2 Clean-clone build status

**Kext (`make -C src/navi48-bringup MKSDK=... all`, `BUILDING.md:148`)**
- It builds on macOS only, because it uses `xcrun` (`Makefile:10`).
- It needs MacKernelSDK. The default path `../RDNA4FB/MacKernelSDK` is missing (`Makefile:11`), so it must be passed in.
- It needs the ten firmware files (`Makefile:114-119`).
- The optional Apple blobs are stubbed (`Makefile:43-55`).
- With MKSDK and firmware supplied, it builds. For Navi 44 it also needs the firmware-name changes in rows 3-5 of the table.

**Navi48Metal.bundle (`tools/native/navi48metal/build.sh`)**
- It needs a built RADV dylib at `$HOME/navi48-native/mesa-mac/...` (`build.sh:13`), the n48xlate build (`:17-24`), and Homebrew Vulkan headers (`:26`).
- **It fails on a clean clone.** The `spvcache/*.spv` glob at `build.sh:31` matches nothing, so `cat` exits non-zero and `set -euo pipefail` aborts the script.
- The spvcache is Apple-derived, because it is translated from Apple's AIR. `fill-spvcache.py` can regenerate it.

**Navi48Accel.kext (`tools/native/navi48accel/Makefile`)**
- **It fails on a clean clone.** It needs `src/RDNA4FB/MacKernelSDK` (`Makefile:10`) and the `ioaccel-layout/generated/` headers (`:16,22`).
- Those headers are reconstructed from the user's own Apple kernel collections, and the result must not be redistributed (`tools/ioaccel-layout/README.md`).
- Carving the kexts out of the KC is a manual step. The README's default output directory is `layout-out/`, not `generated/`.

**Mesa**
- It builds from upstream `f5cb8ee0` with the nine patches applied (`mesa-patches/README.txt:120`) and a cross file for x86_64 (patch 0001).
- It needs no Apple inputs. For Navi 44 it needs rows 13-16.

**libn48xlate**
- It builds from public metal2vulkan `43c46ac8` plus the patch (`third-party/metal2vulkan-n48/README.txt`).

### 3.3 Pieces that need Apple-derived inputs on the native route

1. The aux kext's IOAcceleratorFamily2 layout headers, generated from the user's KC.
2. The hard-coded IOAccelFamily2 field offsets in `native_disp_pure.h:122-133`. These are tied to the macOS build; `verify.sh` pins 26.6.2 / 25G83.
3. The bundle's spvcache.

The native route does **not** need the Apple shader cache, the blit blobs, or the xlat12 captures.

---

## 4. Porting implications

- **Bring-up (B):** rows 1-6 and 9.
  - Add `0x7590`.
  - Switch to the GC 12.0.0 / PSP 14.0.2 / SMU 14.0.2 / SDMA 7.0.0 firmware.
  - Take the geometry from the discovery GC info table instead of constants, or pin 2 SE.
- **RADV (R):** rows 13-16. This needs a gfx1200 canned device, the real PCI ID, and a `GB_ADDR_CONFIG` gate that is chip-aware or removed.
- **Display (D):**
  - Use only instance 0 (DP via RDNA4FB) and skip all of disp2, fb2 and M6.
  - Check the DCN segment bases (row 17) and whether RDNA4FB supports 7590 (row 33).
  - The owner's one monitor matches the developer's DP resolution (2560x1440). The fixed mode tables matter only if the kext programs timing itself.
- **Desktop (W):**
  - Use `arm-gpu-desktop.sh`, not autoarm.
  - Regenerate the IOAccelFamily2 layout headers for the owner's macOS build.
  - Supply or regenerate the spvcache.
  - Recreate the missing boot config from the boot-args in section 1.2.
