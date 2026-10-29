# Navi 44 vs Navi 48 bring-up in Linux amdgpu

Research for issue #3. Question: how does Navi 44 (RX 9060 XT, PCI 1002:7590) bring-up differ from Navi 48 (RX 9070/XT 1002:7550, R9700 1002:7551) in Linux amdgpu, and what does each difference mean for the kext in `src/navi48-bringup`?

## Sources

- Linux kernel at tag **v7.3**, files fetched one at a time from `torvalds/linux`. All `drivers/gpu/drm/amd/...` paths are shortened below: `amdgpu/X.c:N` means `drivers/gpu/drm/amd/amdgpu/X.c` line N at v7.3. `smu_v14_0.c` and `smu_v14_0_2_ppt.c` live under `drivers/gpu/drm/amd/pm/swsmu/smu14/`, and `amdgpu_smu.c` under `drivers/gpu/drm/amd/pm/swsmu/`.
- linux-firmware `WHENCE` on `main` (gitlab.com/kernel-firmware/linux-firmware), the amdgpu section.
- Kext paths are relative to `src/navi48-bringup/` at the commit this file was added on.

## Answer in one paragraph

Linux has almost no Navi 44-specific bring-up code. Discovery sends GC 12.0.0 / 12.0.1, MP0 14.0.2 / 14.0.3, MP1 14.0.2 / 14.0.3 and SDMA 7.0.0 / 7.0.1 to the same IP-block implementations, with the same sequence. The die geometry (SE/SA/WGP/RB) comes from the discovery `gc_info` table on the card, not from code. The real per-version differences are:

1. Firmware file names (`gc_12_0_0_*`, `psp_14_0_2_*`, `smu_14_0_2`, `sdma_7_0_0`). Navi 44 has no kicker variants.
2. The PSP firmware-reservation SOS version threshold (`0x3b0e0d` vs `0x3a0e14`).
3. SMU `enable_gfx_features` (14.0.2 only, reset path only) and the energy accumulator (14.0.3 only).
4. soc24 `pg_flags` (Navi 44 lacks `JPEG_DPG`) and `external_rev_id` (`+0x40` vs `+0x50`).
5. A DF IH client-ID branch in gmc that resolves to the same value.

For the kext, the work is mostly renaming and un-hardcoding: the probe ID, the firmware table, the `kIP_*` pins, and the hardcoded 64-CU geometry.

## How the driver picks a path

- amdgpu binds every AMD VGA/display-class device as `CHIP_IP_DISCOVERY` (`amdgpu/amdgpu_drv.c:2193-2201`). There is no `0x7590` or `0x7550` entry. Everything after that is chosen by the IP versions in the on-die discovery table.
- Family: GC 12.0.0 and 12.0.1 both map to `AMDGPU_FAMILY_GC_12_0_0` (`amdgpu/amdgpu_discovery.c:3404-3407`).
- The only PCI-ID-based logic is the kicker list `{0x744B,0x00},{0x7551,0xC8}` (`amdgpu/amdgpu_ucode.c:32-35`), matched by `amdgpu_is_kicker_fw` (`amdgpu/amdgpu_ucode.c:1432-1438`). Navi 44 (`0x7590`) is not in the list, so it never loads kicker firmware.

## Per block

### IP discovery / SOC common

| | Navi 44 | Navi 48 | Source |
|---|---|---|---|
| GC | 12.0.0 (gfx1200) | 12.0.1 (gfx1201) | firmware names in `amdgpu/gfx_v12_0.c:73-83` |
| SOC common | soc24 | soc24 | `amdgpu/amdgpu_discovery.c:2377-2378` |

- `soc24.c:349-366` (12.0.0) vs `soc24.c:368-387` (12.0.1):
  - `cg_flags` are identical.
  - On 12.0.0, `pg_flags` **omit `AMD_PG_SUPPORT_JPEG_DPG`**.
  - `external_rev_id = rev_id + 0x40` on 12.0.0 and `+ 0x50` on 12.0.1.
- Reset: both use MODE1 (`soc24.c:187-188`).
- LSDMA: 7.0.0 / 7.0.1, both via `lsdma_v7_0` (`soc24.c:553-554`; `amdgpu_discovery.c:3643-3644`).
- **Kext:** neither the JPEG DPG nor the `external_rev_id` difference matters. The kext does no VCN/JPEG power gating, and nothing in the kext reads `external_rev_id`. The kext's own version pins are listed under GFX below.

### NBIO / NBIF

