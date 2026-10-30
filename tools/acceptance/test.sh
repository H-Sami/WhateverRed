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

# --- disarm / cycle boot-args / the arming token -----------------------------------------------------------------------------------
CLEAN='-v debug=0x100 keepsyms=1 revpatch=sbvmm amfi_get_out_of_my_way=0x1 amfi=0x80 ipc_control_port_options=0'
check "disarm strips exactly the NVIDIA args from the bench's args" "$CLEAN" "$(disarmed_args "$BENCH")"
check "disarm strips the arming arg, the stage and any other navi48 arg" "$CLEAN" \
    "$(disarmed_args "navi48bringup=1 $CLEAN navi48-stage=5 navi48-kcq=0")"
check "disarm drops an unlisted nv* arg too" "-v" "$(disarmed_args "-v nvda_drv=1")"
check "disarm is idempotent" "$CLEAN" "$(disarmed_args "$(disarmed_args "$BENCH")")"
check "disarm of nothing is nothing" "" "$(disarmed_args "")"
# The cycle boots UNARMED: the boot-args are the disarmed args plus any extra ladder args (read by start()
# once armed), never navi48bringup / navi48-stage (ignored by the kext; an old AuxKC build would arm on them).
check "cycle args = disarmed args" "$CLEAN" "$(cycle_args "$BENCH")"
check "cycle args drop an old arming arg and stage" "-v" "$(cycle_args "-v navi48bringup=1 navi48-stage=9")"
check "cycle args add extra ladder args" "-v navi48-kcq=0 navi48-psp=1" "$(cycle_args "-v" "navi48-kcq=0" "navi48-psp=1")"
check "cycle args refuse a post-ladder extra" "1" "$(cycle_args "-v" "navi48-native=1" >/dev/null 2>&1; echo $?)"
check "cycle args refuse navi48bringup as an extra (the token arms)" "1" "$(cycle_args "-v" "navi48bringup=1" >/dev/null 2>&1; echo $?)"
check "cycle args refuse navi48-stage as an extra (the token names the stage)" "1" "$(cycle_args "-v" "navi48-stage=3" >/dev/null 2>&1; echo $?)"
check "is_armed sees navi48bringup=1" "yes" "$(is_armed "-v navi48bringup=1" && echo yes)"
check "is_armed: navi48bringup=0 is not armed" "no" "$(is_armed "-v navi48bringup=0" || echo no)"
check "is_armed: bare navi48bringup is armed (PE_parse_boot_argn reads 1)" "yes" "$(is_armed "navi48bringup" && echo yes)"
# arm_token mirrors the kext's grammar (amd/arm_token_pure.h): stage:N, N 1..17, no leading zero.
check "the token for stage 1" "stage:1" "$(arm_token 1)"
check "the token for stage 17 (ComputeDispatch)" "stage:17" "$(arm_token 17)"
for bad in 0 18 99 01 "" x 1x -1; do
    check "arm_token refuses '$bad'" "1" "$(arm_token "$bad" >/dev/null 2>&1; echo $?)"
done
check "the token lives in navi48-arm" "navi48-arm" "$ARM_KEY"
check "the harness arms only after 120 s of uptime (the kext's floor is 90 s)" "120" "$ARM_MIN_UPTIME"
check "the token write" "sudo -n nvram navi48-arm=stage:9" "$(arm_set_cmd stage:9)"
check "the token delete" "sudo -n nvram -d navi48-arm" "$ARM_DELETE_CMD"
check_match "the token read lists navi48-arm and marks a successful read" 'nvram -p.*\^navi48-arm.*nvram-read-ok' "$ARM_READ_CMD"
# token_state <file>: what a token read (ARM_READ_CMD's output) found.
printf 'nvram-read-ok\n' > "$TMP/tok"
check "no token line + read ok = absent" "absent" "$(token_state "$TMP/tok")"
printf 'navi48-arm\tstage:1\nnvram-read-ok\n' > "$TMP/tok"
check "a token line = present + value" "present stage:1" "$(token_state "$TMP/tok")"
printf 'navi48-arm\tstage:1\n' > "$TMP/tok"
check "no read-ok marker = unread (SSH or nvram failed)" "unread" "$(token_state "$TMP/tok")"
check "a missing file = unread" "unread" "$(token_state "$TMP/nope")"
printf 'navi48-armx\tstage:1\nnvram-read-ok\n' > "$TMP/tok"
check "another variable is not the token" "absent" "$(token_state "$TMP/tok")"
check "token_state reads a pipe (accept.sh: token_read | token_state /dev/stdin)" "present stage:3" \
    "$(printf 'navi48-arm\tstage:3\nnvram-read-ok\n' | token_state /dev/stdin)"
