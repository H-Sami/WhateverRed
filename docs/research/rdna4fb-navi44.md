# RDNA4FB on Navi 44, and the modified build Cobalt64 expects

Research for H-Sami/WhateverRed#10, dated 2026-10-29.

**Question:** Does RDNA4FB (github.com/somestupidgirl/RDNA4FB) support Navi 44 (`1002:7590`)? What does it hard-code for Navi 48? Exactly which parts of RDNA4FB does our tree reach into, and what must the "modified build" provide?

**Sources:**
- RDNA4FB at `main` = `5ff69aa` (2026-08-21, 61 commits, one branch `main`, no tags). It was read as data and never built. Citations of the form `RDNA4FB:path:line` refer to that commit.
- RDNA4FB's GitHub metadata, read through `gh api repos/somestupidgirl/RDNA4FB` (issues, branches, license).
- This repository at the imported Cobalt64 tree (kext 0.0.664). Citations are `path:line` in this tree.
- Prior research: `docs/research/native-route-navi48-assumptions.md` on branch `research/native-route-navi48-assumptions` (rows 22 and 33, and section 6).

---

## 1. Answer

1. **RDNA4FB does not support Navi 44 today.** The personality matches only `0x75501002 0x75511002` (`RDNA4FB:Info.plist:37-38`). `probe()` refuses any other device ID with "device id mismatch, not matching" (`RDNA4FB:src/framebuffer.cpp:2038-2045`). On the RX 9060 XT it will not attach, so there is no RDNA4FB framebuffer and nothing for Cobalt64's display code to find.
2. **The author's intent is to add Navi 44 later.** README: "Navi 44 (RX 9060, `0x7590`) shares DCN 4.1.0 and is a likely future addition once someone can test it" (`RDNA4FB:README.md:51-53`). Commit `d79f24d` (2026-07-14) says the same: "Navi 44 (0x7590, same DCN 4.1.0) is noted as a future addition pending test hardware." No branch, issue or later commit mentions Navi 44. The repo has one issue ever, #1 "License missing", which is closed. Its only branch is `main`.
3. **The code is mostly portable, beyond the ID gate.** Register bases come from IP discovery at runtime (section 3). Connector, DDC and HPD mapping comes from the VBIOS (section 3). The scanout is whatever the GOP lit up. The Navi 48 hard-codings that matter for one DP monitor are listed in section 3. The main risks are fixed DCN instance 0 offsets (pipe 0, OTG0, DP0 stream encoder) and a fixed NBIF `RCC_CONFIG_MEMSIZE` offset.
4. **No published RDNA4FB contains `AMDRDNA4FB` or `rdna4-amdname`.** Neither string appears in any of the 61 commits. `git log -S"AMDRDNA4FB"` and `git log -S"rdna4-amdname"` both return nothing. The "modified build" is private to the Cobalt64 developer. It lived at `src/RDNA4FB` in their tree and was not imported here (prior research section 6, row `src/RDNA4FB`).
5. **The license is BSD-3-Clause.** Copyright (c) 2026 Sunneva N. Mariu (`RDNA4FB:LICENSE:1-3`; GitHub `license.spdx_id` = `BSD-3-Clause`). It was added in `5ff69aa` after issue #1. Our tree records the same license (`src/navi48-bringup/NOTICE:5-6`). We may fork, patch and redistribute it with the notice kept.

---

## 2. What RDNA4FB matches and how

