# WhateverRed

WhateverRed brings Cobalt64's macOS GPU driver stack up on one card, the Radeon RX 9060 XT, on one PC.

## Hardware

**Navi 44**:
The GPU die in the RX 9060 XT (PCI 1002:7590, gfx1200, GC 12.0.0). The only target of this project.
_Avoid_: 9060, RDNA4 (when the die is meant)

**Navi 48**:
The GPU die in the RX 9070 XT and R9700 (gfx1201, GC 12.0.1) that upstream Cobalt64 was built and tested on. Reference only, never a target here.

**Test bench**:
The single PC this project runs on: RX 9060 XT 16 GB, MSI B550 Gaming Plus, Ryzen 7 5700X3D, 32 GB DDR4, macOS dual-booted with Windows through OpenCore, driven over SSH from a separate laptop.
_Avoid_: test PC, rig

## Driver stack

**Upstream**:
The Cobalt64 project (github.com/Almosst-DEV/Cobalt64) this repo is based on.

**Bring-up kext**:
The kernel extension that takes the GPU from power-on to a usable device: PCIe setup, IP discovery, firmware load and engine initialisation, display mode-setting.
_Avoid_: driver (ambiguous), Navi48Bringup (upstream's name for the binary)

**Native route**:
The path where Metal runs on Mesa's RADV Vulkan driver, ported to macOS, on top of the bring-up kext.

**Substitution route**:
Upstream's older path that swaps shaders inside Apple's own AMD accelerator kext using Apple-derived shader files. Out of scope.

**Compute test**:
Upstream's first end-to-end GPU check: a shader dispatched through the command processor writes a known value to memory that the CPU reads back.

**GPU-composited desktop**:
The macOS desktop with WindowServer's Metal compositor running on this driver stack instead of the CPU.
_Avoid_: acceleration, QE/CI
