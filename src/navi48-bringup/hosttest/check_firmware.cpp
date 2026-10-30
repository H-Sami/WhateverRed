// check_firmware.cpp - ticket #17: the build's firmware gate (make check-firmware runs it before any object compiles).
//   clang++ -std=c++17 -Wall -Wextra -Werror -O1 -I src/amd hosttest/check_firmware.cpp -o build/check-firmware
//   build/check-firmware firmware
// Every blob in the Navi 44 set (n44id::kFirmware) must be in <dir> and carry its pinned version (n44id::check_firmware):
// a missing blob, a Navi 48 name instead of the Navi 44 one, or another linux-firmware revision fails the build.
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include "navi44_identity_pure.h"

int main(int argc, char **argv) {
    const std::string dir = argc > 1 ? argv[1] : "firmware";
    int bad = 0;
    for (const n44id::FwSpec &f : n44id::kFirmware) {
        std::ifstream in(dir + "/" + f.name + ".bin", std::ios::binary);
        if (!in) { std::printf("MISSING  %s.bin\n", f.name); bad++; continue; }
        std::stringstream ss; ss << in.rdbuf(); const std::string s = ss.str();
        const uint32_t r = n44id::check_firmware(f, reinterpret_cast<const uint8_t *>(s.data()), s.size());
        if (r == n44id::kOk) std::printf("ok       %s.bin\n", f.name);
        else { std::printf("BAD      %s.bin: %s\n", f.name, n44id::reason_name(r)); bad++; }
    }
    if (bad) {
        std::printf("error: %d firmware blob(s) missing or not the pinned version in %s/\n", bad, dir.c_str());
        std::printf("       copy them from linux-firmware commit %s: tools/fetch-firmware.sh <checkout> (see BUILDING.md)\n",
                    n44id::kLinuxFirmwareCommit);
        return 1;
    }
    std::printf("firmware: the Navi 44 set at linux-firmware %.12s (SMU %u.%u.%u, SOS %#x)\n", n44id::kLinuxFirmwareCommit,
                n44id::smu_major(n44id::kSmuVersion), n44id::smu_minor(n44id::kSmuVersion), n44id::smu_patch(n44id::kSmuVersion),
                n44id::kSosVersion);
    return 0;
}