| Item | Value | Source |
|---|---|---|
| IOKit class | `RDNA4FB : IOFramebuffer` (only class in the kext) | `RDNA4FB:src/framebuffer.hpp:34-35`, `src/framebuffer.cpp:15` |
| Personality | key/IOClass `RDNA4FB`, provider `IOPCIDevice`, `IOPCIPrimaryMatch` `0x75501002 0x75511002`, `IOProbeScore` 200000, `IOMatchCategory` `IOFramebuffer` | `RDNA4FB:Info.plist:29-42` |
| Static `model` | "AMD Radeon RX 9070 Series" | `RDNA4FB:Info.plist:44-45` |
| Bundle | `com.hackintosh.RDNA4FB`, executable `RDNA4FB`, 0.0.1, `OSBundleRequired Root` | `RDNA4FB:Info.plist:7-24` |
| probe | `rdna4-off=1` kill switch; vendor must be `0x1002` and device `0x7550` or `0x7551`; then `score += 20000` | `RDNA4FB:src/framebuffer.cpp:2015-2051` |
| start model string | `0x7551` gives "AMD Radeon AI PRO R9700"; **anything else** gives "AMD Radeon RX 9070 XT", set as PCI `model` and `GPU,Variant` | `RDNA4FB:src/framebuffer.cpp:2096-2100` |
| History | class was `RX9070XTFB` and bundle `com.hackintosh.RX9070XT` until the rebrand in `8c54a16` (2026-07-12). The boot-args were renamed `rx9070xt-*` -> `rdna4-*` at the same time | commit `8c54a16` message and `Info.plist` diff |
| Build | `MacKernelSDK` is a **git submodule** (`acidanthera/MacKernelSDK`), not vendored files | `RDNA4FB:.gitmodules`, `Makefile:18` |

**Enabling 7590 needs edits in two places:**
- add `0x75901002` to `Info.plist:38`;
- add `device != 0x7590` to the check at `framebuffer.cpp:2042`.

The model string at `framebuffer.cpp:2097-2098` and the Info.plist `model` are cosmetic but would be wrong. Note that our own kext has the same ID gate (`src/navi48-bringup/Info.plist:31-32,46-47,68-69,85-86`), which is covered by the other tickets.

---

## 3. Navi 48 assumptions inside RDNA4FB

What is **not** hard-coded:
- **IP register bases.** `regReadDmu(baseIdx, dword)` translates every DCN register through the IP discovery table (`HwDmu`, instance 0) (`RDNA4FB:src/framebuffer.cpp:341-347`). The table comes from the VBIOS image (`ATY,bin_image`, `:90-102,205-221`) or from the on-die TMR copy at top of VRAM (`:248-299`). No IP version is checked anywhere. It only publishes `Discovery,GCVersion` and `Discovery,DCNVersion` (`:209-220,287-296`).
- **Connector, DDC and HPD mapping.** It is parsed from the AtomBIOS `DisplayObjectInfo` (format 1, content >= 4) and the GPIO LUT, then published as `AtomBIOS,Connectors` ("DisplayPort/ddc0/hpd1,...") (`RDNA4FB:src/framebuffer.cpp:160-185`, `src/atombios.cpp:217,232-235,310,345-347`). EDID is read on whichever AUX or DDC line the VBIOS names (`framebuffer.cpp:650-667`).
- **Mode and timing.** RDNA4FB does no mode setting. It adopts the GOP/OpenCore console (`v_baseAddr`, width, height, rowBytes) (`RDNA4FB:src/framebuffer.cpp:35-71`). It exposes exactly one mode, sized from the console, with the refresh rate always reported as 60 Hz (`:2231-2254`). It accepts only that mode (`:2320-2325`) and reports one connection (`:2363-2365`). The owner's 2560x1440@60 DP lit by GOP is therefore just "the console".

What **is** Navi 48 or developer-specific. All DCN offsets are dword offsets "from Linux dcn_4_1_0_offset.h" (`RDNA4FB:src/framebuffer.cpp:355-356`):