- Discovery switch at `amdgpu_discovery.c:3440-3510`:
  - NBIO 7.11.0-7.11.3 selects `nbio_v7_11`.
  - NBIO 6.3.1 / 7.11.4 / 7.11.5 select `nbif_v6_3_1` (`:3505`).
- No GC- or MP-version branch distinguishes Navi 44 inside `nbif_v6_3_1.c` (grep of v7.3 for `IP_VERSION(12, 0, x)` returns nothing).
- Which NBIO version Navi 44 reports is not in the source. It is a **live-dump item**.
- **Kext:** `amd/nbif_v6_3_1.cpp:89-99` (`nbif_use_4_10`) is already driven by the reported version, so it is safe.
- **Inconsistency to fix:** the kext contradicts itself.
  - `amd/amdgpu_ip.h:40` and `:54` pin `kIP_NBIO = {7,11,0}`.
  - `amd/nbif_v6_3_1.cpp:91` says Navi 48 reports NBIF 6.3.1.
  - Check which one is true on Navi 48, then dump Navi 44.

### GMC / GFXHUB / MMHUB

- Both use `gmc_v12_0` (`amdgpu_discovery.c:2440-2441`) and `gfxhub_v12_0` (`gmc_v12_0.c:623-624`).
- Both use a 48-bit VM with `dma_addr_bits` 44 (`gmc_v12_0.c:832-843`), and DCC alignment for both (`gmc_v12_0.c:560-561`).
- Golden settings are empty (`gmc_v12_0.c:993`).
- One 12.0.0-specific branch, `gmc_v12_0.c:899-904`, selects the DF IH client ID:
  - 12.0.0 uses `SOC21_IH_CLIENTID_DF`.
  - Other versions use `SOC_V1_0_IH_CLIENTID_DF`.
  - Both are `0x17` (`amd/include/soc15_ih_clientid.h:102`, `amd/include/soc_v1_0_ih_clientid.h:42`), so **the branch has no effect**.
- `mmhub_v4_1_0.c` and `gfxhub_v12_0.c` contain no GC 12.0.x version branches.
- **Kext:** no GMC change is needed. `kIP_GMC = {12,0,0}` at `amd/amdgpu_ip.h:49` is already the 12.0.0 value. VRAM size and FB location come from the card (live-dump item).

### IH

- OSSSYS 7.0.0 selects `ih_v7_0` for both chips (`amdgpu_discovery.c:2488`). There are no chip branches in `ih_v7_0.c`.
- **Kext:** no change. `kIP_IH = {7,0,0}` (`amd/amdgpu_ip.h:55`).

### PSP (MP0)

| | Navi 44 | Navi 48 | Source |
|---|---|---|---|
| MP0 | 14.0.2 | 14.0.3 | `psp_v14_0.c:34-39` |
| Firmware | `psp_14_0_2_sos.bin`, `psp_14_0_2_ta.bin` | `psp_14_0_3_{sos,ta}.bin` (+ `_kicker` variants) | `psp_v14_0.c:34-39`; WHENCE |

- Both use `psp_v14_0` (`amdgpu_discovery.c:2552-2554`). Ignore `IP_VERSION(12,0,1)` at `:2528`; that is the Renoir MP0.
- Setup is identical (`amdgpu_psp.c:268-271`): `sup_ifwi_up`, `psp_v14_0` funcs, and `boot_time_tmr` default true.
- Firmware loading: 14.0.2 and 14.0.3 load `sos` + `ta` with no separate TOC file. Only 14.0.5 loads `toc` + `ta` (`psp_v14_0.c:61-90`).
- Kicker SOS/TA applies only to kicker devices (`amdgpu_psp.c:4173-4175`, `:4454-4456`). This never happens on Navi 44.
- **Real difference.** `psp_update_fw_reservation` (`amdgpu_psp.c:1067-1090`) sends `FB_FW_RESERV_ADDR` only if:
  - 14.0.2: `sos.fw_version >= 0x3b0e0d`
  - 14.0.3: `sos.fw_version >= 0x3a0e14`