check "an empty pipe is unread" "unread" "$(printf '' | token_state /dev/stdin)"
# the re-match: drop the personalities (terminate + remove from the IOCatalogue), then load/re-send them
check "rematch step 1 removes the personalities" "sudo -n kmutil unload -b com.navi48.bringup -P" "$REMATCH_UNLOAD_CMD"
check "rematch step 2 loads the AuxKC kext by id and re-sends its personality" \
    "sudo -n kmutil load -b com.navi48.bringup -P Navi48Bringup" "$REMATCH_LOAD_CMD"

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
cp "$C/log.txt" "$TMP/log.keep"; printf 'log-read-ok 29551BDC-AA5E-48C4-AF8F-39D94BC4041C\n' >> "$C/log.txt"
check_match "expect-log never matches kernel_log's read marker" "^FAIL" "$(evaluate_criterion "log read-ok" "$C")"
cp "$TMP/log.keep" "$C/log.txt"

# the navi44-stage1 preset against a capture of what stage 1 publishes on the 9060 XT
cat > "$C/ioreg.txt" <<'IOREG'
+-o Navi48Bringup  <class Navi48Bringup, id 0x100000abc, registered, matched, active, busy 0 (0 ms), retain 7>
    {
      "IOPCIPrimaryMatch" = "0x75901002"
      "Navi48,StageTarget" = 1
      "Navi48,StageReached" = 1
      "Navi48,StageReachedName" = "IPDiscovery"
      "Navi48,GeometrySE" = 2
      "Navi48,GeometrySAPerSE" = 2
      "Navi48,GeometryCUPerSA" = 8
      "Navi48,GeometryRB" = 8
      "Navi48,Geometry" = "2/2/8/8"
      "Navi48,GeometryCheck" = "accepted"
    }
IOREG
printf '%s\n' '22:10:01 probe: Navi 44 RX 9060 XT (0x7590) - bring-up enabled' > "$C/log.txt"
check "navi44-stage1 preset has 11 criteria" "11" "$(expect_preset navi44-stage1 | wc -l | tr -d ' ')"
check "an unknown preset is refused" "1" "$(expect_preset nope >/dev/null; echo $?)"
pf=0; while IFS= read -r c; do case "$(evaluate_criterion "$c" "$C")" in PASS*) ;; *) pf=$((pf + 1)) ;; esac; done <<< "$(expect_preset navi44-stage1)"
check "navi44-stage1 passes on a good stage-1 capture" "0" "$pf"
sed 's/"Navi48,GeometryRB" = 8/"Navi48,GeometryRB" = 7/' "$C/ioreg.txt" > "$C/i2" && mv "$C/i2" "$C/ioreg.txt"
printf '%s\n' '22:10:01 probe: 0x75501002 not Navi 44, ignoring' > "$C/log.txt"
pf=0; while IFS= read -r c; do case "$(evaluate_criterion "$c" "$C")" in PASS*) ;; *) pf=$((pf + 1)) ;; esac; done <<< "$(expect_preset navi44-stage1)"
check "navi44-stage1 fails on a wrong RB count and no probe line" "2" "$pf"

printf '%s\n' "$CLEAN" > "$C/nvram-after-disarm.txt"
check_match "disarmed passes on clean NVRAM" "^PASS" "$(evaluate_criterion disarmed "$C")"
printf '%s\n' "$CLEAN gfxrst=4" > "$C/nvram-after-disarm.txt"
check_match "disarmed names an NVIDIA leftover" "^FAIL .*gfxrst" "$(evaluate_criterion disarmed "$C")"
printf '%s\n' "$CLEAN navi48bringup=1" > "$C/nvram-after-disarm.txt"
check_match "disarmed names a leftover arming arg" "^FAIL .*navi48bringup" "$(evaluate_criterion disarmed "$C")"