| # | Assumption | Source | Risk on the ASUS 9060 XT |
|---|---|---|---|
| R1 | Device IDs 7550/7551 only | `Info.plist:38`, `framebuffer.cpp:2042` | **Blocker.** It does not attach. |
| R2 | DCN register layout = DCN 4.1.0 / `dcn_4_1_0_offset.h`. The discovery on the developer's ROM reported "GC v12.0.1, DMU/DCN v4.1.0, NBIF v6.3.1" | `src/ipdiscovery.hpp:11-14`, `framebuffer.cpp:355-356,422-425` | The author claims Navi 44 has the same DCN (README:52-53, commit `d79f24d`). This is **not verified here**. Check `Discovery,DCNVersion` on the 9060 XT once it attaches. |
| R3 | `RCC_CONFIG_MEMSIZE` read at fixed BAR5 byte offset `0x378c` **before** discovery is available. It sizes VRAM to find the on-die discovery TMR | `framebuffer.cpp:257-271` | Correct only if Navi 44's NBIF layout matches. The fallback path needs no ROM injection, so this is the path that will run. Our kext uses the same constant (prior research row 25). |
| R4 | Boot pipe = pipe/instance 0: `OTG0_OTG_CONTROL 0x1b43`, OTG0 lock/double-buffer/VSTARTUP `0x1b5c,0x1b85-0x1b89`, HUBP0 primary address `0x060a/0x060b`, cursor plane 0 `0x0653,0x0679-0x0680`, DPP0 `0x0cc5` | `framebuffer.cpp:372,1576-1661,1730-1743` | Correct if GOP lights the single DP on pipe 0 / OTG0, as on the developer's card (our tree assumes the same: `tools/native/navi48metal/n48_m6route.h:9`). It must be confirmed on the ASUS board. Only the opt-in hardware cursor (`rdna4-hwcursor=1`) writes these. |
| R5 | **Display sleep assumes the DP0 stream encoder**: `DP_VID_STREAM_CNTL 0x2122` "the boot pipe". It blanks the stream and sends DPCD `SET_POWER` over the first AUX sink | `framebuffer.cpp:1453-1475`, `:711-720` ("that is AUX0/DP0 on this board") | This one **writes by default**: display sleep is on unless `rdna4-nosleep=1` (`framebuffer.cpp:2066-2070`). If the ASUS DP port is driven by a different DIG/stream encoder, sleep blanks the wrong encoder and the panel never sleeps or wakes correctly. Use `rdna4-nosleep=1` until this is checked with `rdna4-modedump=1`. |
| R6 | The diagnostics dump four DP stream encoders, four HUBPs and four DIG_FE at fixed per-instance strides | `framebuffer.cpp:365-370` | Read-only. Harmless if Navi 44 has fewer pipes (it just reads garbage). |
| R7 | AUX engine block `DP_AUX0 0x16b2`, stride `0x1c`; DC_I2C, DIO mem-power and `HPD_Y 0x28f7` offsets | `framebuffer.cpp:422-432,643,772-773` | Same layout if R2 holds. The instance comes from the VBIOS `ddcLine`. |
| R8 | 8 bpc experiment and color-channel map compensate an observed (G,B,R) rotation on the developer's setup | `framebuffer.hpp:48-62`, `framebuffer.cpp:1954-1959` | Both are opt-in boot-args (`rdna4-8bpc`, `rdna4-cmap`). The default is standard ARGB. |
| R9 | Cursor sprite placed inside a 256 MiB non-ReBAR BAR window (`192 MiB` guard) | `framebuffer.cpp:1697-1705` | Opt-in. It fits any 16 GB card's 256 MiB BAR0. |
| R10 | DMUB, SMU and PSP probes: DMUB inbox1 `0x01d5-0x01d7`, scratch `0x01e3`; MP1 `C2PMSG_66/82/90` via discovery base_idx 1; MP0 `C2PMSG_35/64/81`; DMUB fw compared against "Debian 0x00010300" | `framebuffer.cpp:1013-1030,1092-1110,1222,1271,1289-1290` | All gated off by default (`rdna4-dmub*`, `rdna4-smuping`, `rdna4-pspdump`, `rdna4-ihdump`). They are not needed. |
| R11 | `firmware/Sapphire.RX9070XT.16384.241213.rom` is the reference ROM for the parsers. The AtomBIOS offsets were "validated against the Navi 48 ROM" | `RDNA4FB:firmware/`, `src/atombios.cpp:11` | It is used only for host tests and tooling. On the card, the live expansion ROM or `ATY,bin_image` is parsed. |
| R12 | `model`/`GPU,Variant` strings name the 9070 / R9700 | `Info.plist:44-45`, `framebuffer.cpp:2096-2100` | Cosmetic. |

There is no fixed EDID or mode table in RDNA4FB. The developer's monitor-specific tables (`dcn41_modes.tsv`, `gen_modes.py`) live in **our** tree, not in RDNA4FB (prior research rows 19-20).

---

## 4. Every place our tree reaches into RDNA4FB

### 4a. Runtime identity: class names `RDNA4FB` / `AMDRDNA4FB`