- The bootloader/SOS/TOC/TA command sequence has no other MP0 14.0.2-vs-14.0.3 branch.
- **Kext:**
  - `src/Navi48Bringup.cpp:43-44` and `:586` reference `fw_psp_14_0_3_sos` directly. Route them through `fw_get(FwId::PSP_SOS)`.
  - `amd/fw_table.cpp:102-104` hardcodes `psp_14_0_3_*`.
  - `Makefile:27-28` (`FW_REQUIRED`) lists `psp_14_0_3_*`.
  - `amd/psp_v14_0.cpp:891` (`psp_query_fw_reservation`) is called unconditionally from `amd/amdgpu_init.cpp:371`. Upstream gates the call on the SOS version threshold. The kext tolerates `PSP_ERR_UNKNOWN_COMMAND` (0x100) at `amd/psp_v14_0.cpp:923-926`, so an older Navi 44 SOS should not break it. For fidelity, add the 14.0.2 / `0x3b0e0d` gate.
  - `amd/psp_v14_0.cpp:700-775` (`psp_setup_tmr`) checks only `kIP_PSP.major == 14` and does LOAD_TOC from the SOS container's TOC sub-bin. It needs a live check that `psp_14_0_2_sos.bin` carries the same sub-bins.
  - `amd/amdgpu_psp.h:148` records SOS `0x003a1014` for Navi 48. The Navi 44 SOS version must be dumped.
  - `src/psp.cpp:279` says "psp_14_0_3 ships no SPDM_DRV". The same needs to be confirmed for `psp_14_0_2_sos.bin`.

### SMU (MP1)

| | Navi 44 | Navi 48 | Source |
|---|---|---|---|
| MP1 | 14.0.2 | 14.0.3 | `smu_v14_0.c:63-65` |
| Firmware | `smu_14_0_2.bin` | `smu_14_0_3.bin` (+ `smu_14_0_3_kicker.bin`) | `smu_v14_0.c:63-65, 83-85`; WHENCE |

- Both use `smu_v14_0` (`amdgpu_discovery.c:2622-2623`) and the **same** ppt file, `smu_v14_0_2_ppt.c` (`amdgpu_smu.c:829-831`; `smu_v14_0_2_ppt.c:2985-2994`).
- Driver IF version `SMU14_DRIVER_IF_VERSION_SMU_V14_0_2 = 0x2E` is shared (`smu_v14_0_2_ppt.c:74`). The PPTable, driver table and metrics layouts are therefore the same.
- Behavior that is the same on both chips:
  - PPTable upload is skipped; PMFW uses its own (`smu_v14_0.c:181-182`).
  - gfxoff handling (`smu_v14_0.c:628-640`).
  - DPM disable is skipped on teardown (`amdgpu_smu.c:2041-2042`).
- Fan control has no 14.0.2-vs-14.0.3 branch in `smu_v14_0_2_ppt.c`. The fan table values come from the PPTable on the card.
- Real differences:
  - `smu_v14_0_2_enable_gfx_features` (`smu_v14_0_2_ppt.c:2131-2140`): on 14.0.2 only, it sends `EnableAllSmuFeatures` with param `FEATURE_PWR_GFX`; 14.0.3 returns `-EOPNOTSUPP`. It is reached only through `smu_enable_gfx_features` → `.asic_reset_enable_gfx_features` (`amdgpu_smu.c:3641-3650`, `:4028`), so this is the **post-reset path, not cold boot**.
  - The energy accumulator is enabled only on 14.0.3 with SMU fw `>= 0x00685000` (`smu_v14_0_2_ppt.c:2198`).
- **Kext:**
  - `amd/fw_table.cpp:124` and the Makefile hardcode `smu_14_0_3`.
  - The kext's boot sequence (`amd/smu_v14_0.cpp` ~420-530) needs no change, because IF 0x2E is shared.
  - `amd/smu_v14_0.cpp:835, 882` verify the metrics layout against `kSMUDriverIfVersion_14_0_2 = 0x2E` (`amd/amdgpu_smu.h:300`). This is correct for Navi 44 as long as its PMFW reports 0x2E (live-dump item).
  - If the kext ever implements a reset/recovery path, it must send the 14.0.2-only `EnableAllSmuFeatures(FEATURE_PWR_GFX)`.

### IMU

- Firmware: `gc_12_0_0_imu.bin` for Navi 44; `gc_12_0_1_imu.bin` (+ `_kicker`) for Navi 48 (`imu_v12_0.c:35-37, 51-55`).
- `imu_v12_0_program_rlc_ram` (`imu_v12_0.c:367-392`) uses `imu_rlc_ram_golden_12_0_1` (`imu_v12_0.c:177-256`) for **both** 12.0.0 and 12.0.1.
  - That table hardcodes `GB_ADDR_CONFIG = 0x08200545`, `GL2_PIPE_STEER_0..3`, and the GCMC FB location.
  - It is used only on the RLC backdoor-autoload path (`gfx_v12_0.c:3766-3815`). The PSP path, which is the default and the kext's path, never writes it.
