#!/bin/zsh
# navi44_identity_plant.sh - planted breaks for tests/navi44_identity_test.cpp (ticket #16, spec #14: the Navi 44 identity module).
# For each plant: copy the real sources into a scratch tree, apply the break (exact-once replacements; the script first proves each replaced text occurs exactly once),
# compile the real test against the scratch tree and demand that it FAILS (a compile error, a failing check, a crash or a hang cut off by the alarm all count).
# A plant the suite lets through is a hole: the script exits non-zero and says which. The CONTROL (no break) must pass.
#   run from anywhere:  src/navi48-bringup/tests/navi44_identity_plant.sh [first-plant-id last-plant-id]
set -u
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
K=src/navi48-bringup
SR=$K/src
SCR="${TMPDIR:-/tmp}/n44id-plant.$$"
trap 'rm -rf "$SCR"' EXIT
escaped=0; total=0
FIRST=${1:-0}; LAST=${2:-9999}

fresh() {
  rm -rf "$SCR"; mkdir -p "$SCR/$K/tests" "$SCR/tools/conductor"
  rsync -a --exclude fw "$ROOT/$SR/" "$SCR/$SR/"          # the generated firmware arrays are not read by any pin
  cp "$ROOT/$K/Info.plist" "$SCR/$K/"
  cp "$ROOT/$K/tests/navi44_identity_test.cpp" "$SCR/$K/tests/"
  cp "$ROOT/tools/conductor/suites.sh" "$SCR/tools/conductor/"
}
build_run() {   # prints the verdict line; returns 0 if the suite PASSED, 1 if it failed, 2 if it did not compile
  local out
  out=$(cd "$SCR" && clang++ -std=c++17 -Wall -Wextra -O0 -I $SR -I $SR/amd $K/tests/navi44_identity_test.cpp -o "$SCR/t" 2>&1)
  if [ $? -ne 0 ]; then echo "CAUGHT at compile time: $(echo "$out" | /usr/bin/grep -m1 -E 'error|static assertion' | cut -c1-140)"; return 2; fi
  out=$(cd "$SCR" && perl -e 'alarm 120; exec @ARGV' ./t . 2>&1)
  local trc=$?
  if [ $trc -eq 0 ]; then echo "the suite passed: $(echo "$out" | tail -1)"; return 0; fi
  if [ $trc -ge 128 ]; then echo "CAUGHT by a crash or hang (signal $((trc-128)))"; return 1; fi
  echo "CAUGHT by $(echo "$out" | /usr/bin/grep -c '^FAIL') check(s); first: $(echo "$out" | /usr/bin/grep -m1 '^FAIL' | cut -c1-120)"; return 1
}
plant() {   # plant <id> <file relative to repo root> <description> <old1> <new1> [<old2> <new2> ...]
  local id="$1" file="$2" desc="$3"; shift 3
  if [ "$id" -lt "$FIRST" ] || [ "$id" -gt "$LAST" ]; then return; fi
  fresh
  local pairs=""
  while [ $# -ge 2 ]; do pairs="${pairs}${1}"$'\x1e'"${2}"$'\x1d'; shift 2; done
  PAIRS="$pairs" FILE="$SCR/$file" python3 - <<'PY'
import os,sys
p=os.environ['FILE']; s=open(p).read()
for pair in os.environ['PAIRS'].split('\x1d'):
    if not pair: continue
    o,n=pair.split('\x1e')
    if s.count(o)!=1:
        print("PLANT TEXT NOT FOUND EXACTLY ONCE (%d): %r" % (s.count(o), o[:80])); sys.exit(3)
    s=s.replace(o,n)
open(p,'w').write(s)
PY
  local rc=$?
  total=$((total+1))
  if [ $rc -ne 0 ]; then echo "PLANT $id: $desc: THE PLANT ITSELF DID NOT APPLY"; escaped=$((escaped+1)); return; fi
  local msg; msg=$(build_run); local brc=$?
  if [ $brc -eq 0 ]; then echo "PLANT $id: $desc: *** ESCAPED ($msg) ***"; escaped=$((escaped+1)); else echo "PLANT $id: $desc: $msg"; fi
}
control() {
  fresh; total=$((total+1))
  local msg; msg=$(build_run); local brc=$?
  if [ $brc -eq 0 ]; then echo "CONTROL navi44_identity_test (no break): $msg"; else echo "CONTROL navi44_identity_test (no break): *** FAILED: $msg ***"; escaped=$((escaped+1)); fi
}
P=$SR/amd/navi44_identity_pure.h
B=$SR/Navi48Bringup.cpp
H=$SR/Navi48Bringup.hpp
PL=$K/Info.plist
IP=$SR/amd/amdgpu_ip.h
PEER=$SR/apple/Navi48AccelPeer.cpp
SU=tools/conductor/suites.sh

control
# --- device ID
plant 1  $P "the module's device ID is Navi 48's" 'kDeviceId = 0x7590;' 'kDeviceId = 0x7550;'
plant 2  $P "accepts_device ignores the vendor" 'return (vendorDevice & 0xffffu) == kVendorId && (vendorDevice >> 16) == kDeviceId;' 'return (vendorDevice >> 16) == kDeviceId;'
plant 3  $P "accepts_device accepts the whole 759x family" '(vendorDevice >> 16) == kDeviceId;' '((vendorDevice >> 16) & 0xfff0u) == kDeviceId;'
plant 4  $P "the IOPCIPrimaryMatch string keeps Navi 48" 'kPciPrimaryMatch = "0x75901002";' 'kPciPrimaryMatch = "0x75501002 0x75901002";'
# --- IP versions
plant 5  $P "GC is Navi 48's 12.0.1" 'kGc     = { 12, 0, 0 };' 'kGc     = { 12, 0, 1 };'
plant 6  $P "PSP is Navi 48's 14.0.3" 'kPsp    = { 14, 0, 2 };' 'kPsp    = { 14, 0, 3 };'
plant 7  $P "SMU is Navi 48's 14.0.3" 'kSmu    = { 14, 0, 2 };' 'kSmu    = { 14, 0, 3 };'
plant 8  $P "SDMA is Navi 48's 7.0.1" 'kSdma   = {  7, 0, 0 };' 'kSdma   = {  7, 0, 1 };'
plant 9  $P "NBIF is Navi 48's 7.11.0" 'kNbif   = {  6, 3, 1 };' 'kNbif   = {  7, 11, 0 };'
plant 10 $P "UMC revision off by one" 'kUmc    = {  8, 14, 0 };' 'kUmc    = {  8, 14, 1 };'
plant 11 $P "MMHUB filed under the wrong hardware ID" '{  34, "MMHUB", kMmhub }' '{  35, "MMHUB", kMmhub }'
plant 12 $P "expected_ip answers with the first entry for any ID" 'if (e.hwId == hwId) return &e.ver;' 'if (e.hwId != 0) return &e.ver;'
plant 13 $P "same_ver ignores the revision" 'a.minor == minor && a.rev == rev' 'a.minor == minor'
# --- gc_info parse
plant 14 $P "the table list ignores the v2 header" 'rd16(bin + 4) >= 2 ? 16 : 12' '12'
plant 15 $P "the binary signature is not checked" ' || rd32(bin) != kBinarySignature' ''
plant 16 $P "the GC table is table_info[0]" 'kGcTableIndex    = 1;' 'kGcTableIndex    = 0;'
plant 17 $P "a missing GC table is not reported" '    if (off == 0) { g.reason = kNoGcTable; return g; }
' ''
plant 18 $P "the GC table id is not checked" '    if (rd32(gc) != kGcTableId) { g.reason = kGcBadId; return g; }
' ''
plant 19 $P "a GC table past the buffer is read" 'if (size < 12 || size > len - off)' 'if (size < 12)'
plant 20 $P "the GC checksum is not checked" '    if (sum != want) { g.reason = kGcBadChecksum; return g; }
' ''
plant 21 $P "a v1 table too short for sa_per_se is read" 'if (size < 80)' 'if (size < 12)'
plant 22 $P "gc_info 3.x is read as 1.x" 'if (g.verMajor == 1) {' 'if (g.verMajor == 1 || g.verMajor == 3) {'
plant 23 $P "v1 CU per SA counts only wgp0" '2 * (wgp0 + wgp1)' '2 * wgp0'
plant 24 $P "v1 SA per SE read from gc_num_sc_per_se" 'sa = rd32(gc + 76)' 'sa = rd32(gc + 72)'
plant 25 $P "v1 RB is per SE, not total" 'g.geo = { se, sa, 2 * (wgp0 + wgp1), se * rbPerSe };' 'g.geo = { se, sa, 2 * (wgp0 + wgp1), rbPerSe };'
plant 26 $P "v2 swaps CU per SH and SH per SE" 'g.geo = { se, sh, cu, se * rbPerSe };' 'g.geo = { se, cu, sh, se * rbPerSe };'
# --- geometry check
plant 27 $P "the geometry is Navi 48's RB count" 'kGeometry = { 2, 2, 8, 8 };' 'kGeometry = { 2, 2, 8, 16 };'
plant 28 $P "check_geometry skips SA" '         : g.saPerSe != kGeometry.saPerSe ? kSaMismatch
' ''
plant 29 $P "an RB mismatch reports CU" '? kRbMismatch' '? kCuMismatch'
plant 30 $P "check_geometry accepts SE one above" 'return g.se      != kGeometry.se      ? kSeMismatch' 'return (g.se != kGeometry.se && g.se != kGeometry.se + 1) ? kSeMismatch'
plant 31 $P "the RB reason names the CUs" 'return "render backend count mismatch";' 'return "CU per SA mismatch";'
# --- the kext consumes the module
plant 32 $B "probe still accepts Navi 48" 'if (!n44id::accepts_device(vendorDevice)) {' 'if (!n44id::accepts_device(vendorDevice) && (vendorDevice >> 16) != 0x7550) {'
plant 33 $B "probe still logs Navi 48" 'not Navi 44, ignoring' 'not Navi 48, ignoring'
plant 34 $B "the stage-1 gate does not gate" 'if (stageIPDiscovery()) {' 'stageIPDiscovery(); {'
plant 35 $B "surveyPSP runs before the gate" '		surveyIPs();
' '		surveyIPs();
		surveyPSP();
'
plant 36 $B "stage 1 parses from the TMR start, not the binary" 'n44id::parse_gc_info(onDieDisc + ipDiscovery.binaryOffset(),' 'n44id::parse_gc_info(onDieDisc,'
plant 37 $B "stage 1 does not check the geometry" 'n44id::check_geometry(gi.geo)' 'n44id::kOk'
plant 38 $B "stage 1 does not publish StageReached" '		setProperty("Navi48,StageReached", static_cast<uint64_t>(reached), 32);
' ''
plant 39 $B "stage 1 does not publish the geometry string" '		setProperty("Navi48,Geometry", geo);
' ''
plant 40 $B "stage 1 does not publish the RB count" '		setProperty("Navi48,GeometryRB", static_cast<uint64_t>(gi.geo.rb), 32);
' ''
plant 41 $B "surveyIPs compares nothing" 'n44id::expected_ip(w.id)' 'static_cast<const n44id::IpVer *>(nullptr)'
plant 42 $B "stage 1 names its stage by hand" 'setProperty("Navi48,StageFailed", amdgpu::stage_name(amdgpu::BringupStage::IPDiscovery));' 'setProperty("Navi48,StageFailed", "IPDiscovery");'
plant 43 $B "Navi48Bringup.cpp does not include the module" '#include "amd/navi44_identity_pure.h"' '#include "amd/amdgpu_ip.h"'
plant 44 $B "navi48-stage is parsed after the gate" '		if (!PE_parse_boot_argn("navi48-stage", &targetStage, sizeof(targetStage))) targetStage = 0;
		if (targetStage) N48LOG("stages: navi48-stage=%u (mac-amdgpu BringupStage numbering; 5 = PSP SOS)", targetStage);
		surveyIPs();
' '		surveyIPs();
		if (stageIPDiscovery()) {}
		if (!PE_parse_boot_argn("navi48-stage", &targetStage, sizeof(targetStage))) targetStage = 0;
		if (targetStage) N48LOG("stages: navi48-stage=%u (mac-amdgpu BringupStage numbering; 5 = PSP SOS)", targetStage);
'
plant 45 $H "the header does not declare the stage-1 gate" 'bool stageIPDiscovery();' 'void stageIPDiscoveryUnused();'
plant 46 $PL "the bring-up personality is back on Navi 48" '<string>0x75901002</string>' '<string>0x75501002 0x75511002</string>'
plant 47 $PL "the ungated Apple HWServices personality matches 7590" $'<string>AMDRadeonX6000HWServices</string>\n\t\t\t<key>IOPCIPrimaryMatch</key>\n\t\t\t<string>0x75501002 0x75511002</string>' $'<string>AMDRadeonX6000HWServices</string>\n\t\t\t<key>IOPCIPrimaryMatch</key>\n\t\t\t<string>0x75501002 0x75511002 0x75901002</string>'
plant 48 $IP "kIP_PSP is a literal again" 'kIP_PSP  = ip_of(n44id::kPsp);' 'kIP_PSP  = { 14, 0, 3 };'
plant 49 $IP "kIP_NBIO reads the wrong IP" 'kIP_NBIO = ip_of(n44id::kNbif);' 'kIP_NBIO = ip_of(n44id::kHdp);'
plant 50 $PEER "strnstr is back" 'if (cls && name_has_accel(cls, 64) && ' 'if (cls && strnstr((char *)cls, "Accel", 64) && '
plant 51 $SU "the suite does not run the test" '$T/navi44_identity_test.cpp' '$T/navi44_identity_test.cc'
echo "plants: $total run, $escaped escaped/failed"
[ "$escaped" -eq 0 ]