| Our site | What it expects | RDNA4FB side |
|---|---|---|
| `src/navi48-bringup/src/apple/DisplayPipeGuard.cpp:68-85` (`dpg_is_our_fb_name`) | Allowlist of exactly `RDNA4FB` and `AMDRDNA4FB`. The comment says "src/RDNA4FB now ships BOTH `RDNA4FB` (the default) and `AMDRDNA4FB` (selected by boot-arg rdna4-amdname=1)" because CoreDisplay derives the GPU vendor from a case-sensitive "AMD" substring in the class name | Upstream has only `RDNA4FB` (`framebuffer.cpp:15`). **The second name and the boot-arg exist only in the private build.** |
| `DisplayPipeGuard.cpp:2038,2103-2113,2266,2492,2543-2550,2806,2938` | AGDC, scanout and `fbwc` paths look the framebuffer up by either name. `fbwc` registers a first-publish notification on both | same |
| `src/navi48-bringup/src/amd/native_disp.cpp:395-405,429` | `find_fb()` tries both names. `pipe+0x98` must be a framebuffer of either class (`fbOurs`) | same |
| `src/navi48-bringup/src/amd/native_agdc_pure.h:60-65`, `native_agdc_flow.h:21` | `fb_name_ok()` accepts both | same |
| `src/navi48-bringup/src/dcn/navi48_fbname.hpp:22-25`, `navi48_fbname.cpp:1-36,96-154` | **Our own runtime rename** (N48N action 78, `native_disp_pure.h:57`). It swaps the `className` OSSymbol pointer in `RDNA4FB`'s `gMetaClass` for "AMDRDNA4FB". It requires the live leaf class to be exactly `RDNA4FB`, an `IOFramebuffer` superclass link, and `gMetaClass` in writable `__DATA,__common` | Works against upstream's `OSDefineMetaClassAndStructors(RDNA4FB, IOFramebuffer)` (`framebuffer.cpp:15`), so it does not need the modified build. |
| `tools/pc/agdc-probe.c:258-264`, `tools/pc/fbbench.c:124-129`, `tools/pc/m4-adopt.c:38-41`, `tools/pc/m4-flush.c:40-43` | User-space tools try `AMDRDNA4FB` first, then `RDNA4FB`. `agdc-probe.c:259` says "Since 0.0.307 our framebuffer publishes as AMDRDNA4FB when boot-arg rdna4-amdname=1 is set" | private build |
| `tools/pc/accel-run.sh:1150-1153` | Log predicate matches `eventMessage CONTAINS "AMDRDNA4FB"` because "Every FBLOG line is prefixed 'AMDRDNA4FB: '" | Upstream prefix is `"RDNA4FB: "` (`framebuffer.cpp:13`). **So the private build also changed the log prefix.** The predicate misses every upstream log line. |
| `tools/pc/accel-run.sh:1534-1537,1856-1866`, `tools/pc/m6-stage1a-run.sh:412`, `m6-stage2-run.sh:36`, `tools/pc/display-topology.sh:6`, `tools/stage-to-pc.sh:13` | `ioreg -c RDNA4FB` / `-c IOFramebuffer`, `kmutil ... grep rdna4` | upstream class and bundle names |
| `tools/native/navi48accel/verify.sh:16` | "RDNA4FB still in the aux KC" | bundle name `RDNA4FB` (`Info.plist:8-14`) |

### 4b. IORegistry properties read from the RDNA4FB node