- **Kext:** `amd/imu_v12_0.cpp:1-80` follows the PSP path and does no IMU MMIO, so no change is needed beyond the firmware name (`amd/fw_table.cpp:106`). Do not copy `0x08200545` from that table as a Navi 44 constant. Read `GB_ADDR_CONFIG` from the card instead, as `amd/gfx_v12_0.cpp:405` already does.

### RLC

- Firmware: `gc_12_0_0_rlc.bin` for Navi 44; `gc_12_0_1_rlc.bin` (+ `rlc_kicker`) for Navi 48 (`gfx_v12_0.c:73-83, 625-628`).
- With PSP loading, `hw_init` waits for RLC autoload and then applies the golden registers. `program_rlc_ram` runs only under `AMDGPU_FW_LOAD_RLC_BACKDOOR_AUTO` (`gfx_v12_0.c:3766-3815`).
- **Kext:** change the firmware name at `amd/fw_table.cpp:120`.

### GFX: golden settings and die geometry

- `gfx_v12_0_init_golden_registers` (`gfx_v12_0.c:3673-3692`) treats 12.0.0 and 12.0.1 the same:
  - Always applies `golden_settings_gc_12_0` (`DB_MEM_CONFIG` 0x8000).
  - Also applies `golden_settings_gc_12_0_rev0` when `adev->rev_id == 0` (`DB_MEM_CONFIG` 0xf, `CB_HW_CONTROL_1` 0x03000000, `GL2C_CTRL5` 0x70→0x20) (`gfx_v12_0.c:253-261`).
- `gpu_early_init` (`gfx_v12_0.c:955-972`) sets identical SC FIFO sizes and `max_hw_contexts` for both. `max_shader_engines`, `max_sh_per_se`, `max_cu_per_sh` and `max_backends_per_se` come from the **discovery `gc_info` table**, not from code.
- `sw_init` (`gfx_v12_0.c:1416-1434`) is the same for both: ME 1 / 1 / 8, MEC 1 / 2 / 4. Clock and power gating are also the same (`gfx_v12_0.c:4142`, `:4381`).
- Branches beyond the boot path: userq firmware-version gates (`:1436-1448`), cleaner shader (`:1452-1463`) and per-queue reset (`:1561-1571`) are keyed on firmware versions. They are not bring-up.
- **Navi 44 die geometry is not in Linux source.** Public RX 9060 XT specs give 32 CUs, which suggests 2 SE × 2 SA × 4 WGP × 2 CU, but the RB count and SE/SA split must come from the card's `gc_info` and harvest registers (live-dump item).
- **Kext:**
  - `amd/gfx_v12_0.cpp:370-376` hardcodes Navi 48 geometry: 4 SE, 2 SA/SE, 4 RB/SE, 8 CU/SA = 64 CU.
  - `:397-401` warns when GC is not 12.0.1.
  - Replace the hardcoded values with discovery `gc_info` values, or at least a 12.0.0 case filled from a dump. Otherwise the CU/RB bitmasks at `amd/gfx_v12_0.cpp:248, 280, 316` cover nonexistent arrays.
  - The kext writes no GFX golden registers; a grep for `DB_MEM_CONFIG` / `GL2C_CTRL5` finds none. That is a pre-existing gap shared with Navi 48, not a Navi 44 issue.
  - Pins: `kIP_GFX = {12,0,1}` and `kIP_MES = {12,0,1}` at `amd/amdgpu_ip.h:48, 53`.
  - Optional shaders are built for gfx1201 (`Makefile:43-44`, `shader_blit_*_gfx1201`). Navi 44 is gfx1200, so the shaders need a gfx1200 build or a check that the ISA is compatible.

### CP

- Firmware: `gc_12_0_0_{pfp,me,mec}.bin` for Navi 44; `gc_12_0_1_*` for Navi 48 (`gfx_v12_0.c:73-83`). The RS64 CP path is the same for both.
- **Kext:** change the firmware names (`amd/fw_table.cpp:108-112`). The comment at `amd/cp_v12_0.cpp:981` about gc_12_0_1 headers needs re-checking against the gc_12_0_0 headers.

### SDMA

- `sdma_7_0_0.bin` for Navi 44; `sdma_7_0_1.bin` for Navi 48 (`sdma_v7_0.c:48-49`). Both are handled the same (`sdma_v7_0.c:1355-1356`; `amdgpu_discovery.c:2833-2834`).
- **Kext:**
  - Change the firmware name (`amd/fw_table.cpp:126`, Makefile).
  - `kIP_SDMA = {7,0,1}` (`amd/amdgpu_ip.h:50`).
  - `amd/amdgpu_sdma.h:315-327` (`sdma_pick_reg_table`) selects the gc_12_1_0 table only for GC 12.1.x and otherwise uses gc_12_0_0. It is already correct for 12.0.0.

