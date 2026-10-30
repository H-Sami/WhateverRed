// fbread: read the console framebuffer's scanout memory and print it as a block-averaged PPM.
//
// Why: the console-integrity check has to measure what the monitor shows. A WindowServer screenshot
// reads the compositor's buffers, not the scanout, and over SSH it fails anyway (no Screen Recording
// grant). This maps the framebuffer's VRAM aperture (kIOFBVRAMMemory) through the shared IOFramebuffer
// connection (type 1, never WindowServer's type 0) read-only, and only reads it.
//
//   fbread [block [width height rowbytes]]
//     block = side of the averaged square in pixels (default 16 -> 160x90 cells at 2560x1440).
//     The geometry defaults to IOFBConfig.IOFBModes[0].DM (IODisplayModeInformation: nominal width,
//     height) at 4 bytes per pixel, unpadded; pass it explicitly if that is ever wrong.
//
// Output on stdout: "P3 <w> <h> 255" then one "R G B" line per cell, row-major. Exit 1 on any failure.
// Build on the Mac: clang -O2 -Wall -framework IOKit -framework CoreFoundation fbread.c -o fbread
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>

#define kFBSharedConnectType 1   // kIOFBSharedConnectType
#define kFBVRAMMemory 110        // kIOFBVRAMMemory (IOFramebufferShared.h, not in the public SDK)

// Width and height of the first mode in IOFBConfig (the boot framebuffer has exactly one).
static int mode_geometry(io_service_t fb, unsigned *w, unsigned *h) {
    CFTypeRef cfg = IORegistryEntryCreateCFProperty(fb, CFSTR("IOFBConfig"), kCFAllocatorDefault, 0);
    int ok = 0;
    if (cfg && CFGetTypeID(cfg) == CFDictionaryGetTypeID()) {
        CFTypeRef modes = CFDictionaryGetValue((CFDictionaryRef)cfg, CFSTR("IOFBModes"));
        if (modes && CFGetTypeID(modes) == CFArrayGetTypeID() && CFArrayGetCount((CFArrayRef)modes) > 0) {
            CFTypeRef m = CFArrayGetValueAtIndex((CFArrayRef)modes, 0);
            CFTypeRef dm = (m && CFGetTypeID(m) == CFDictionaryGetTypeID())
                               ? CFDictionaryGetValue((CFDictionaryRef)m, CFSTR("DM")) : NULL;
            if (dm && CFGetTypeID(dm) == CFDataGetTypeID() && CFDataGetLength((CFDataRef)dm) >= 8) {
                uint32_t v[2];
                CFDataGetBytes((CFDataRef)dm, CFRangeMake(0, 8), (UInt8 *)v);
                *w = v[0]; *h = v[1]; ok = *w > 0 && *h > 0;
            }
        }
    }
    if (cfg) CFRelease(cfg);
    return ok;
}

int main(int argc, char **argv) {
    unsigned block = argc > 1 ? (unsigned)strtoul(argv[1], NULL, 0) : 16;
    if (block < 1 || block > 256) { fprintf(stderr, "fbread: block must be 1..256\n"); return 1; }
    io_service_t fb = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("IOFramebuffer"));
    if (!fb) { fprintf(stderr, "fbread: no IOFramebuffer\n"); return 1; }
    unsigned w = 0, h = 0, rb = 0;
    if (argc > 4) {
        w = (unsigned)strtoul(argv[2], NULL, 0); h = (unsigned)strtoul(argv[3], NULL, 0);
        rb = (unsigned)strtoul(argv[4], NULL, 0);
    } else if (mode_geometry(fb, &w, &h)) {
        rb = w * 4;
    } else { fprintf(stderr, "fbread: no mode geometry in IOFBConfig; pass width height rowbytes\n"); return 1; }
    io_connect_t conn = 0;
    kern_return_t kr = IOServiceOpen(fb, mach_task_self(), kFBSharedConnectType, &conn);
    if (kr) { fprintf(stderr, "fbread: IOServiceOpen(shared) kr=0x%x\n", kr); return 1; }
    mach_vm_address_t addr = 0; mach_vm_size_t size = 0;
    kr = IOConnectMapMemory64(conn, kFBVRAMMemory, mach_task_self(), &addr, &size,
                              kIOMapAnywhere | kIOMapReadOnly);
    if (kr) { fprintf(stderr, "fbread: map kIOFBVRAMMemory kr=0x%x\n", kr); return 1; }
    if ((uint64_t)rb * h > size || (uint64_t)w * 4 > rb || w < block || h < block) {
        fprintf(stderr, "fbread: %ux%u rowbytes %u does not fit map 0x%llx\n", w, h, rb, size);
        return 1;
    }
    unsigned ow = w / block, oh = h / block;
    fprintf(stderr, "fbread: %ux%u rowbytes %u map 0x%llx -> %ux%u cells\n", w, h, rb, size, ow, oh);
    printf("P3\n%u %u\n255\n", ow, oh);
    const volatile uint8_t *base = (const volatile uint8_t *)(uintptr_t)addr;
    for (unsigned oy = 0; oy < oh; oy++)
        for (unsigned ox = 0; ox < ow; ox++) {
            uint64_t r = 0, g = 0, b = 0;
            for (unsigned y = oy * block; y < (oy + 1) * block; y++) {
                const volatile uint32_t *row = (const volatile uint32_t *)(base + (uint64_t)y * rb);
                for (unsigned x = ox * block; x < (ox + 1) * block; x++) {
                    uint32_t p = row[x];   // x8r8g8b8, little-endian
                    r += (p >> 16) & 0xff; g += (p >> 8) & 0xff; b += p & 0xff;
                }
            }
            uint64_t n = (uint64_t)block * block;
            printf("%llu %llu %llu\n", r / n, g / n, b / n);
        }
    IOConnectUnmapMemory64(conn, kFBVRAMMemory, mach_task_self(), addr);
    IOServiceClose(conn);
    return 0;
}