# inert-at-boot: before arming, the boot-time match left the kext inert and nothing was installed for boot.
LOG_OK='log-read-ok 29551BDC-AA5E-48C4-AF8F-39D94BC4041C'
INERT='22:09:12 probe: not armed — inert (no navi48-arm token in NVRAM; arm after login with nvram navi48-arm=stage:N)'
inert_fixture() {
    printf '%s\n' "-v keepsyms=1" > "$C/boot-bootargs.txt"
    printf 'nvram-read-ok\n' > "$C/token-before.txt"
    printf '+-o Root  <class IORegistryEntry, id 0x100000100, retain 20>\nioreg-read-ok\n' > "$C/ioreg-before.txt"
    printf '%s\n%s\n' "$INERT" "$LOG_OK" > "$C/log-before.txt"
    printf 'aux 8d5f\nboot-paths\nauxkc com.navi48.bringup\n' > "$C/safety-boot.txt"
    cp "$C/safety-boot.txt" "$C/safety-before.txt"; cp "$C/safety-boot.txt" "$C/safety-after.txt"
}
inert_fixture
check_match "inert-at-boot passes on an inert boot, naming the AuxKC member" "^PASS .*inert.*8d5f.*com.navi48.bringup" "$(evaluate_criterion inert-at-boot "$C")"
printf 'navi48-arm\tstage:1\nnvram-read-ok\n' > "$C/token-before.txt"
check_match "inert-at-boot fails when a token was in NVRAM before arming" "^FAIL .*token.*stage:1" "$(evaluate_criterion inert-at-boot "$C")"
inert_fixture; printf 'nothing\n' > "$C/token-before.txt"
check_match "inert-at-boot fails when NVRAM was not read" "^FAIL .*NVRAM.*not read" "$(evaluate_criterion inert-at-boot "$C")"
inert_fixture; printf '%s\n' "-v navi48bringup=1" > "$C/boot-bootargs.txt"
check_match "inert-at-boot fails on a boot carrying navi48bringup (an old AuxKC build arms on it)" "^FAIL .*navi48bringup" "$(evaluate_criterion inert-at-boot "$C")"
inert_fixture; rm "$C/boot-bootargs.txt"
check_match "inert-at-boot fails when the boot's args were not read" "^FAIL .*kern.bootargs" "$(evaluate_criterion inert-at-boot "$C")"
inert_fixture; printf '%s\n' "$LOG_OK" > "$C/log-before.txt"
check_match "inert-at-boot passes when the AuxKC kext was not matched at boot (no inert line on macOS 26.7.2)" \
    "^PASS .*not matched at boot" "$(evaluate_criterion inert-at-boot "$C")"
inert_fixture
check_match "inert-at-boot records the inert line as evidence when there is one" "^PASS .*inert line logged" "$(evaluate_criterion inert-at-boot "$C")"
inert_fixture; printf '%s\n' "$INERT" > "$C/log-before.txt"
check_match "inert-at-boot fails when this boot's kernel log was not read" "^FAIL .*kernel log not read" "$(evaluate_criterion inert-at-boot "$C")"
inert_fixture; : > "$C/log-before.txt"
check_match "inert-at-boot fails on an empty log-before (the read never finished)" "^FAIL .*kernel log not read" "$(evaluate_criterion inert-at-boot "$C")"
inert_fixture; printf '      "Navi48,ProvisionedThisBoot" = Yes\nioreg-read-ok\n' > "$C/ioreg-before.txt"
check_match "inert-at-boot fails on any Navi48,* property before arming" "^FAIL .*Navi48,ProvisionedThisBoot" "$(evaluate_criterion inert-at-boot "$C")"
inert_fixture; printf '%s\n%s\n' '22:09:13 probe: Navi 44 RX 9060 XT (0x7590) — bring-up enabled' "$LOG_OK" > "$C/log-before.txt"
check_match "inert-at-boot fails when probe ran a ladder before arming, even without an inert line" "^FAIL .*bring-up enabled" "$(evaluate_criterion inert-at-boot "$C")"
inert_fixture; printf '%s\n%s\n%s\n' "$INERT" '22:09:12 probe: navi48-arm token consumed (deleted from NVRAM) — armed for this match, stage target 1' "$LOG_OK" > "$C/log-before.txt"
check_match "inert-at-boot fails when a token was consumed before arming" "^FAIL .*consumed before" "$(evaluate_criterion inert-at-boot "$C")"
inert_fixture; printf '%s\n%s\n%s\n' "$INERT" '22:09:13 probe: Navi 44 RX 9060 XT (0x7590) — bring-up enabled' "$LOG_OK" > "$C/log-before.txt"
check_match "inert-at-boot fails when probe enabled the bring-up before arming" "^FAIL .*bring-up enabled" "$(evaluate_criterion inert-at-boot "$C")"
inert_fixture; printf '      "Navi48,StageReached" = 1\nioreg-read-ok\n' > "$C/ioreg-before.txt"
check_match "inert-at-boot fails on a Navi48,Stage property before arming" "^FAIL .*Navi48,Stage" "$(evaluate_criterion inert-at-boot "$C")"
inert_fixture; printf '+-o Navi48Bringup  <class Navi48Bringup, id 0x1>\nioreg-read-ok\n' > "$C/ioreg-before.txt"
check_match "inert-at-boot fails on an attached instance before arming" "^FAIL .*instance" "$(evaluate_criterion inert-at-boot "$C")"
inert_fixture; printf '+-o Root\n' > "$C/ioreg-before.txt"
check_match "inert-at-boot fails when the IORegistry was not read" "^FAIL .*IORegistry.*not read" "$(evaluate_criterion inert-at-boot "$C")"
inert_fixture; printf 'aux 9999\nboot-paths\n' > "$C/safety-after.txt"
check_match "inert-at-boot fails when the AuxKC changed during the cycle" "^FAIL .*AuxKC changed" "$(evaluate_criterion inert-at-boot "$C")"
inert_fixture; printf 'aux 8d5f\nboot-paths /Library/Extensions/Navi48Bringup.kext\n' > "$C/safety-after.txt"
check_match "inert-at-boot fails on a kext in a boot path" "^FAIL .*Library/Extensions" "$(evaluate_criterion inert-at-boot "$C")"
inert_fixture; rm "$C/safety-boot.txt"
check_match "inert-at-boot fails when a safety snapshot is missing" "^FAIL .*snapshot" "$(evaluate_criterion inert-at-boot "$C")"
inert_fixture