### MES

- `mes_v12_0` handles both chips, with `enable_mes`, `enable_mes_kiq` and `uni_mes` (`amdgpu_discovery.c:2994-2995`).
- Firmware: `gc_12_0_0_{mes,mes1,uni_mes}.bin` and `gc_12_0_1_{mes,mes1,uni_mes}.bin` (`mes_v12_0.c:36-41`).
- `amdgpu_mes.c:784, 869-871` checks only `< 12.0.0`.
- **Kext:** change the firmware name (`amd/fw_table.cpp:118`); `amd/mes_v12_1.cpp:1481` uses `fw_get(GC_UNI_MES)` and needs no other change.

### linux-firmware files

Taken from WHENCE on main:

- **Navi 44 needs:**
  - `gc_12_0_0_{imu,me,mec,mes,mes1,pfp,rlc,toc,uni_mes}.bin`
  - `psp_14_0_2_{sos,ta}.bin`
  - `sdma_7_0_0.bin`
  - `smu_14_0_2.bin`
  - Display and video, not used in bring-up: `dcn_4_0_1_dmcub.bin` and `vcn_5_0_0.bin`
- **Navi 48 equivalents:**
  - `gc_12_0_1_{imu,imu_kicker,me,mec,mes,mes1,pfp,rlc,rlc_kicker,toc,uni_mes}.bin`
  - `psp_14_0_3_{sos,sos_kicker,ta,ta_kicker}.bin`
  - `sdma_7_0_1.bin`
  - `smu_14_0_3.bin`, `smu_14_0_3_kicker.bin`
- No `gc_12_0_0_*_kicker`, `psp_14_0_2_*_kicker` or `smu_14_0_2_kicker` files exist.
- **Kext:**
  - Change `Makefile:27-28` `FW_REQUIRED` to `gc_12_0_0_{imu,me,mec,pfp,rlc,uni_mes} psp_14_0_2_{sos,ta} sdma_7_0_0 smu_14_0_2`.
  - Rename the embedded symbols at `amd/fw_table.cpp:45-75` and `:97-128`.
  - The `GC_MES` / `GC_MES1` / `GC_TOC` nullptr entries at `amd/fw_table.cpp:114-122` can stay. Linux also loads no separate TOC for 14.0.2 (`psp_v14_0.c:61-90`).

## Kext change list

The changes the issue asks for:

1. `src/Navi48Bringup.cpp:7335-7338`: the probe accepts only `0x7550` / `0x7551`. Add `0x7590`.
2. `Makefile:27-28, 43-44`, `amd/fw_table.cpp:45-128` and `src/Navi48Bringup.cpp:43-44, 586`: firmware names (see above).
3. `amd/amdgpu_ip.h:48-56`: the `kIP_*` pins become GFX/MES 12.0.0, SDMA 7.0.0, PSP/SMU 14.0.2, and NBIO as dumped. Better, drop the pins in favor of the discovery values already in `IPBaseTable`.
4. `amd/gfx_v12_0.cpp:370-401`: hardcoded 64-CU geometry and the GC 12.0.1 warning.
5. `amd/psp_v14_0.cpp:891` and `amd/amdgpu_init.cpp:371`: optional MP0 14.0.2 SOS `>= 0x3b0e0d` gate for `FB_FW_RESERV_ADDR`.
6. gfx1201 shader blobs need a gfx1200 build or a compatibility check.

## Only learnable from a live dump of the card

- The discovery table: every IP version, especially NBIO (6.3.1 or 7.11.x), the GC revision, IP base addresses and harvest info.
- `gc_info`: SE count, SA/SE, WGP (CU)/SA, RB/SE, and the CC/GC_USER harvest registers for this SKU.
- The `GB_ADDR_CONFIG` value. Do not assume Navi 48's `0x08200545`.
- PCI revision ID. It decides whether `golden_settings_gc_12_0_rev0` applies.
- SOS firmware version (MP0 `C2PMSG_58`) compared with `0x3b0e0d`.
- Sub-bins in the `psp_14_0_2_sos.bin` container: TOC present, SPDM present.
- PMFW driver IF version (expected 0x2E) and SMU firmware version.
- The PPTable contents, including fan table, power limits and DPM levels.
- VRAM size and FB location.
