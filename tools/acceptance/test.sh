#!/usr/bin/env bash
# Tests for the acceptance harness's pure logic (tools/acceptance/lib.sh). No bench, no SSH.
# Runs on the laptop's Git Bash and on the Mac (bash 3.2):   bash tools/acceptance/test.sh
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
. "$HERE/lib.sh"

fails=0 passes=0
check() {   # check <description> <expected> <actual>
    if [ "$2" = "$3" ]; then passes=$((passes + 1))
    else fails=$((fails + 1)); printf 'FAIL: %s\n  expected: [%s]\n  actual:   [%s]\n' "$1" "$2" "$3"; fi
}
check_match() {   # check_match <description> <grep -E pattern> <actual>
    if printf '%s\n' "$3" | grep -Eq -- "$2"; then passes=$((passes + 1))
    else fails=$((fails + 1)); printf 'FAIL: %s\n  pattern: [%s]\n  actual:  [%s]\n' "$1" "$2" "$3"; fi
}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/accept-test.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

# The bench's real NVRAM boot-args on 2026-10-29 (before any disarm).
BENCH='-v debug=0x100 keepsyms=1 revpatch=sbvmm nvfb=1 nvaccel=1 nvfbheads=4 -nvkmsnosmooth amfi_get_out_of_my_way=0x1 amfi=0x80 ipc_control_port_options=0 gfxrst=4 nvrmsettle=10000 -nvoff'

# --- refusal: post-ladder boot-args --------------------------------------------------------------
check "bench args carry no post-ladder arg" "" "$(post_ladder_args "$BENCH")"
check "ladder flags are not post-ladder" "" \
    "$(post_ladder_args "navi48bringup=1 navi48-stage=17 navi48-psp=1 navi48-smu=1 navi48-kcq=1 navi48-idle-clamp=1 navi48-hsa-abi=1 navi48-mes-nonfatal=1")"
check "native S1b is named" "navi48-native" "$(post_ladder_args "-v navi48bringup=1 navi48-native=1")"
check "accel gate counts even at 0" "navi48-accel-gate" "$(post_ladder_args "navi48-accel-gate=0")"
check "self-tests and G5/M6 are all named, in order" \
    "navi48-sdma-q1-test navi48-srbm-test navi48-g5 navi48-m6flip" \
    "$(post_ladder_args "navi48-sdma-q1-test=1 keepsyms=1 navi48-srbm-test=1 navi48-g5=1 navi48-m6flip=1")"
check "a flag-style post-ladder arg counts" "navi48-metal" "$(post_ladder_args "-v navi48-metal")"
check "unknown navi48 args are refused too (fail closed)" "navi48-something-new" \
    "$(post_ladder_args "navi48-something-new=1")"
check "refusal message names the arg" "REFUSING: post-ladder boot-arg(s) set in NVRAM: navi48-g5 navi48-native" \
    "$(refusal_message "navi48-g5=1 -v navi48-native=1")"
check "no refusal message when clean" "" "$(refusal_message "$BENCH")"

# --- kext path: only a non-boot staging path under a home directory ----------------------------------
check "a staged kext under the home dir is fine" "" "$(kext_path_problem /Users/pc/navi-stage/kext/Navi48Bringup.kext)"
check_match "/Library/Extensions is refused" "boot" "$(kext_path_problem /Library/Extensions/Navi48Bringup.kext)"
check_match "a home path that links into a boot path is refused" "boot" \
    "$(kext_path_problem /Users/pc/x/Library/Extensions/Navi48Bringup.kext)"
check_match "the ESP is refused" "boot path" "$(kext_path_problem /Volumes/EFI/EFI/OC/Kexts/Navi48Bringup.kext)"
check_match "a relative path is refused" "absolute" "$(kext_path_problem navi-stage/Navi48Bringup.kext)"
check_match "a non-kext is refused" "kext" "$(kext_path_problem /Users/pc/navi-stage/foo)"
check_match "a path with spaces is refused" "space" "$(kext_path_problem '/Users/pc/my stage/N.kext')"

# --- disarm / arm -----------------------------------------------------------------------------------
CLEAN='-v debug=0x100 keepsyms=1 revpatch=sbvmm amfi_get_out_of_my_way=0x1 amfi=0x80 ipc_control_port_options=0'
check "disarm strips exactly the NVIDIA args from the bench's args" "$CLEAN" "$(disarmed_args "$BENCH")"
check "disarm strips the arming arg, the stage and any other navi48 arg" "$CLEAN" \
    "$(disarmed_args "navi48bringup=1 $CLEAN navi48-stage=5 navi48-kcq=0")"
