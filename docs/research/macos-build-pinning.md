# What ties Cobalt64 to macOS 26.6.2, and can 26.7.2 work?

Research for issue #2 (H-Sami/WhateverRed). Snapshot researched: the tree on `main` as of 2026-10-29. The research was done from Windows with no Mac, so every "check on the Mac" item below is still open.

## Answer

26.7.2 can work, but not as-is. The native route (the one WhateverRed ports) has two kinds of pin to 26.6.2 (25G83):

1. **Hard static addresses** from the 25G83 kernel collections, used by the AGDC nub (`pipeagdc`) and the `fbwc` helper. These almost certainly differ on 26.7.2 (25G332). They fail closed (REFUSED), so the result is a missing feature, not a panic. They have to be re-derived by hand from the 26.7.2 KCs.
2. **Layout facts**: vtable slot numbers, field offsets, and return types of IOAcceleratorFamily2 classes. These survive a point release only if Apple didn't touch the family. The aux kext checks them at build time and again at run time, and refuses on a mismatch. The generated header is not shipped at all (see "Surprises"), so the layout has to be regenerated on the Mac whatever the build.

Most of the hard-coded `25G83` addresses in the tree (AMDRadeonX6000 vtables, HWServices, `g2capture`, `xlat12` captures) belong to the **substitution route**, which is out of scope here ([GLOSSARY.md](../../GLOSSARY.md)). They would not apply on Navi 44 anyway without spoofing an X6000 device ID.

**Recommendation:** stay on 26.7.2 and re-derive the pins there. Don't downgrade the test bench. If you want a known-good baseline, install 26.6.2 (25G83) on a second APFS volume. Apple's catalog still listed its full installer as of the source below, but you must confirm that on the Mac.

## Build facts

