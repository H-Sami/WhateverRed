#!/bin/zsh
# arm_token_plant.sh - planted breaks for tests/arm_token_test.cpp (ticket #16, spec #14: the consume-once NVRAM arming token).
# For each plant: copy the real sources into a scratch tree, apply the break (exact-once replacements; the script first proves each replaced text occurs exactly once),
# compile the real test against the scratch tree and demand that it FAILS (a compile error, a failing check, a crash or a hang cut off by the alarm all count).
# A plant the suite lets through is a hole: the script exits non-zero and says which. The CONTROL (no break) must pass.
#   run from anywhere:  src/navi48-bringup/tests/arm_token_plant.sh [first-plant-id last-plant-id]
set -u
ROOT="$(cd "$(dirname "$0")/../../.." && pwd)"
K=src/navi48-bringup
SR=$K/src
SCR="${TMPDIR:-/tmp}/armtok-plant.$$"
trap 'rm -rf "$SCR"' EXIT
escaped=0; total=0
FIRST=${1:-0}; LAST=${2:-9999}

fresh() {
  rm -rf "$SCR"; mkdir -p "$SCR/$K/tests" "$SCR/tools/conductor"
  rsync -a --exclude fw "$ROOT/$SR/" "$SCR/$SR/"          # the generated firmware arrays are not read by any pin
  cp "$ROOT/$K/Info.plist" "$SCR/$K/"
  cp "$ROOT/$K/tests/arm_token_test.cpp" "$SCR/$K/tests/"
  cp "$ROOT/tools/conductor/suites.sh" "$SCR/tools/conductor/"
}
build_run() {   # prints the verdict line; returns 0 if the suite PASSED, 1 if it failed, 2 if it did not compile
  local out
  out=$(cd "$SCR" && clang++ -std=c++17 -Wall -Wextra -O0 -I $SR -I $SR/amd $K/tests/arm_token_test.cpp -o "$SCR/t" 2>&1)
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
  if [ $brc -eq 0 ]; then echo "CONTROL arm_token_test (no break): $msg"; else echo "CONTROL arm_token_test (no break): *** FAILED: $msg ***"; escaped=$((escaped+1)); fi
}
P=$SR/amd/arm_token_pure.h
B=$SR/Navi48Bringup.cpp
SU=tools/conductor/suites.sh

control
# --- the pure decision
plant 1  $P "an undeletable token still arms" '    if (!consumed) return { false, 0, kNotConsumed };
' ''
plant 2  $P "absent is not checked" '    if (!present) return { false, 0, kAbsent };
' ''
plant 3  $P "the top stage is 18" 'kStageMax = 17;' 'kStageMax = 18;'
plant 4  $P "stage 0 arms" 'kStageMin = 1;' 'kStageMin = 0;'
plant 5  $P "a leading zero is accepted" "    if (len == p + 2 && value[p] == '0') return no;  // no leading zero
" ''
plant 6  $P "any number of trailing NULs" 'if (value[len - 1] == 0) len--;' 'while (len && value[len - 1] == 0) len--;'
plant 7  $P "the prefix is not checked" 'if (value[i] != static_cast<uint8_t>(kPrefix[i])) return no;' 'if (false) return no;'
plant 8  $P "a non-digit is read as a digit" "if (value[i] < '0' || value[i] > '9') return no;" "if (value[i] < '0') return no;"
plant 9  $P "three digits are accepted" 'if (len < p + 1 || len > p + 2) return no;' 'if (len < p + 1 || len > p + 3) return no;'
plant 10 $P "a refused stage still carries the number" 'return { false, 0, kStageOutOfRange };' 'return { false, n, kStageOutOfRange };'
plant 11 $P "the NVRAM key is the boot-arg's name" 'kNvramKey = "navi48-arm";' 'kNvramKey = "navi48bringup";'
plant 12 $P "the out-of-range reason drops the range" 'return "stage out of range (want 1..17)";' 'return "stage out of range";'
# --- the kext consumes it
plant 13 $B "probe runs super::probe before the consume" '	const n48arm::Decision arm = consumeArmToken();   // FIRST: nothing else runs on an unarmed match
	if (!arm.armed) return nullptr;
	targetStage = arm.stage;
	if (!super::probe(provider, score)) return nullptr;' '	if (!super::probe(provider, score)) return nullptr;
	const n48arm::Decision arm = consumeArmToken();   // FIRST: nothing else runs on an unarmed match
	if (!arm.armed) return nullptr;
	targetStage = arm.stage;'
plant 14 $B "the token is not deleted" '			if (const OSSymbol *key = OSSymbol::withCString(n48arm::kNvramKey)) { opts->removeProperty(key); key->release(); }
' ''
plant 15 $B "consumed without re-reading" '			OSObject *again = opts->copyProperty(n48arm::kNvramKey);
			consumed = (again == nullptr);
			if (again) again->release();' '			consumed = true;'
plant 16 $B "the boot-arg arms again" '	if (!arm.armed) return nullptr;' '	uint32_t on = 0;
	if (!arm.armed && (!PE_parse_boot_argn("navi48bringup", &on, sizeof(on)) || on == 0)) return nullptr;'
plant 17 $B "the stage comes from navi48-stage again" '		// Ticket #16: the stage target came from the navi48-arm token in probe (targetStage), not a boot-arg.
' '		if (!PE_parse_boot_argn("navi48-stage", &targetStage, sizeof(targetStage))) targetStage = 0;
'
plant 18 $B "the stage target is not taken from the token" '	targetStage = arm.stage;
' ''
plant 19 $B "the inert line is not logged" 'N48LOG("probe: not armed — inert (%s;' 'IOLog("(%s;'
plant 20 $B "decide is told the token was consumed when it was not" 'n48arm::decide(present, buf, len, consumed, uptimeS)' 'n48arm::decide(present, buf, len, present, uptimeS)'
plant 21 $B "/options is not checked to be the NVRAM" 'if (opts && opts->metaCast("IODTNVRAM")) {' 'if (opts) {'
plant 22 $B "the kext does not tie kStageMax to the ladder" 'static_assert(n48arm::kStageMax == static_cast<uint32_t>(amdgpu::BringupStage::Max), "the arming token'"'"'s top stage is the ladder'"'"'s last");' ''
plant 24 $P "no uptime floor" '    if (uptime_s < kMinUptimeS) return { false, 0, kTooEarly };
' ''
plant 25 $P "the floor is 30 s" 'kMinUptimeS = 90;' 'kMinUptimeS = 30;'
plant 26 $B "the uptime is not passed" 'n48arm::decide(present, buf, len, consumed, uptimeS)' 'n48arm::decide(present, buf, len, consumed, 1000)'
plant 23 $SU "the suite does not run the test" '$T/arm_token_test.cpp' '$T/arm_token_test.cc'
echo "plants: $total run, $escaped escaped/failed"
[ "$escaped" -eq 0 ]