# token-consumed: after the re-match the token is gone from NVRAM and the kext says it deleted it.
printf 'nvram-read-ok\n' > "$C/token-after.txt"
printf '%s\n' '22:12:40 probe: navi48-arm token consumed (deleted from NVRAM) — armed for this match, stage target 1' > "$C/log.txt"
check_match "token-consumed passes when it is gone and the kext logged the consume" "^PASS .*consumed" "$(evaluate_criterion token-consumed "$C")"
printf 'navi48-arm\tstage:1\nnvram-read-ok\n' > "$C/token-after.txt"
check_match "token-consumed fails while the token is still in NVRAM (probe never ran)" "^FAIL .*still in NVRAM.*stage:1" "$(evaluate_criterion token-consumed "$C")"
printf 'nvram-read-' > "$C/token-after.txt"
check_match "token-consumed fails when NVRAM was not read" "^FAIL .*not read" "$(evaluate_criterion token-consumed "$C")"
printf 'nvram-read-ok\n' > "$C/token-after.txt"; : > "$C/log.txt"
check_match "token-consumed fails when the token vanished without the kext's consume line" "^FAIL .*no .token consumed. line" "$(evaluate_criterion token-consumed "$C")"
check "the default criteria, in order" "ssh-return disarmed inert-at-boot kext-loaded token-consumed ssh-alive console" "$(printf '%s\n' "$CRITERIA" | tr '\n' ' ' | sed 's/ $//')"
check_match "armed-boot is gone (the boot is unarmed now)" "^FAIL unknown criterion" "$(evaluate_criterion armed-boot "$C")"
check_match "never-at-boot is gone (inert-at-boot replaces it)" "^FAIL unknown criterion" "$(evaluate_criterion never-at-boot "$C")"

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
echo 31 > "$C/kmutil-load.rc"
printf "Error occurred while building a collection: \n    1: One or more binaries has an error which prevented linking.  See other errors.\n    2: Could not use 'x' because: Failed to bind '_strnstr' in 'x'\n" > "$C/kmutil-load.txt"
check_match "kext-loaded names the link error, not the generic header" "^FAIL .*rc 31: .*Failed to bind '_strnstr'" "$(evaluate_criterion kext-loaded "$C")"
echo 30 > "$C/kmutil-load.rc"; printf 'Validating extension failed: Kext x at /p:\n\nFilesystem error: Invalid ownership (501:20)\n' > "$C/kmutil-load.txt"
printf 'sudo: a password is required\n' > "$C/kext-chown.txt"; echo 1 > "$C/kext-chown.rc"
check_match "kext-loaded names a refused chown before kmutil's reason" \
    "^FAIL sudo -n refused chown -R 0:0 .*whateverred-chown.*password is required" "$(evaluate_criterion kext-loaded "$C")"