check "disarm drops an unlisted nv* arg too" "-v" "$(disarmed_args "-v nvda_drv=1")"
check "disarm is idempotent" "$CLEAN" "$(disarmed_args "$(disarmed_args "$BENCH")")"
check "disarm of nothing is nothing" "" "$(disarmed_args "")"
check "arm = disarmed args + navi48bringup=1" "$CLEAN navi48bringup=1" "$(armed_args "$BENCH")"
check "arm with a stage target" "$CLEAN navi48bringup=1 navi48-stage=17" "$(armed_args "$BENCH" 17)"
check "arm replaces an old stage target" "-v navi48bringup=1 navi48-stage=3" \
    "$(armed_args "-v navi48bringup=1 navi48-stage=9" 3)"
check "arm with extra ladder args" "-v navi48bringup=1 navi48-stage=17 navi48-kcq=0" \
    "$(armed_args "-v" 17 "navi48-kcq=0")"
check "arm refuses a post-ladder extra arg" "1" "$(armed_args "-v" "" "navi48-native=1" >/dev/null 2>&1; echo $?)"
check "arm refuses a non-numeric stage" "1" "$(armed_args "-v" "x" >/dev/null 2>&1; echo $?)"
check "is_armed sees navi48bringup=1" "yes" "$(is_armed "-v navi48bringup=1" && echo yes)"
check "is_armed: navi48bringup=0 is not armed" "no" "$(is_armed "-v navi48bringup=0" || echo no)"
check "is_armed: bare navi48bringup is armed (PE_parse_boot_argn reads 1)" "yes" "$(is_armed "navi48bringup" && echo yes)"

# --- console integrity ----------------------------------------------------------------------------------
# A 32x18 "desktop": varied gradients, like fbread's block averages of a real screen.
awk 'BEGIN { print "P3"; print "32 18"; print "255";
             for (y = 0; y < 18; y++) for (x = 0; x < 32; x++)
                 printf "%d %d %d\n", 40 + x * 6, 30 + y * 10, 60 + (x + y) * 3 }' > "$TMP/base.ppm"
# The same desktop a minute later: the clock and the cursor changed 3 cells.
awk 'NR == 4 + 5 || NR == 4 + 40 || NR == 4 + 500 { print "255 255 255"; next } { print }' "$TMP/base.ppm" > "$TMP/clock.ppm"
cc() { console_compare "$TMP/base.ppm" "$1"; }
check_match "an unchanged console passes" "^PASS " "$(cc "$TMP/base.ppm")"
check_match "a clock/cursor change passes" "^PASS " "$(cc "$TMP/clock.ppm")"
ppm_alter black "$TMP/base.ppm" > "$TMP/black.ppm"
check_match "a black console fails as black" "^FAIL black" "$(cc "$TMP/black.ppm")"
ppm_alter swap "$TMP/base.ppm" > "$TMP/swap.ppm"
check_match "red/blue swapped fails as colour" "^FAIL colour" "$(cc "$TMP/swap.ppm")"
ppm_alter tint "$TMP/base.ppm" > "$TMP/tint.ppm"
check_match "a green tint fails as colour" "^FAIL colour" "$(cc "$TMP/tint.ppm")"
ppm_alter garbage "$TMP/base.ppm" > "$TMP/garbage.ppm"
check_match "a garbage band fails as corrupted" "^FAIL corrupted" "$(cc "$TMP/garbage.ppm")"
awk 'NR <= 3 { print; next } { print "128 128 128" }' "$TMP/base.ppm" > "$TMP/flat.ppm"
check_match "a flat grey console fails as blank" "^FAIL blank" "$(cc "$TMP/flat.ppm")"
check_match "a black baseline is not a measurement" "^FAIL baseline" \
    "$(console_compare "$TMP/black.ppm" "$TMP/base.ppm")"
printf 'P3\n16 9\n255\n' > "$TMP/small.ppm"
check_match "different sizes fail" "^FAIL size" "$(cc "$TMP/small.ppm")"
check_match "a missing capture fails" "^FAIL missing" "$(cc "$TMP/nope.ppm")"
check "console_compare exits 1 on FAIL" "1" "$(cc "$TMP/black.ppm" >/dev/null; echo $?)"
check "console_compare exits 0 on PASS" "0" "$(cc "$TMP/clock.ppm" >/dev/null; echo $?)"