| Build | Version | Released | Source |
|---|---|---|---|
| 25G83 | 26.6.2 (upstream's tested build) | 2026-08-17 | [Eclectic Light](https://eclecticlight.co/2026/08/17/apple-has-released-a-security-update-to-macos-tahoe-26-6-2/) ("The new build number is 25G83"); [Mr. Macintosh installer database](https://mrmacintosh.com/macos-tahoe-full-installer-database-download-directly-from-apple/) |
| 25G229 | 26.7 | 2026-09-14 | Mr. Macintosh database (above); [Mr. Macintosh 26.7 article](https://mrmacintosh.com/macos-tahoe-26-7-update-everything-you-need-to-know/) |
| 25G241 | 26.7.1 | 2026-09-28 | Mr. Macintosh database |
| 25G332 | 26.7.2 (the owner's bench) | 2026-10-26 | Mr. Macintosh database. **Unconfirmed** by a first-party Apple page; confirm with `sw_vers` on the bench. |

Note that the Mr. Macintosh 26.7 article's release history also prints 26.6.2 as "26G82". That is a typo on their page; Eclectic Light and their own database both give 25G83.

Tahoe is the last macOS for Intel Macs; macOS 27 is Apple silicon only ([TechRadar](https://www.techradar.com/computing/mac-os/its-official-your-intel-mac-wont-get-macos-27-with-macos-tahoe-26-marking-the-end-of-an-era), [MacRumors](https://macrumors.com/2026/06/03/macos-27-wont-run-on-these-macs/)). So 26.x point releases are the only x86_64 target left, and each one may move these pins again.

Upstream claims to have been tested only on 26.6.2: [README.md:25](../../README.md), [README.md:49](../../README.md), [README.md:57-58](../../README.md) ("many pieces work around one specific macOS build"), and [tools/ioaccel-layout/README.md:62](../../tools/ioaccel-layout/README.md) ("Tested only on macOS 26.6.2 (25G83) x86_64").

## Can 26.6.2 still be installed (October 2026)?

- **Full installer availability.** Mr. Macintosh's database lists a 26.6.2 / 25G83 InstallAssistant.pkg and says the package "is stored on Apple's servers" ([source](https://mrmacintosh.com/macos-tahoe-full-installer-database-download-directly-from-apple/)). Apple's documented way to fetch one is `softwareupdate --list-full-installers` followed by `softwareupdate --fetch-full-installer --full-installer-version 26.6.2`; a version missing from that list "is not available for your Mac" ([Apple 102662](https://support.apple.com/en-us/102662)). **Unconfirmed:** whether 26.6.2 appears in that list for the bench's SMBIOS model today. Mist is a front end to the same catalog, so it cannot find anything the catalog has dropped.
- **Signing.** On Intel there is no iOS-style signing window. What exists instead is Apple Secure Boot personalisation, and it only applies when OpenCore's `SecureBootModel` is set and `ApECID` is non-zero. In that case "Fresh installs with ApECID set to a non-zero value will require a network connection at install time for verification" ([Dortania, Apple Secure Boot](https://dortania.github.io/OpenCore-Post-Install/universal/security/applesecureboot.html)). **Unconfirmed:** whether Apple's server still personalises 25G83, and which `SecureBootModel` the bench uses. Nothing in this repo records it (grep for `SecureBootModel`/`ApECID` finds nothing). With `SecureBootModel=Disabled` no Apple server is involved and the installer only needs its package signature to be valid.
- **Second APFS volume.** Apple supports this: add a volume to the existing APFS container in Disk Utility, choose it in the installer under "Show All Disks", and switch with Startup Disk. Each volume has to be updated separately ([Apple 118282](https://support.apple.com/en-us/118282)). Disable automatic updates on the 26.6.2 volume, or it will update itself to 26.7.x.

## Build-pinned dependencies

Key:

- **Route:** N = native (in scope); S = substitution / X6000 (out of scope).
- **Fails:** C = fails closed (refuses with a status and log line); ? = not verified.
- **Mac:** whether a running Mac is needed to regenerate it.

### Native route

| # | Pin | Where | Fails | Regenerate for 26.7.2 | Mac |
|---|---|---|---|---|---|
| N1 | IOAcceleratorFamily2 class layout header `IOAccelFamily2_decl.h`, plus `model.json` / `forwarded.json` | Consumed at [tools/native/navi48accel/Makefile:11](../../tools/native/navi48accel/Makefile), [:22](../../tools/native/navi48accel/Makefile); [Navi48Accel.cpp:72](../../tools/native/navi48accel/src/Navi48Accel.cpp); [gates/common.py:3](../../tools/native/navi48accel/gates/common.py), [:9](../../tools/native/navi48accel/gates/common.py), [:12](../../tools/native/navi48accel/gates/common.py) | C at build ([gate_link.py:89](../../tools/native/navi48accel/gates/gate_link.py)) and at run time ([n48accel_pure.h:46-66](../../tools/native/navi48accel/src/n48accel_pure.h) compares our vtable to the family's `__ZTV` slot by slot) | Yes. Copy the two KCs and carve IOAcceleratorFamily2 and AppleParavirtGPU by hand ([tools/ioaccel-layout/README.md:27-38](../../tools/ioaccel-layout/README.md); no carving script exists). Then run `dump_vtables.py`, `gen_header.py` and `check_layout.py` (expect MATCH). `KERNEL_BASE` ([kcmod.py:18](../../tools/ioaccel-layout/kcmod.py)) was verified across 26.x ([README.md:60-61](../../tools/ioaccel-layout/README.md)). | KC copies come from the Mac; the scripts are Python and also run on a PC |
| N2 | Leaf slot lists: `FB_EXTRA = [184, 310, 311, 319, 322, 325, 326, 330, 331, 332, 344, 345]`, slot 267, `EXPECT_CLASSES = 13`, `EXPECT_SLOTS = 2720` | [gates/leaves.py:4](../../tools/native/navi48accel/gates/leaves.py), [:10](../../tools/native/navi48accel/gates/leaves.py), [:46-47](../../tools/native/navi48accel/gates/leaves.py) | C (build gate) | By hand, if N1's model shows changed slot counts. Otherwise unchanged. | No, once N1 exists |
| N3 | `N48_R_` return-type pins, taken from disassembly of AppleParavirtGPU 25G83 (e.g. `0x142b78f4`) | [Navi48Accel.cpp:33-71](../../tools/native/navi48accel/src/Navi48Accel.cpp) | ? A wrong return type is not detected by the slot gate | By hand: disassemble the 26.7.2 AppleParavirtGPU at the cited slots. Only needed if N1 shows a changed layout. | Carved kext from the Mac; the disassembly can be done anywhere |
| N4 | IOAccelConfig built from AppleParavirtAccelerator::populateAccelConfig (`0x142b78f4`): 12 stores, 0x90 bytes | [src/navi48-bringup/src/amd/native_metal_pure.h:63-73](../../src/navi48-bringup/src/amd/native_metal_pure.h) | ? | Re-disassemble populateAccelConfig in the 26.7.2 paravirt kext and diff the 12 stores | As N3 |
| N5 | IOAccelDisplayPipe field offsets (`kPipeSize 0x318`, `+0x88/+0x90/+0x98/+0xe0`, `kPipeActive 0x298`) and the accel IOAccelConfig base `0xc88` (the `pipearm` gate byte `accel+0xccf`, static-asserted) | [native_disp_pure.h:121-131](../../src/navi48-bringup/src/amd/native_disp_pure.h) | ? These are raw offsets. The verbs check that the objects exist, but nothing proves a field still sits at the same offset. | Re-disassemble IOAccelDisplayPipe::init / isActive and the type-4 user-client gate (cited at `0x145cb31a`, `0x145cc042`, `0x145c01a8`) in the 26.7.2 IAF2 | Carved kext from the Mac |
| N6 | AGDC nub (`pipeagdc`, action 88): static System KC addresses `kAgdcGMetaClass=0x13d418e8`, `kAgdcVtable=0x13d40478`, operator new / ctor / dtor / delete, slots 184/238/267, `kAgdcVtSlots=276`, class size 0x110, first-byte signatures, slot 7 = `0x13d3be5e` | [DisplayPipeGuard.cpp:1652-1670](../../src/navi48-bringup/src/apple/DisplayPipeGuard.cpp), [:1932-1946](../../src/navi48-bringup/src/apple/DisplayPipeGuard.cpp) | C. The slide must agree between the AGDC metaclass and the IAF2 `IOAccelDisplayPipe::gMetaClass` (`kPipeMeta=0x146036d8`), and be page aligned ([native_agdc_pure.h:44-55](../../src/navi48-bringup/src/amd/native_agdc_pure.h)). Then class size, code bytes and vtable slots are checked. | By hand from the 26.7.2 System KC: `nm`/symbol lookup for the two `gMetaClass` symbols and `__ZTV26AppleGraphicsDeviceControl`, then read the slots and first bytes. A point update rebuilds the KC, so expect every address to move and expect status 4 (slide) until this is done. | Yes (KC copy; a live `pipeagdc 0` read confirms) |
| N7 | `fbwc` helper: kernel addresses `kKIOLog=0xffffff8000a859d0`, `kKIOSleep`, IOGeneralMemoryDescriptor vtable `0xffffff800026b5d8` and slots 70/72, plus IOFramebuffer slots 310/317/318 | [DisplayPipeGuard.cpp:2420-2425](../../src/navi48-bringup/src/apple/DisplayPipeGuard.cpp), [:2774-2775](../../src/navi48-bringup/src/apple/DisplayPipeGuard.cpp) | ? Opt-in behind boot-arg `navi48-fbwc` ([:2539](../../src/navi48-bringup/src/apple/DisplayPipeGuard.cpp)). Not part of the autoarm recipe. | By hand from the 26.7.2 Boot KC (kernel symbols) | Yes |
| N8 | Private Metal SPI superclasses (`MTLIOAccelDevice`, `_MTLResource`, `_MTLHeap` and others), declared without ivars | [tools/native/navi48metal/Navi48Device.m:4-11](../../tools/native/navi48metal/Navi48Device.m), [:72-104](../../tools/native/navi48metal/Navi48Device.m) | ? The non-fragile ObjC ABI tolerates ivar changes. Renamed or removed SPI methods would only show at run time. | Not generated. Check with `nm`/`otool -ov` on 26.7.2's Metal.framework that the classes and the overridden selectors still exist. | Yes |
| N9 | GPUCompiler libLLVM: dlopen under `/System/Library/PrivateFrameworks/GPUCompiler.framework/Versions/*` and ten C API symbols; the C++ `setOpaquePointers` symbol is optional | [n48_xlate.h:51](../../tools/native/navi48metal/n48_xlate.h), [:580-591](../../tools/native/navi48metal/n48_xlate.h) | C. Every failure falls back to the old behaviour ([n48_xlate.h:6-12](../../tools/native/navi48metal/n48_xlate.h)). | Nothing to regenerate. Confirm the dylib path and symbols with `nm -gU`. | Yes |
| N10 | spvcache keyed by the sha256 of Apple's AIR bitcode (WindowServer, CoreImage) | [Navi48Device.m:130-153](../../tools/native/navi48metal/Navi48Device.m), [:1917-2023](../../tools/native/navi48metal/Navi48Device.m) | C, soft. A miss gives a fallback pipeline in WindowServer and dumps the AIR to `/tmp/n48m/<sha>.air`. Drawing may be wrong, but nothing panics. | Yes. Run on 26.7.2, collect the dumps, and re-translate with `tools/native/autotranslate` (`air-pull.sh`, `pc-translate.sh`) and `tools/native/navi48metal/fill-spvcache.py` / `add-air.py`. The cache is not shipped in this repo anyway. | Yes for collecting the dumps; translation can run elsewhere |
| N11 | `kWindowServerUid = 88` | [native_open_policy_pure.h:18](../../src/navi48-bringup/src/amd/native_open_policy_pure.h) | n/a | Stable (`_windowserver` is uid 88 on stock macOS). Spot-check with `id _windowserver`. | Trivial |
| N12 | `LSMinimumSystemVersion 26.6` | [tools/native/navi48metal/Info.plist](../../tools/native/navi48metal/Info.plist) | n/a | 26.7.2 satisfies it | No |
| N13 | Autoarm recipe and the boot-arg set | [tools/pc/autoarm/n48-autoarm.sh:147](../../tools/pc/autoarm/n48-autoarm.sh), [:239-240](../../tools/pc/autoarm/n48-autoarm.sh), [:279](../../tools/pc/autoarm/n48-autoarm.sh) | Each step fails on a refusal | Not build-pinned in itself. It calls `pipeagdc 1` (N6) and `pipearm 1` (N5), so it inherits their pins. | Run it on the Mac |

Not pinned:

- The bring-up kext targets the macOS 11 ABI ([src/navi48-bringup/Makefile:2](../../src/navi48-bringup/Makefile)).
- The aux kext links by symbol against `com.apple.iokit.IOAcceleratorFamily2` 2.0.0 ([tools/native/navi48accel/Info.plist:75-76](../../tools/native/navi48accel/Info.plist)).
- `fbname` scans OSMetaClass words at run time (src/navi48-bringup/src/dcn/navi48_fbname.cpp).
- ReBAR > 1 GiB is an IOPCIFamily behaviour, not an address ([rebar_pure.h:3](../../src/navi48-bringup/src/amd/rebar_pure.h)).

### Substitution / X6000 route (out of scope, listed so nobody re-derives them by mistake)

| Pin | Where |
|---|---|
| AMDRadeonX6000 Hardware/regs/VMM vtable slots and deltas, "Measured from … Tahoe 26.6.2"; VMContext slots 40/41 "CONFIRMED … 25G83" | [AppleHardwareHook.cpp:159-215](../../src/navi48-bringup/src/apple/AppleHardwareHook.cpp), [:261-277](../../src/navi48-bringup/src/apple/AppleHardwareHook.cpp) |
| 16 static X6000 VAs (e.g. `kVaRingGetHead=0x0be1bc5c`, `kVaHWHandlerVtable=0x0bf2c698`) | AppleHardwareHook.cpp:5314-14900 |
| X6000 accelerator `__ZTV…AMDGraphicsAccelerator @0xbf14be0`, 366 slots; `navi48_x6000_slide` anchors (slots 184, 326) | [Navi48AccelPeer.cpp:157-174](../../src/navi48-bringup/src/apple/Navi48AccelPeer.cpp), [:4401-4412](../../src/navi48-bringup/src/apple/Navi48AccelPeer.cpp) |
| X6000 pipe guard getMetaClass addresses; translation-route AGDC slide check against the X6000 slide | [DisplayPipeGuard.cpp:108-119](../../src/navi48-bringup/src/apple/DisplayPipeGuard.cpp), [:2017-2025](../../src/navi48-bringup/src/apple/DisplayPipeGuard.cpp) |
| `__ZTV14AmdTtlServices @0xc7c28f0`; HWServices values | [AmdTtlServicesABI.h:3](../../src/navi48-bringup/src/apple/AmdTtlServicesABI.h), [AppleTtlHook.cpp:14](../../src/navi48-bringup/src/apple/AppleTtlHook.cpp), [:37](../../src/navi48-bringup/src/apple/AppleTtlHook.cpp) |
| GFX neuter slot 95 @0xbe20a00; GPUPass ABI; resprov tables | [gfx_neuter.h:26](../../src/navi48-bringup/src/apple/gfx_neuter.h), [gfx_lutidx111.h:6](../../src/navi48-bringup/src/apple/gfx_lutidx111.h), [ws_resprov.h:1028](../../src/navi48-bringup/src/apple/ws_resprov.h) |
| AMDRadeonX6000MTLDriver 25G83 constants | [src/g2capture/g2capture.m:9](../../src/g2capture/g2capture.m), [:37](../../src/g2capture/g2capture.m) |
| xlat12 self-tests need captured Apple IB streams (not shipped) | [BUILDING.md](../../BUILDING.md) |
| `iodp_vtable_static.h`: 306 IOAccelDisplayPipe slots of 25G83 IAF2 and kernel addresses. GENERATED, but **nothing in the shipped tree includes it**. | [iodp_vtable_static.h:1-4](../../src/navi48-bringup/src/apple/iodp_vtable_static.h) |

## What to check on the Mac (26.7.2 bench)

1. Confirm the build with `sw_vers`; 25G332 is expected.
2. Compare the IAF2 version on 26.6.2 and 26.7.2 with `kmutil showloaded --list-only | grep -i IOAccelerator`. An unchanged bundle version is a strong hint, though not proof, that N1-N5 hold.
3. Copy `/System/Library/KernelCollections/BootKernelExtensions.kc` and `SystemKernelExtensions.kc`, carve IOAcceleratorFamily2 and AppleParavirtGPU, and run the ioaccel-layout pipeline (N1) with `C64_LAYOUT_OUT` pointing at `tools/native/ioaccel-layout/generated` (see Surprises). `check_layout.py` must say MATCH, and `make` in `tools/native/navi48accel` must pass its gates (N2).
4. In the System KC, look up `__ZN26AppleGraphicsDeviceControl10gMetaClassE`, `__ZTV26AppleGraphicsDeviceControl` and `__ZN18IOAccelDisplayPipe10gMetaClassE`. Update N6's constants and byte signatures, rebuild, then run `pipeagdc 0` / `pipeagdc 1`; status 0 means it works.
5. Re-disassemble the cited IAF2 functions for N4/N5: IOAccelDisplayPipe init / isActive, the type-4 gate, and populateAccelConfig.
6. Metal: `otool -ov` on Metal.framework for the N8 classes, and `nm -gU` on GPUCompiler's libLLVM for N9.
7. Run the autoarm recipe (N13) with WindowServer up, then collect `/tmp/n48m/*.air` misses and refill the spvcache (N10).
8. Record the bench's OpenCore `SecureBootModel`/`ApECID` values, to settle the signing question above.

## Surprises

- **The aux kext build points at a directory that doesn't exist.** `tools/native/navi48accel/Makefile:11` sets `LAYOUT := ../ioaccel-layout` and `gates/common.py:3` uses `'../../ioaccel-layout'`. Both resolve to `tools/native/ioaccel-layout`, but the scripts live in `tools/ioaccel-layout`, and `kcmod.py:23` writes to `C64_LAYOUT_OUT` (default `./layout-out`). The generated header is not shipped either. So Navi48Accel does not build from this tree on any macOS build until N1 is regenerated and the path is fixed or bridged.
- **The native AGDC nub hard-codes System KC addresses.** In practice this is the pin most likely to break on 26.7.2. Its double-anchor slide check makes it refuse cleanly, though.
- **`iodp_vtable_static.h` is dead weight.** It is 25G83-specific and nothing includes it; it was only used by a test that isn't shipped.