| Our site | Property | RDNA4FB side |
|---|---|---|
| `DisplayPipeGuard.cpp:2301,2326-2327,2494,2834-2835,2956-2958`, `src/navi48-bringup/src/Navi48Bringup.cpp:2396-2423` (`scanout_read_u64` at 2411), `src/navi48-bringup/src/apple/scanout_copy.h:38-43` | `Console,BaseAddress`, `Console,Length`, `Console,Width`, `Console,Height`, `Console,RowBytes`, `Console,Depth` | published from the GOP console (`framebuffer.cpp:35-71`). The base is masked `& ~0xFFF` (`:40`) |
| `src/navi48-bringup/src/Navi48DisplayNub.cpp:86,154-168`, `src/navi48-bringup/src/amd/native_fb_pure.h:34-35` | `EDID,DDC3` / `EDID,DDC2` (developer's HDMI monitors B and A) | `EDID,<bus><inst>` (`framebuffer.cpp:700-705`). The owner's DP monitor would appear as `EDID,AUX<n>`, which nothing of ours reads (out of scope: no HDMI, no M6). |
| `Navi48Bringup.cpp:6903` | `IOFBMemorySize` (About This Mac fallback) | IOFramebuffer default |

### 4c. Properties we write onto the RDNA4FB node

| Our site | What |
|---|---|
| `src/navi48-bringup/src/apple/Navi48AccelPeer.cpp:3384-3386` (verb `pairing`, `Navi48Bringup.cpp:7633-7640`) | `IOAccelTypes` (accelerator path), `IOAccelIndex` 0, `IOAccelRevision` 2. This is the display-pairing stamp, which is opt-in. |

### 4d. Binary and ABI reach-ins

| Our site | Assumption about RDNA4FB |
|---|---|
| `DisplayPipeGuard.cpp:28,2297,2418-2421,2469,2520` (`fbwc`), `display_pipe_guard.h:537` | RDNA4FB's vtable is exactly IOFramebuffer's 351 slots (`0xb08` bytes). Slot 310 (byte `0x9b0`) is `getVRAMRange`, which returns `IODeviceMemory::withRange(console base, len)`. We swap the descriptor. Upstream overrides `getVRAMRange` at `framebuffer.cpp:2212` (`.hpp:294`). The modified build must add **no new virtual methods** and keep this override. |
| `DisplayPipeGuard.cpp:1525-1533` | VBL cookie at `pipe+0xd8`. "RDNA4FB refuses that type unless boot-arg rdna4-vbl=1 is set (src/RDNA4FB/src/framebuffer.cpp:1514, :2090)" | upstream: `registerForInterruptType` refusal `framebuffer.cpp:1509-1515`, boot-arg `:2073-2074` |
| `tools/native/navi48accel/src/Navi48Accel.cpp:225,588,641`, `n48accel_pure.h:191,213` | `Navi48Framebuffer` copies "RDNA4FB's AMDRDNA4FB" `initForPM` (cited at `:1908-1941`), the IOFramebuffer overrides (`:2194-2442`) and the connection attributes (`:2385-2417`) | upstream: `initForPM` `:1907`, overrides `:2202-2424`, connection attributes `:2367-2407` |
| `src/navi48-bringup/src/dcn/navi48_dispread.h:25`, `navi48_dispread_flow.h:11` | DC_I2C EDID engine "ported from RDNA4FB's AMDRDNA4FB::readEDIDI2C" | upstream: `RDNA4FB::readEDIDI2C` `:792` |
| `tools/pc/fbbench.c:8,121` | `getPixelInformation` leaves `flags` 0; `'FBVR'` descriptor | upstream `:2261` |
| `src/navi48-bringup/src/ipdiscovery.cpp:3`, `amd/amdgpu_discovery.h:3`, `src/kmod_info.c:2`, `psp.cpp:67,198` | code copied from or modelled on RDNA4FB (discovery parser, kmod glue, DMU-segment FB location, BAR0 == MC offset) | `src/ipdiscovery.cpp`, `src/kmod_info.c` |

### 4e. Build, boot-args and scripts

| Our site | Dependency |
|---|---|
| `src/navi48-bringup/Makefile:2,11`; `BUILDING.md:12-15,43`; `CHANGES.md:71` | `MKSDK := ../RDNA4FB/MacKernelSDK`. BUILDING says plain `git clone .../RDNA4FB`. **This leaves `MacKernelSDK` empty because it is a submodule** (`RDNA4FB:.gitmodules`). Use `--recursive`, or clone `acidanthera/MacKernelSDK` directly. |
| `tools/native/navi48accel/Makefile:2,10`; `tools/native/navi48accel/tests/gate_plants.sh:21`; `tools/dcn41/build.py:24` | `src/RDNA4FB/MacKernelSDK` / symlink `src/RDNA4FB` (the private checkout location) |
| `tools/pc/esp-config.sh:5-7`, `tools/pc/esp-kext.sh:28`, `src/navi48-bringup/src/Navi48Bringup.hpp:7` | boot-arg `rdna4-off=1` (bring-up configs and rescue) | `framebuffer.cpp:2016-2023` |
| `mesa-patches/0004-*.patch:149`, `src/navi48-bringup/src/Navi48NativeABI.h:345` | `rdna4-nosleep` to keep the display awake | `framebuffer.cpp:2066-2070` |
| `DisplayPipeGuard.cpp:73`, `tools/pc/agdc-probe.c:259` | `rdna4-amdname=1` | **absent upstream** |
| `tools/pc/navi48test.c:718` | `rdna4-dmubver` | `framebuffer.cpp:1202-1215` |
| `README.md:90,99`, `src/navi48-bringup/NOTICE:5-6`, `third-party/THIRD-PARTY-LICENSES.txt` | credit / license | `LICENSE` |

None of the `tools/pc` scripts load or install RDNA4FB. They assume it is already installed. The production config is "RDNA4FB display, no bring-up" (`tools/pc/esp-config.sh:5`).

---

## 5. What the "modified build" must provide (inferred)

Our cited line numbers for the private `src/RDNA4FB/src/framebuffer.cpp` sit just above upstream's:

| | private (our citation) | upstream |
|---|---|---|
| vbl refusal | 1514 | 1513-1514 |
| "vbl armed" log line | 1546 (`accel-run.sh:1151`) | 1545 |
| initForPM | 1908 | 1907 |
| rdna4-vbl parse | 2090 | 2074 |
| overrides end | 2442 | 2424 |
| connection attributes | 2385-2417 | 2367-2407 |

So the private file is upstream (or something very close to it) plus about 1 line before `initForPM` and about 16-18 lines inside `start()` or just before it. That is enough for one boot-arg parse and a class/name switch. It is not a different driver. From the citations, the modified build must:

1. **Match the card.** Today that is 7550/7551. For us it must include **`0x7590`** in `Info.plist` and `probe()`. Neither the private nor the public build does this.
2. **Publish under `AMDRDNA4FB` when `rdna4-amdname=1`, else `RDNA4FB`** (`DisplayPipeGuard.cpp:72-74`, `agdc-probe.c:259`). Exactly one of the two classes matches per boot (`DisplayPipeGuard.cpp:2543-2545`). That implies two personalities/classes, each probe gated on the boot-arg. The most likely shape is `class AMDRDNA4FB : public RDNA4FB` with a second `IOKitPersonalities` entry. This is an inference: the private source is not available.
3. **Prefix logs with `AMDRDNA4FB: `** (`accel-run.sh:1150`), or at least do so when renamed.
4. **Keep everything upstream already provides:**
   - the `Console,*` properties;
   - `EDID,<bus><n>`;
   - the single-mode IOFramebuffer contract;
   - `getVRAMRange` returning the console range;
   - the `registerForInterruptType` VBL refusal unless `rdna4-vbl=1`;
   - `rdna4-off` and `rdna4-nosleep`;
   - a vtable of exactly IOFramebuffer's 351 slots, so no new virtuals;
   - `gMetaClass` in writable data.

Alternatively, **we may not need the modified build at all.** Point 2 exists only to put "AMD" in the class name CoreDisplay reads. Our kext already does that at runtime against stock `RDNA4FB` through `fbname` (`navi48_fbname.cpp:1-36`). Matching and lookups keep working under `RDNA4FB` (`navi48_fbname.cpp:28-36`), and every lookup site of ours accepts either name (section 4a). The only hard dependency on the private build is the log predicate in `accel-run.sh:1153`, and that script is tooling.

**Minimal plan for the 9060 XT:**
- fork or patch upstream RDNA4FB with `0x7590` (2 lines, BSD-3-Clause allows it);
- boot with `rdna4-nosleep=1` until R5 is checked;
- verify `Discovery,DCNVersion` and `rdna4-modedump=1` (which OTG, HUBP and DP stream encoder the GOP lit);
- use our `fbname` verb in place of `rdna4-amdname`;
- if wanted, add the ~17-line `rdna4-amdname` switch and the `AMDRDNA4FB:` log prefix.

---

## 6. Open items

- The DCN IP version on Navi 44 (R2) and the NBIF `RCC_CONFIG_MEMSIZE` offset (R3) are unverified. They should be confirmed by the Navi 44 vs Navi 48 research ticket, or by reading `Discovery,*` on the card.
- Which DIG/stream encoder the ASUS GOP uses for the DP port (R4/R5) is unknown until `rdna4-modedump=1` runs on the card.
- The private RDNA4FB source is not public. Section 5 is inferred from our citations and line offsets only.
