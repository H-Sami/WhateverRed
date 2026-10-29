# Does the pinned Mesa have gfx1200 device info for RADV's built-in table?

Research for issue #5 (2026-10-29). Target: RX 9060 XT / Navi 44, PCI 1002:7590, gfx1200, GC 12.0.0.

## Short answer

1. **No.** The drm-shim table `amdgpu_devices[]` at the pin has no `gfx1200` entry. Mesa `main` (checked 2026-10-29) has none either, and no Mesa commit or MR adds one. So there is no "first commit that adds it". We have to write our own `gfx1200` entry (in a patch), or build `dev_info` from the kext.
2. **Yes, RADV supports gfx1200 at the pin.** ac_gpu_info identifies the chip, the gfx level is GFX12, and the LLVM processor is `gfx1200`. Every GFX12 workaround is keyed on `gfx_level`, so none are chip-specific. Mesa CI exercises only gfx1201.
3. The Darwin patches hard-code the table name `"gfx1201"`, the RX 9070 XT marketing name, a fake PCI id 0x7550, the GB_ADDR_CONFIG value 0x08200545 (checked for equality, and also served to RADV from the canned `mmr_regs`), and the canned VA ranges. They serve the canned `DEV_INFO` as is. The full list is in the table in section 3.

Abbreviation: `M:` = `https://gitlab.freedesktop.org/mesa/mesa/-/blob/f5cb8ee032adabef599ae892ec4e42d796b84da9/`. Patch refs are `mesa-patches/<file>:<line>` in this repo; the numbers are lines in the .patch file. `0001` = `0001-radv-Darwin-macOS-backend-with-canned-gfx1201-device.patch`, `0002` = `0002-radv-Darwin-native-backend-Mesa-half-of-the-Navi48-k.patch`, and so on.

## Sources