: > "$C/kext-chown.txt"; echo 0 > "$C/kext-chown.rc"
check_match "a chown that worked leaves kmutil's reason" "^FAIL .*rc 30" "$(evaluate_criterion kext-loaded "$C")"
rm -f "$C/kext-chown.txt" "$C/kext-chown.rc"

# --- kext ownership: chown commands (sudoers allows exactly these two) ------------------------------
K=/Users/pc/navi-stage/kext/Navi48Bringup.kext
check "chown to root for the load" "sudo -n /usr/sbin/chown -R 0:0 $K" "$(kext_chown_cmd root "$K")"
check "chown back to pc for restaging" "sudo -n /usr/sbin/chown -R 501:20 $K" "$(kext_chown_cmd pc "$K")"
check "no other owner" "" "$(kext_chown_cmd 0:80 "$K")"
kext_chown_cmd 0:80 "$K" >/dev/null; check "other owner is an error" 1 "$?"
check "no other path (sudoers would refuse it)" "" "$(kext_chown_cmd root /Users/pc/other/Navi48Bringup.kext)"
R=$(kext_restage_cmd /Users/pc/src/WhateverRed/src/navi48-bringup/build/Navi48Bringup.kext "$K")
check_match "restage hands the old bundle back to pc first" "^if test -e $K; then sudo -n /usr/sbin/chown -R 501:20 $K" "$R"
check_match "then replaces it with a copy of the build" \
    "rm -rf $K .*cp -R /Users/pc/src/WhateverRed/src/navi48-bringup/build/Navi48Bringup.kext $K" "$R"
check_match "and stops if the chown is refused" "chown -R 501:20 $K \|\| exit" "$R"
check "restage refuses a source with spaces" "" "$(kext_restage_cmd '/Users/pc/my build/N.kext' "$K")"

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

# --- boot_log_lines: this boot's kernel log, by boot UUID (kern.bootsessionuuid), from `log show --style ndjson` ---
# Shapes from the bench's `log show --style ndjson` on 2026-10-30 (keys trimmed): the run's log.txt got the
# previous boot's (01A2D260) inert line through the old --start <kern.boottime> filter.
CUR=29551BDC-AA5E-48C4-AF8F-39D94BC4041C PREV=01A2D260-37ED-48D8-872B-A5F1B953D9C5
{
    echo "$CUR"
    echo '{"messageType":"Default","bootUUID":"'"$PREV"'","timestamp":"2026-10-30 03:08:41.1+0300","eventMessage":"Navi48Bringup: probe: not armed — inert (no navi48-arm token in NVRAM; arm after login with nvram navi48-arm=stage:N)","processID":0}'
    echo '{"messageType":"Default","bootUUID":"'"$CUR"'","eventMessage":"installation check: bundle for com.navi48.bringup is at \/Users\/pc\/navi-stage\/kext\/Navi48Bringup.kext","processID":124}'
    echo '{"messageType":"Default","bootUUID":"'"$CUR"'","timestamp":"2026-10-30 03:12:31.2+0300","eventMessage":"Navi48Bringup: probe: navi48-arm token consumed (deleted from NVRAM) — armed for this match, stage target 1","processID":0}'
    echo '{"bootUUID":"'"$CUR"'","eventMessage":"Navi48Bringup: dcn: path \/a\/b says \"hi\" and a\\b","processID":0}'
    echo '{"bootUUID":"'"$CUR"'","processID":0}'
} > "$TMP/ndjson.txt"
check "boot_log_lines keeps only this boot's Navi48Bringup lines, prefix stripped and JSON-unescaped, then the read marker" \
    "probe: navi48-arm token consumed (deleted from NVRAM) — armed for this match, stage target 1
dcn: path /a/b says \"hi\" and a\\b
log-read-ok $CUR" "$(boot_log_lines < "$TMP/ndjson.txt")"
check "boot_log_lines writes nothing (no read marker) when the boot UUID was not read" "" \
    "$(printf '\n{"bootUUID":"","eventMessage":"Navi48Bringup: probe: x"}\n' | boot_log_lines)"
printf '%s\n' "$CUR" | boot_log_lines > /dev/null; check "boot_log_lines succeeds on a boot with no lines" 0 $?
printf '\n' | boot_log_lines > /dev/null; check "boot_log_lines fails without a boot UUID" 1 $?

printf '%d passed, %d failed\n' "$passes" "$fails"
[ "$fails" -eq 0 ]