# --- criteria over a cycle directory ----------------------------------------------------------------------
C=$TMP/cycle; mkdir -p "$C"
cat > "$C/ioreg.txt" <<'IOREG'
+-o Navi48Bringup  <class Navi48Bringup, id 0x100000abc, registered, matched, active, busy 0 (0 ms), retain 7>
    {
      "Navi48,StageReached" = 17
      "Navi48,StageReachedName" = "ComputeDispatch"
      "SMU,FirmwareVersion" = "102.70.0"
      "IOClass" = "Navi48Bringup"
    }
IOREG
check "ioreg_value reads a number" "17" "$(ioreg_value "$C/ioreg.txt" "Navi48,StageReached")"
check "ioreg_value strips quotes" "ComputeDispatch" "$(ioreg_value "$C/ioreg.txt" "Navi48,StageReachedName")"
check "ioreg_value of an absent key is empty" "" "$(ioreg_value "$C/ioreg.txt" "Navi48,Nope")"
check_match "expect KEY=VALUE passes on a match" "^PASS" "$(evaluate_criterion "ioreg Navi48,StageReached=17" "$C")"
check_match "expect KEY=VALUE fails with what it saw" "^FAIL .*16.*17|^FAIL .*17.*16" \
    "$(sed 's/= 17/= 16/' "$C/ioreg.txt" > "$C/i2" && mv "$C/i2" "$C/ioreg.txt"; evaluate_criterion "ioreg Navi48,StageReached=17" "$C")"
check_match "expect KEY=VALUE fails on an absent key" "^FAIL .*absent" "$(evaluate_criterion "ioreg Navi48,Nope=1" "$C")"
check_match "expect KEY~REGEX" "^PASS" "$(evaluate_criterion "ioreg SMU,FirmwareVersion~^102\.70\." "$C")"
check_match "no-attach fails when the kext attached" "^FAIL" "$(evaluate_criterion no-attach "$C")"
: > "$C/ioreg.txt"
check_match "no-attach passes with no instance" "^PASS" "$(evaluate_criterion no-attach "$C")"

printf '%s\n' '22:10:01 probe: 0x75901002 not Navi 48, ignoring' > "$C/log.txt"
check_match "expect-log passes on a matching line" "^PASS" "$(evaluate_criterion "log probe: 0x[0-9a-f]+ not Navi 48" "$C")"
check_match "expect-log fails without one" "^FAIL" "$(evaluate_criterion "log stages: ladder" "$C")"

printf '%s\n' "-v keepsyms=1 navi48bringup=1 navi48-stage=17" > "$C/armed-bootargs.txt"
check_match "armed-boot passes when the running boot is armed" "^PASS" "$(evaluate_criterion armed-boot "$C")"
printf '%s\n' "-v keepsyms=1" > "$C/armed-bootargs.txt"
check_match "armed-boot fails when OpenCore dropped the arg" "^FAIL" "$(evaluate_criterion armed-boot "$C")"
printf '%s\n' "$CLEAN" > "$C/nvram-after-disarm.txt"
check_match "disarmed passes on clean NVRAM" "^PASS" "$(evaluate_criterion disarmed "$C")"
printf '%s\n' "$CLEAN gfxrst=4" > "$C/nvram-after-disarm.txt"
check_match "disarmed names an NVIDIA leftover" "^FAIL .*gfxrst" "$(evaluate_criterion disarmed "$C")"
printf '%s\n' "$CLEAN navi48bringup=1" > "$C/nvram-after-disarm.txt"
check_match "disarmed names a leftover arming arg" "^FAIL .*navi48bringup" "$(evaluate_criterion disarmed "$C")"

printf 'aux 3b6c\nboot-paths\n' > "$C/safety-before.txt"; cp "$C/safety-before.txt" "$C/safety-after.txt"
check_match "never-at-boot passes when nothing changed" "^PASS" "$(evaluate_criterion never-at-boot "$C")"
printf 'aux 9999\nboot-paths\n' > "$C/safety-after.txt"
check_match "never-at-boot fails when the AuxKC changed" "^FAIL .*AuxKC" "$(evaluate_criterion never-at-boot "$C")"
printf 'aux 3b6c\nboot-paths /Library/Extensions/Navi48Bringup.kext\n' > "$C/safety-after.txt"
check_match "never-at-boot fails on a kext in a boot path" "^FAIL .*Library/Extensions" "$(evaluate_criterion never-at-boot "$C")"