- Pin: `mesa-patches/README.txt`. The commit is `f5cb8ee032adabef599ae892ec4e42d796b84da9` (VERSION 26.3.0-devel, 2026-09-29, "gallivm: fix clamping of 32-bit unsigned pure integer image writes").
- Mesa source, read at the pin from the GitLab archive tarball of `src/amd`, plus the GitLab commits API for the file history of `src/amd/common/amdgpu_devices.c`.
- libdrm `data/amdgpu.ids` at `main` (drm head 33fab8cb, 2026-10-27): https://gitlab.freedesktop.org/mesa/drm/-/blob/main/data/amdgpu.ids (lines 666 and 672-676).
- Sibling research: `docs/research/navi44-vs-navi48-amdgpu.md` on branch `research/navi44-vs-navi48-amdgpu` (issue #3).

## 1. The gfx1200 entry in amdgpu_devices.c

- At the pin, the table contains exactly these entries: pitcairn, bonaire, polaris10, polaris12, stoney, vega10, raven, raven2, renoir, vega20, navi10, navi21, vangogh, rembrandt, raphael_mendocino, navi31, navi33, phoenix, strix1, gfx1201. The file is 2705 lines long: `M:src/amd/common/amdgpu_devices.c`.
- The only RDNA4 entry is `gfx1201`, at `M:src/amd/common/amdgpu_devices.c#L2574` through `#L2702`. Its values:
  - `radeon_family = CHIP_GFX1201`
  - `hw_ip_gfx` / `hw_ip_compute` 12.0, `ip_discovery_version 0xc0001`
  - firmware: me 2590, pfp 2630, mec 2800 (feature 29)
  - `mmr_regs` = `{0x263e, 0xffffffff, 0x08200545}`, which is GB_ADDR_CONFIG (`#L2607`-`#L2609`)
  - `device_id 0x7550`, `chip_rev 0x01`, `external_rev 0x51`, `pci_rev 0xc0`, `family AMDGPU_FAMILY_GC_12_0_0`
  - `num_shader_engines 4`, `num_shader_arrays_per_engine 2`, `cu_active_number 64`, `cu_bitmap` with 4 SE rows of `{0xff,0xff,0,0}`, `num_cu_per_sh 8`
  - `enabled_rb_pipes_mask 0xffff`, `num_rb_pipes 16`
  - `max_engine_clock 2460000`, `max_memory_clock 1258000`, `pcie_gen 4`, `pcie_num_lanes 8`
  - `vram_type 9` (GDDR6), `vram_bit_width 256`, `num_tcc_blocks 32`, `gl2c_cache_size 8192`, `mall_size` 64 MiB
  - VA from 0x10000 to 0x800000000000; high VA from 0xffff800000000000 to 0xffffffffffbfe000
  - `mem`: VRAM total 17095983104, CPU-accessible 256 MiB, GTT 16785289216
- History (GitLab commits API, `path=src/amd/common/amdgpu_devices.c`, ref `main`):
  - `gfx1201` came in with b69b9b8eb2dc (2025-03-10), "amd/drm-shim: add gfx1201".
  - The most recent change to the file is cb821bfb49b6 (2026-09-10), "ac: sort devices ...".
  - No commit on `main` adds `gfx1200`, and searching MRs for "drm-shim" turns up no gfx1200 MR. The distance from the pin is therefore undefined: no such commit exists.

## 2. Does RADV fully support gfx1200 at the pin?

- `CHIP_GFX1200` ("Radeon 9060") and `CHIP_GFX1201` ("Radeon 9070") both exist: `M:src/amd/common/amd_family.h#L139`-`#L140`.
- `ac_get_gfx_level` maps any chip >= CHIP_GFX1200 to GFX12: `M:src/amd/common/amd_family.c#L74`-`#L79`.
- The LLVM processor name is `"gfx1200"`: `M:src/amd/common/amd_family.c#L198`-`#L199`.
- How the chip is identified:
  - `FAMILY_NV4` is 0x98: `M:src/amd/addrlib/src/amdgpu_asic_addr.h#L36`.
  - The external_rev ranges are GFX1200 0x40-0x50 and GFX1201 0x50-0xFF: `M:src/amd/addrlib/src/amdgpu_asic_addr.h#L121`-`#L122`.
  - The identify macro is at `M:src/amd/common/ac_gpu_info.c#L669`-`#L673`.
  - On FAMILY_NV4 with GFX minor 0, ac_gpu_info tries GFX1200 first, then GFX1201: `M:src/amd/common/ac_gpu_info.c#L763`-`#L770`.
  - An external_rev outside both ranges fails with "unknown (family_id, chip_external_rev)": `#L773`-`#L776`.
  - The gfx level is then set to GFX12 at `#L777`-`#L782`.
- `radv_is_gpu_supported` rejects only chips with `gfx_level > GFX12`, so GFX12 passes: `M:src/amd/vulkan/radv_physical_device.c#L2503`-`#L2512`.
- `deviceID` comes from `info.pci_id` (`M:src/amd/vulkan/radv_physical_device.c#L1817`). `pci_id` and `pci_rev` come from the kernel's device_info (`M:src/amd/common/ac_gpu_info.c#L663`-`#L664`).
- The device name comes from `ac_get_family_name`: `M:src/amd/vulkan/radv_physical_device.c#L2773`-`#L2775`.
- Workarounds:
  - Every GFX12 workaround is keyed on `gfx_level`, not on the chip. Examples: `gfx12_hiz_wa` at `M:src/amd/vulkan/radv_physical_device.c#L2793`; `has_out_of_order_uncached_l2` at `M:src/amd/common/ac_gpu_info.c#L585`; `has_vrs_export_bug` at `#L1033`.
  - A grep for `CHIP_GFX1200` and `CHIP_GFX1201` across `src/amd` finds no chip-specific branches, only ids, names and compiler tests (for example `M:src/amd/compiler/tests/helpers.cpp#L80`, which maps GFX12 to CHIP_GFX1201). RGP writes EF_AMDGPU_MACH_AMDGCN_GFX1200 for every GFX12 chip: `M:src/amd/common/ac_rgp.c#L883`.
  - MR 33986 (2025-03-10) fixed a GPU hang with inherited rendering plus HiZ/HiS that its description says "isn't present on GFX1200". The fix is keyed on gfx_level, so gfx1200 also runs the workaround. That is harmless, just slightly conservative: https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/33986
- CI coverage at the pin is gfx1201 only: `M:src/amd/ci/gitlab-ci.yml#L338`-`#L365` and `#L556`, with `radv-gfx1201-*.txt` expectation lists. MR 33258 (2025-01) once added GFX1200 lists: https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/33258. gfx1200 is supported in code, but CI does not test it.
- Geometry: RADV derives `max_se`, `num_cu_per_sh`, `max_render_backends`, `enabled_rb_mask` and the CU masks from `dev_info` (`M:src/amd/common/ac_gpu_info.c#L1207`-`#L1216`, `#L1238`-`#L1295`). These values feed binning tables (`M:src/amd/vulkan/radv_cmd_buffer.c#L2918`-`#L2955`), occlusion query strides and result reads (`M:src/amd/vulkan/radv_query.c#L1991`, `#L2196`-`#L2204`), GS ring sizing (`M:src/amd/vulkan/radv_shader_info.c#L680`-`#L689`) and `shaderEngineCount` (`M:src/amd/vulkan/radv_physical_device.c#L2096`). Serving 9070 XT geometry to a 9060 XT is therefore a real risk. Occlusion queries in particular would wait for results from 16 RBs when only 8 exist.
- GB_ADDR_CONFIG matters less on GFX12 than on earlier generations:
  - Gfx12 addrlib's `HwlInitGlobalParams` ignores `gbAddrConfig`: `M:src/amd/addrlib/src/gfx12/gfx12addrlib.cpp#L1213`-`#L1223`.
  - In ac_gpu_info, the register only feeds `num_tile_pipes`, plus an assert that the pipe interleave is 256 B: `M:src/amd/common/ac_gpu_info.c#L455`-`#L468`.
  - GFX12 DCC modifiers do not encode pipes or PKRS (`M:src/amd/common/ac_surface.c`, GFX12 modifier case, about lines 940-955).
  - The one RADV consumer found is `WALK_FENCE_SIZE`, which depends on `num_tile_pipes == 2`: `M:src/amd/vulkan/radv_cmd_buffer.c#L4871`.
  - Field layout, from `M:src/amd/registers/gfx12.json#L11647`: NUM_PIPES[2:0], PIPE_INTERLEAVE_SIZE[5:3], MAX_COMPRESSED_FRAGS[7:6], NUM_PKRS[10:8], NUM_SHADER_ENGINES[22:19], NUM_RB_PER_SE[27:26]. Decoding 0x08200545 gives NUM_PIPES=5, interleave 0 (256 B), frags=1, PKRS=5, SE field=4, RB_PER_SE=2. A Navi 44 value will differ at least in the SE and pipe fields; the sibling research says not to assume 0x08200545.

## 3. Fields the Darwin patches check or hard-code

| What | Where | Value now | Change for 1002:7590 |
|---|---|---|---|
| Canned table name | `0001:2930` (`find_canned("gfx1201")`, returns -ENODEV if missing); lookup at `0001:2914`-`2921` | `"gfx1201"` | Add a `gfx1200` entry to `amdgpu_devices.c` in a patch (meson already builds the file, `0001:4227`-`4231`) and look it up. Better: pick the entry from the kext's PCI id or gc_version. |
| Marketing name | `0001:2884`-`2885`; kept in `0002:271`-`272` | "AMD Radeon RX 9070 XT" (amdgpu.ids 7550,C0) | "AMD Radeon RX 9060 XT" (amdgpu.ids `7590,C0`, line 672). Other 7590 revisions: C1 "RX 9060 XT LP", C7 "RX 9060", CF "RX 9050". |
| Fake PCI identity (only with `RADV_DARWIN_FAKE=1`) | `0001:4343`-`4352`; repeated at `0002:3632` | vendor 0x1002, device 0x7550, subvendor 0x1002, rev 0xC0 | device 0x7590, rev 0xC0 |
| `DEV_INFO` | `0001` `acd_info`, `0001:3017`-`3100` (served whole as `d->dev`) | gfx1201 entry | `device_id 0x7590`, `pci_rev 0xc0`. `external_rev` in 0x40-0x4F (= chip_rev + 0x40 on GC 12.0.0, per the sibling doc); 0x51 would be identified as GFX1201. Keep `family GC_12_0_0`. `num_shader_engines 2`, `num_shader_arrays_per_engine 2`, `cu_active_number 32`, `cu_bitmap` 2 SE rows. `num_rb_pipes 8`, `enabled_rb_pipes_mask 0xff`. `vram_bit_width 128`, `vram_type 9`. Unverified, need a live dump: `num_tcc_blocks` (likely 16), `gl2c_cache_size` (likely 4096), `mall_size` (likely 32 MiB), clocks, PCIe lanes. |
| `hw_ip` / IP discovery | `0001` `acd_info` hw_ip_gfx/compute | 12.0, `ip_discovery_version 0xc0001` | 12.0 stays. `ip_discovery_version` is likely 0xc0000 (GC 12.0.0). |
| Firmware versions | `0001` `acd_info` fw_gfx_me/pfp/mec | me 2590 / pfp 2630 / mec 2800 | Take them from the card's firmware blobs or the kext. RADV only uses them for feature gating. |
| `READ_MMR_REG` GB_ADDR_CONFIG served to RADV | `0001:3021`-`3040` (serves `mmr_regs` triples from `amdgpu_devices.c#L2607`-`#L2609`) | 0x08200545, always canned | Serve the live value that the kext read (`n48n_info.gb_addr_config`, struct at `0002:189`-`200`), not the canned triple. At the moment RADV sees 0x08200545 whatever the hardware has. |
| GB_ADDR_CONFIG equality check | define at `0002:1885`-`1886`; check at `0002:390`; still present after `0007:16`-`38` (QueryInfo first, ReadRegs fallback) | must equal 0x08200545 unless `RADV_DARWIN_ALLOW_ADDRCFG=1` | Compare against the Navi 44 value (record it from a live dump), or compare against the chosen canned entry's `mmr_regs`. |
| VA ranges | VA manager init `0001:2946`-`2949`, `0002:457`-`460`; containment check `0002:397`-`410`; `address32_hi` `0001:2988`-`2989`; `va_range_query` `0001:3012`-`3013` | canned gfx1201 VA | Same GC 12 VM layout, so it most likely stays. The check guards it anyway. |
| `MEMORY` | `0001` `acd_info` (canned `d->mem`, max_allocation 3/4); `0002` `acd_info_memory` `0002:376`-`419` uses kext QueryInfo, canned only without a kext (`0002:455`-`472`); `0006` budget, `acd_apply_budget` | kext values, or 16 GiB canned | 16 GB VRAM comes from the kext. Only the HOST_BO/no-kext fallback needs a canned value of about 16 GiB. |
| Mock kext | `0002:2028`-`2032` (vis 168 MiB, hi 15 GiB, GTT 512 MiB); `0002:2428` and `0002:2457` (gb_addr_config GFX1201); `0002:2429` gc_version 12.0.1; tests `0002:3562`-`3563`; `0003:406`, `0003:663`; scanout 2560x1440@60 `0003:463`-`466` | Navi 48 | gc_version 12.0.0 and the Navi 44 GB_ADDR_CONFIG, if the mock is meant to model this card. |
| Service class names | `0008:35`, `0008:104`-`105` | "Navi48Bringup" / "Navi48Accelerator" | Naming only. Must match whatever the kext registers. |
| `has_userptr` | ac_gpu_info hunk `0001:2325`-`2340` | false | Not device-specific. |

Patches 0004, 0005 and 0009 contain no device-table hard-codes.

## Needs a live dump from the card

GB_ADDR_CONFIG, `external_rev`/`chip_rev`, `num_tcc_blocks`, `gl2c_cache_size`, `mall_size`, the CU/RB harvest masks, clocks, firmware versions, and `ip_discovery_version`. The sibling research (issue #3) found that Navi 44 geometry comes from the IP-discovery `gc_info`, not from driver source. A Linux dump of the `amdgpu_info` ioctls from a real 7590 would fill every value in one pass. Those are the same queries `amdgpu_devices.c` entries are generated from.

## Surprises

- No upstream gfx1200 drm-shim entry exists, even on `main`.
- Gfx12 addrlib ignores GB_ADDR_CONFIG, so the strict equality check protects less than its comment suggests ("the tiling layout the canned info and S1d's image assume", `0002:383`).
- Whatever the live register holds, the canned `mmr_regs` keep serving 0x08200545 to RADV.
- The mock kext reports gc_version 12.0.1 (Navi 48). The target card is 12.0.0.
- Mesa CI tests only gfx1201.
