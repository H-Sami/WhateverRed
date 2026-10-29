# The Metal bundle's missing spvcache

**Question** ([#11](https://github.com/H-Sami/WhateverRed/issues/11)): what is in `tools/native/navi48metal/spvcache/`, how is it keyed and consumed, how did upstream produce it, can the owner regenerate it on macOS Tahoe 26.7.2, is any of it GPU-specific, and what is the smallest change that lets `build.sh` succeed without it?

**Source:** this repository at `4c57b01` (the tree imported from Cobalt64). Every claim cites `path:line`. One fact comes from upstream metal2vulkan at the pinned commit `43c46ac8`; it is marked **(m2v)**. It was read from `raw.githubusercontent.com/steelbrain/metal2vulkan/43c46ac8.../README.md`. Nothing here was run on macOS.

## Answer in brief

- **Contents.** The cache holds `<sha256>.spv` (SPIR-V) and `<sha256>.meta.json` (trimmed reflection) for Apple's own system shaders. These come from SkyLight (`SkyLightShaders.air64`, the WindowServer compositor), QuartzCore (Core Animation), CoreDisplay and CoreImage, plus any AIR dumped at run time.
- **Key.** The key is the sha256 of `-[MTLFunction bitcodeData]`, which is Apple's AIR bitcode for that function. It is therefore tied to the macOS build. It has nothing to do with the GPU.
- **Consumers.** Every pipeline creation reads it first. A miss in WindowServer gives a magenta (render) or no-op (compute) fallback pipeline and dumps the AIR. A daemon on the PC then translates the dump, and a hot-swap replaces the fallback. Admitted applications translate in process through `libn48xlate`. **WindowServer never translates in process**, so in-process translation does not make the cache optional for the desktop. The daemon does, at the cost of magenta first frames.
- **Regeneration.** It can be regenerated on 26.7.2, and it should be: a 26.6.2 cache would mostly miss on 26.7.2 anyway. All of it runs on macOS.
- **Smallest build fix.** Add zsh's `(N)` null-glob qualifier to the two globs on `build.sh:31`.

## 1. What spvcache contains

`fill-spvcache.py` is the bulk filler. It installs translations from a "census" of four Apple libraries (`tools/native/navi48metal/fill-spvcache.py:5`, `:13`):

| Census library | What it is |
|---|---|
| `SkyLight__SkyLightShaders.air64` | WindowServer's compositor shaders. The file is `/System/Library/PrivateFrameworks/SkyLight.framework/Versions/A/Resources/SkyLightShaders.air64.metallib` (`tools/native/mtlprobe/mtlprobe.m:1178`). |
| `QuartzCore__default` | Core Animation, the "AIR slice" of its default metallib |
| `CoreDisplay__default` | CoreDisplay |
| `CoreImage__default` | Core Image default kernels |

It copies only functions that the census classed `OK` (`fill-spvcache.py:20`). Each one must start with the SPIR-V magic (`:26`), and two libraries that share a key must have byte-identical SPIR-V (`:27-29`). It writes no `.meta.json` sidecar (`:9`).

`add-air.py` is the incremental filler. It translates dumped AIR files into `<sha>.spv` plus `<sha>.meta.json` (`tools/native/navi48metal/add-air.py:2`, `:72-78`). The meta is a trimmed copy of metal2vulkan's ShaderReflection, covering stage, entry point, bindings, static samplers, vertex attributes, local size, kernel dispatch and function constants (`:7-9`, `:23-30`). The bundle reads static samplers and the compute dispatch contract from that sidecar (`tools/native/navi48metal/Navi48Device.m:3179`). The bundle reflects everything else from the SPIR-V itself (`fill-spvcache.py:9`).

The cache also absorbs whatever the auto-translator receives. That covers WindowServer misses (`tools/native/autotranslate/air-pull.sh:3-6`) and the probe's own test shaders (`mtlprobe dumpair`, `tools/native/mtlprobe/mtlprobe.m:1144-1146`).

The census's measured size was 275 functions, whose file hash equalled the `bitcodeData` hash for 275/275 (`fill-spvcache.py:6-7`, `add-air.py:5`).

The tree ships none of it. `git ls-files | grep spvcache` lists only `fill-spvcache.py`. The census tooling and its inputs (`~/navi48-native/census2/{air,res}/<lib>/`, `results.tsv`; `fill-spvcache.py:12`, `:17`, `:21-22`) are not in the tree either. Neither is the `re/` work area that `air-pull.sh:10` writes to. The same policy applies on the metal2vulkan side: "No build outputs and no test data derived from Apple software are included" (`third-party/metal2vulkan-n48/NOTICE:30-32`).

## 2. How entries are keyed

- The key is `sha256(bitcodeData)` as lowercase hex (`Navi48Device.m:1919-1922`, `:3825`). The file names are `<key>.spv` and `<key>.meta.json` (`:3830`, `:3567`).
- For a linked stage (build 18), the bundle looks up a "linked" key first, made from the entry plus its sorted dependencies, and then the entry's own sha (`Navi48Device.m:3827-3829`, `:3386-3387`).
- The key contains **no device ID, no ISA and no OS version**. It is purely the hash of Apple's AIR bytes. Those bytes come from the system frameworks, so the key changes whenever Apple ships a changed metallib. The cache is therefore effectively per macOS build. Upstream's work dir was named `re/pc-26.6.2` (`air-pull.sh:10`), and its `llvm-dis` stand-in pins the triple `air64_v28-apple-macosx26.6.0` (`tools/native/autotranslate/n48-llvm-dis:10`). The triple is cosmetic: metal2vulkan's sanitizer rewrites it (`n48-llvm-dis:4-5`).
- The in-process user cache is separate. Its directory key `K` hashes the libn48xlate UUID, the reader identity (the dyld shared cache UUID), the override table and `kern.osversion` (`tools/native/navi48metal/n48_xlate.h:126-139`; `Navi48Device.m:3339-3346`). So that cache is explicitly per OS build.

## 3. Where it is consumed, and what a miss does

**Lookup order.** The bundle first checks its own `Resources/spvcache`. Then, for in-process applications only, it checks the user cache `K`. Last it checks the side directory `/private/var/tmp/n48m-spv` (`Navi48Device.m:3536-3539`, `:3546-3561`). The first valid file wins (`:3830-3833`). A miss returns error 41 (`:3837`).

**Render pipeline** (`Navi48Device.m:3877-3921`):
1. The bundle looks up both stages (`:3880-3881`).
2. If this process translates in process, it runs `n48x_translate_fn` and looks up again (`:3885-3895`).
3. If both stages are still not found and the fallback is allowed, it dumps the AIR to `/tmp/n48m/<uid>/<sha>.air` with a JSON sidecar (`:3902`, `:2005-2016`). Applications may then wait up to 3 s for the daemon (`:3903-3914`).
4. If that also fails, the result is `FALLBACK` with built-in magenta shaders (`:3915-3918`; `n48_fallback_spv.h:1`, generated by `fallback/gen.sh`). Without a fallback, the creation fails (`:3920`).

**Compute pipeline.** A miss in WindowServer gives a valid no-op pipeline, because MPS aborts on a pipeline error (`Navi48Device.m:6071-6076`). The dispatch is then skipped (`:5721`).

**Who may fall back.** WindowServer, forced test modes and admitted applications may fall back (`tools/native/navi48metal/n48_gate.h:145`).

**Hot-swap.** A fallback pipeline registers its keys. A 0.5 s watcher stats the lookup directories and rebuilds the pipeline when `<sha>.meta.json` and `<sha>.spv` appear (`Navi48Device.m:3681-3685`).

**The on-PC daemon.** `pc-translate.sh` is the LaunchDaemon `com.navi48.translate`. It runs as `nobody` from `/usr/local/navi48/` (`tools/native/autotranslate/com.navi48.translate.plist:5-8`; `pc-translate.sh:2`). It polls `/private/tmp/n48m/<uid>/*.air` and skips anything already in the bundle's spvcache (`pc-translate.sh:3-4`, `:17`). It translates under a sandbox with CPU and memory caps (`:5-6`) and installs into `/private/var/tmp/n48m-spv`, meta first and spv last (`:14`; `pc-translate.py:2-6`). Its inputs are a `metal2vulkan` binary, `n48-llvm-dis` (built on CommandLineTools clang, `n48-llvm-dis:7-8`), `overrides.txt` and an optional real `spirv-val`. Without `spirv-val` it uses a stub that always says "valid" (`pc-translate.sh:28-30`, `:88`).

**Does libn48xlate make the cache optional?** Only for applications. `n48x_applies` returns 0 for WindowServer unconditionally (`n48_xlate.h:74-75`). The comments state that "WindowServer NEVER applies: it keeps the precompiled spvcache plus the daemon and loads neither library" (`Navi48Device.m:3296`; `n48_xlate.h:7-8`). Admitted applications translate a miss with GPUCompiler's libLLVM plus `Resources/libn48xlate.dylib` (`Navi48Device.m:3294-3296`; `CHANGES.md:38-43`).

**So, with an empty spvcache:**
- The bundle still loads. A missing file is just a miss (`Navi48Device.m:3830-3837`).
- Applications translate in process.
- WindowServer starts with magenta or no-op pipelines for every shader. With the daemon installed, each pipeline is swapped in after one translation, which has a 20 s wall-clock cap per shader (`pc-translate.sh:5-6`). Without the daemon the desktop stays magenta.

The precompiled cache is therefore an optimisation for the first boot and for robustness. It is not required for correctness, provided the daemon runs.

## 4. How upstream produced it

1. **Census (bulk).** AIR was extracted per function from the four system metallibs into `census2/air/<lib>/`. Each function was translated and classified in `census2/res/<lib>/results.tsv` and `spv/`, then installed with `fill-spvcache.py` (`fill-spvcache.py:2-4`, `:16-31`). The extractor and the classifier are **not in the tree**. `mtlprobe libhash <metallib>` survives: it prints every function's name, stage and `sha256(bitcodeData)`, which is the spvcache key (`mtlprobe.m:445-447`, `:3697-3706`). `xlate-corpus-prep.py` also consumes a census2 dir (`tools/native/navi48metal/xlate-corpus-prep.py:3`, `:32-37`).
2. **Incremental.** WindowServer misses were dumped on the PC and pulled to the host Mac over ssh by `air-pull.sh` (`air-pull.sh:2-6`, `:17-22`). They were translated by `add-air.py`, which runs metal2vulkan (`--emit-meta`, `--local` from overrides) and then `spirv-val --target-env vulkan1.2` (`add-air.py:6`, `:59-64`). A storage-image format patch follows (`:31-52`, `:67-71`). The results were pushed back to the side dir and also written to `navi48metal/spvcache` "for the next bundle build" (`air-pull.sh:6`, `:26-30`).

`add-air.py` expects a metal2vulkan CLI from a private `navi48-s0-fixes` branch at `~/navi48-native/metal2vulkan/target/release/metal2vulkan` (`add-air.py:6`, `:14`). The public replacement is upstream `43c46ac8` plus `third-party/metal2vulkan-n48/patches/0001-*.patch` (`third-party/metal2vulkan-n48/README.txt:2-6`; `NOTICE:11`, `:17-21`). The CLI is built with the `serde` feature, which provides `--emit-meta` and `--local` **(m2v README:39-40, :56, :67, :92)**.

## 5. Regenerating on the owner's Mac (Tahoe 26.7.2)

Yes. Every step is macOS-only, and it has to run against the 26.7.2 frameworks, because the keys are hashes of that build's AIR.

**Route A: let the bench fill it (no census tooling needed).**
1. On a Mac, build the metal2vulkan CLI: upstream `43c46ac8`, plus the patch, built with `cargo build --release --features serde`, for x86_64. Do the same for `libn48xlate` (`README.txt:1-6`; `build.sh:15-25`).
2. Install the daemon on the PC. That means `pc-translate.sh`, `pc-translate.py`, `n48-llvm-dis`, `overrides.txt`, `n48-translate.sb` and the `metal2vulkan` binary, ideally with a real x86_64 `spirv-val`, all in `/usr/local/navi48/`. Then load `com.navi48.translate.plist` (`pc-translate.sh:2`, `:28-32`).
3. Build the bundle with an empty spvcache (see section 7) and boot WindowServer on it. Every miss is dumped, translated and hot-swapped.
4. Copy the PC's `/private/var/tmp/n48m-spv/*` back into `tools/native/navi48metal/spvcache/` for the next build. Alternatively, run `add-air.py` on the `/tmp/n48m/<uid>/*.air` dumps on the host Mac (`add-air.py:2-4`).

**Route B: pre-seed before first boot.**
- `mtlprobe libhash` lists the keys of each system metallib (`mtlprobe.m:445-447`).
- To translate them, the per-function AIR has to be written out. `dumpair` covers only the probe's own shaders (`mtlprobe.m:1144-1156`), so this needs a small new `mtlprobe` subcommand that combines `libhash` and `dumpair`: open a metallib and write every function's `bitcodeData` to `<sha>.air` plus `<sha>.txt`.
- Then run `add-air.py <dir>`.
- This runs on any 26.7.2 install: `bitcodeData` is AIR from the system file, measured equal to the file hash (`fill-spvcache.py:6-7`).
- Assumption, not verified here: it may need a Metal device to open the library.

Route B is optional. Route A is fully supported by the existing code.

## 6. Is any of it GPU-specific (gfx1201 vs gfx1200)?

**spvcache: no.**
- Its keys are AIR hashes (section 2).
- Its payload is SPIR-V validated against `vulkan1.2` (`add-air.py:63`) plus reflection JSON (`add-air.py:23-30`).
- None of `add-air.py`, `fill-spvcache.py` or the autotranslate scripts mentions gfx12xx or a PCI device ID. A grep for `gfx120|0x7550|0x7590|navi4` finds nothing.
- The kernel threadgroup sizes in `overrides.txt` (`tools/native/autotranslate/overrides.txt:1-4`) are Metal-semantic nominal sizes, not tuning for one GPU.
- RADV compiles the SPIR-V for whatever device it opens. A gfx1201 cache would be valid on gfx1200.

**Related caches that are GPU-specific, not to be confused with spvcache:**
- **The T2 Vulkan pipeline cache** `/private/var/tmp/n48m-pipecache.bin` holds RADV-compiled ISA. The bundle records `pipelineCacheUUID`, `vendorID` and `deviceID` and keys the cache on the bundle version and the RADV dylib (`Navi48Device.m:368-383`). It is regenerated at run time and not shipped. Delete it after any GPU swap; kill file `/private/tmp/n48m-nopcache` (`:381`).
- **`src/shadercache`** is a hash-keyed **gfx1201 ISA** cache for the older Apple-accelerator route (`src/shadercache/shadercache.h:2`, `:68`). Its blob is optional. When it is absent the build embeds a zero-length blob and substitution stays off. The native route does not use it (`src/navi48-bringup/Makefile:38-44`). For Navi 44 it would be wrong ISA, but it is off by default.

## 7. Smallest change to make build.sh succeed without it

`build.sh` is zsh (`tools/native/navi48metal/build.sh:1`; `${f:t}` on `:31` is a zsh modifier). In zsh an unmatched glob is a "no matches found" error, so with `set -euo pipefail` (`:6`) the `for` line on `:31` aborts the script. This refines the earlier note that the `cat` fails. The fix is the `(N)` null-glob qualifier on both patterns:

```zsh
mkdir -p "$APP/Contents/Resources/spvcache"; for f in "$D"/spvcache/*.spv(N) "$D"/spvcache/*.meta.json(N); do cat "$f" > "$APP/Contents/Resources/spvcache/${f:t}"; done
```

It is a one-line change. The bundle then ships an empty `Resources/spvcache`. The runtime treats that as all misses (section 3), and `pc-translate.sh` simply finds nothing to skip (`pc-translate.sh:17`). It would help to also print a warning when the directory is empty, because WindowServer will then rely entirely on the daemon.

## Must run on the Mac

- Building the metal2vulkan CLI and `libn48xlate` (x86_64 for the PC).
- Installing the daemon on the PC and collecting the dumps from `/tmp/n48m/<uid>/` and the side dir.
- `add-air.py`, which needs metal2vulkan, `llvm-dis` and `spirv-val` from Homebrew (`add-air.py:14-15`).
- Optionally, `mtlprobe libhash` on 26.7.2's SkyLight, QuartzCore, CoreDisplay and CoreImage metallibs, to know the full key set before first boot.

## Surprises

- In-process translation deliberately excludes WindowServer (`n48_xlate.h:74-75`). The desktop depends on the daemon, not on `libn48xlate`.
- `air-pull.sh` is stale. It lists `/private/tmp/n48m/*.air` flat (`air-pull.sh:17`), but since bundle build 6 the dumps go into per-uid subdirectories (`n48_gate.h:147-151`). Use `pc-translate.sh` (the on-PC daemon) instead.
- Without a real `spirv-val`, the daemon validates with a stub that always passes (`pc-translate.sh:28-30`).
- The census extractor that produced most of upstream's cache is not in the tree. Only its consumer (`fill-spvcache.py`) and `mtlprobe libhash` remain.