echo 30 > "$C/ssh-return.txt"
check_match "ssh-return passes with the seconds" "^PASS .*30 s" "$(evaluate_criterion ssh-return "$C")"
echo timeout > "$C/ssh-return.txt"
check_match "ssh-return fails on a timeout" "^FAIL" "$(evaluate_criterion ssh-return "$C")"
printf '1761771000\n1761771000\n' > "$C/alive.txt"
check_match "ssh-alive passes on the same boot" "^PASS" "$(evaluate_criterion ssh-alive "$C")"
printf '1761771000\n1761779999\n' > "$C/alive.txt"
check_match "ssh-alive fails after a reboot (panic)" "^FAIL .*reboot" "$(evaluate_criterion ssh-alive "$C")"
printf '1761771000\n' > "$C/alive.txt"
check_match "ssh-alive fails when SSH did not answer" "^FAIL" "$(evaluate_criterion ssh-alive "$C")"
echo 0 > "$C/kmutil-load.rc"; echo "com.navi48.bringup (0.0.664)" > "$C/loaded.txt"
check_match "kext-loaded passes" "^PASS" "$(evaluate_criterion kext-loaded "$C")"
echo 30 > "$C/kmutil-load.rc"; : > "$C/loaded.txt"; printf 'Validating extension failed: Kext x at /p:\n\nFilesystem error: Invalid ownership (501:20)\n' > "$C/kmutil-load.txt"
check_match "kext-loaded fails with kmutil's reason" "^FAIL .*rc 30.*Invalid ownership" "$(evaluate_criterion kext-loaded "$C")"
cp "$TMP/base.ppm" "$C/console-before.ppm"; cp "$TMP/swap.ppm" "$C/console-after.ppm"
check_match "console criterion uses console_compare" "^FAIL colour" "$(evaluate_criterion console "$C")"
check_match "an unknown criterion fails loudly" "^FAIL unknown criterion" "$(evaluate_criterion bogus "$C")"

# Plugging in a criterion: define crit_<name> and register it.
crit_demo_thing() { echo "PASS demo saw $(cat "$1/ssh-return.txt")"; }
register_criterion demo-thing
check_match "a registered criterion is listed" "demo-thing" "$(printf '%s\n' "$CRITERIA" | tail -1)"
check_match "and evaluated" "^PASS demo saw timeout" "$(evaluate_criterion demo-thing "$C")"

# evaluate_cycle writes results.tsv: name<TAB>PASS|FAIL<TAB>detail, one line per criterion, in order.
CRITERIA="ssh-return
demo-thing"
evaluate_cycle "$C"
check "evaluate_cycle writes one row per criterion" \
    "$(printf 'ssh-return\tFAIL\tSSH did not return (timeout)\ndemo-thing\tPASS\tdemo saw timeout')" "$(cat "$C/results.tsv")"

# --- result table -----------------------------------------------------------------------------------
R=$TMP/run; mkdir -p "$R/cycle-1" "$R/cycle-2"
printf 'ssh-return\tPASS\t31 s\nconsole\tPASS\tluma\n' > "$R/cycle-1/results.tsv"
printf 'ssh-return\tPASS\t33 s\nconsole\tFAIL\tblack luma=0\n' > "$R/cycle-2/results.tsv"
T=$(result_table "$R" 2)
check_match "table has a column per cycle" "^criterion +cycle 1 +cycle 2" "$T"
check_match "table row for ssh-return" "^ssh-return +PASS +PASS" "$T"
check_match "table row for console" "^console +PASS +FAIL" "$T"
check_match "table lists failure details" "cycle 2 console: black luma=0" "$T"
check_match "overall FAIL names the criterion" "^OVERALL: FAIL .*console" "$T"
check "result_table exits 1 on FAIL" "1" "$(result_table "$R" 2 >/dev/null; echo $?)"
printf 'ssh-return\tPASS\t33 s\nconsole\tPASS\tluma\n' > "$R/cycle-2/results.tsv"
check_match "overall PASS when every cycle passed" "^OVERALL: PASS \(2/2 cycles" "$(result_table "$R" 2)"
check "result_table exits 0 on PASS" "0" "$(result_table "$R" 2 >/dev/null; echo $?)"
T=$(result_table "$R" 3)
check_match "a cycle that never ran shows as a dash" "^ssh-return +PASS +PASS +-" "$T"
check_match "and the run fails" "^OVERALL: FAIL .*2/3 cycles" "$T"

printf '%d passed, %d failed\n' "$passes" "$fails"
[ "$fails" -eq 0 ]
