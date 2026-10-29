#!/usr/bin/env bash
# accept.sh: the bring-up acceptance harness. RUN ON THE LAPTOP; it drives the test bench over SSH.
#
#   tools/acceptance/accept.sh [options]
#     --cycles N            cold-boot cycles (default 3); the run passes only if every cycle passes
#     --stage N             the ladder target the token arms (navi48-arm=stage:N, N 1..17; default 1)
#     --arg NAME=VALUE      an extra LADDER boot-arg for the cycle's boot (repeatable; post-ladder,
#                           navi48bringup and navi48-stage refused)
#     --restage BUILD       first replace the staged kext (/Users/pc/navi-stage/kext/Navi48Bringup.kext, fixed:
#                           the sudoers chown rule names it) with a copy of BUILD, a .kext path on the Mac.
#                           WARNING: what loads is the AuxKC's copy; a new build needs the owner to rebuild
#                           and approve the AuxKC (at the PC) before the cycle tests it
#     --expect KEY=VALUE    an IORegistry value the Navi48Bringup instance must publish (repeatable);
#     --expect KEY~REGEX      KEY~REGEX matches with grep -E instead
#     --expect-log REGEX    a kernel log line (Navi48Bringup lines, prefix stripped) must match (repeatable)
#     --expect-no-attach    no Navi48Bringup instance may attach
#     --expect-preset NAME  a named set of the above, from lib.sh expect_preset (navi44-stage1)
#     --settle S            seconds between the load and the collection (default 20)
#     --force-cycle         run even when the preflight finds the kext missing or the chown not allowed
#                           (kext-loaded then FAILs)
#     --host H              SSH host (default mac)
#     --disarm              only disarm NVRAM (recovery: boot-args and the navi48-arm token), then exit
#     --console-demo PPM    offline: show the console check on PPM and deliberately altered copies
#
# One cycle (ticket #16: the approved kext is in the AuxKC and matched at EVERY boot; it stays inert
# unless armed after login by the consume-once NVRAM token navi48-arm=stage:N):
#   refuse post-ladder boot-args -> boot-args = disarmed + --arg extras (never navi48bringup) ->
#   `sudo shutdown -r now` UNARMED -> wait for SSH on a NEW boot (3 min, else stop) -> disarm NVRAM ->
#   wait until 120 s of uptime (the kext refuses a token seen < 90 s after boot) -> console baseline ->
#   record the inert state (token absent, ioreg, kernel log) -> write navi48-arm=stage:N -> re-match
#   (`kmutil unload -b com.navi48.bringup -P`, then `kmutil load -b com.navi48.bringup -P Navi48Bringup`)
#   -> settle -> check the token is gone -> delete it anyway -> collect ioreg, kernel log, console,
#   boot paths + AuxKC -> evaluate the criteria (lib.sh) -> next cycle. Then a per-criterion table and
#   one OVERALL line; exit 0 only on PASS (1 FAIL, 2 refused/usage, 3 bench did not come back).
#
# The token is deleted on EVERY exit path (EXIT trap, end of cycle, end of run, --disarm); a token left
# behind anyway (the bench reset in the seconds between the write and the re-match) is consumed and
# refused by the next boot's probe (the 90 s floor). Every cycle checks /Library/Extensions, the
# LaunchDaemons/Agents and the AuxKC md5 (inert-at-boot).
# Captures and logs go to runs/acceptance-<time>/ (gitignored). Plug-ins: tools/acceptance/criteria.d/*.sh
# are sourced after lib.sh; each may define crit_<name> + register_criterion and collect_<name> +
# register_collector (a collector runs over SSH after the settle and writes files into the cycle dir).
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
. "$HERE/lib.sh"

COLLECTORS=""
register_collector() { COLLECTORS="$COLLECTORS $1"; }
for plugin in "$HERE"/criteria.d/*.sh; do [ -r "$plugin" ] && . "$plugin"; done

HOST=mac CYCLES=3 STAGE=1 EXTRA=() KEXT=$STAGED_KEXT SETTLE=20
FORCE=0 MODE=run DEMO="" RESTAGE=""
REMOTE_DIR=navi-stage/acceptance          # relative to the Mac user's home
RETURN_TIMEOUT=180

usage() { sed -n '2,24p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
while [ $# -gt 0 ]; do
    case "$1" in
        --cycles) CYCLES=$2; shift ;;
        --stage) STAGE=$2; shift ;;
        --arg) EXTRA+=("$2"); shift ;;
        --restage) RESTAGE=$2; shift ;;
        --expect) register_criterion "ioreg $2"; shift ;;
        --expect-log) register_criterion "log $2"; shift ;;
        --expect-no-attach) register_criterion no-attach ;;
        --expect-preset) p=$(expect_preset "$2") || { echo "unknown preset: $2" >&2; exit 2; }
            while IFS= read -r c; do register_criterion "$c"; done <<< "$p"; shift ;;
        --settle) SETTLE=$2; shift ;;
        --force-cycle) FORCE=1 ;;
        --host) HOST=$2; shift ;;
        --disarm) MODE=disarm ;;
        --console-demo) MODE=demo; DEMO=$2; shift ;;
        -h|--help) usage ;;
        *) echo "unknown option: $1" >&2; usage ;;
    esac
    shift
done
case "$CYCLES" in ""|*[!0-9]*|0) echo "--cycles must be a positive number" >&2; exit 2 ;; esac
case "$SETTLE" in ""|*[!0-9]*) echo "--settle must be a number" >&2; exit 2 ;; esac

say() { printf '[%s] %s\n' "$(date +%T)" "$*"; }

# --- offline console demo -----------------------------------------------------------------------------
if [ "$MODE" = demo ]; then
    [ -r "$DEMO" ] || { echo "no such capture: $DEMO" >&2; exit 2; }
    d=$(mktemp -d "${TMPDIR:-/tmp}/console-demo.XXXXXX")
    printf '%-9s %s\n' "unchanged" "$(console_compare "$DEMO" "$DEMO")"
    for m in black swap tint garbage; do
        ppm_alter "$m" "$DEMO" > "$d/$m.ppm"
        printf '%-9s %s\n' "$m" "$(console_compare "$DEMO" "$d/$m.ppm")"
    done
    rm -rf "$d"; exit 0
fi

# --- SSH helpers ------------------------------------------------------------------------------------
SSHO=(-o BatchMode=yes -o ConnectTimeout=8 -o ServerAliveInterval=5 -o ServerAliveCountMax=3)
rsh() { ssh "${SSHO[@]}" "$HOST" "$@"; }
boottime() { rsh 'sysctl -n kern.boottime' 2>/dev/null | sed -nE 's/.*[{] sec = ([0-9]+).*/\1/p'; }
nvram_bootargs() {   # prints the NVRAM boot-args (empty if unset); exit 1 if SSH failed
    local out
    out=$(rsh 'nvram boot-args 2>/dev/null; echo "rc=$?"') || return 1
    case "$out" in *rc=0*) printf '%s\n' "$out" | sed -n '1s/^boot-args[[:space:]]*//p' ;; *rc=*) echo "" ;; esac
}
set_nvram_bootargs() {   # set_nvram_bootargs <value>, then read it back; exit 1 unless it reads back equal
    case "$1" in *"'"*|*'"'*|*'\'*) say "refusing to write boot-args with quotes/backslashes: $1"; return 1 ;; esac
    if [ -z "$1" ]; then rsh "sudo -n nvram -d boot-args" || return 1
    else rsh "sudo -n nvram 'boot-args=$1'" || return 1; fi
    [ "$(nvram_bootargs)" = "$1" ]
}
disarm_nvram() {   # -> prints the NVRAM boot-args after the disarm; exit 1 if it could not be done
    local cur new
    cur=$(nvram_bootargs) || { say "disarm: SSH failed reading NVRAM"; return 1; }
    new=$(disarmed_args "$cur")
    if [ "$new" != "$cur" ]; then
        set_nvram_bootargs "$new" || { say "disarm: NVRAM write did not read back"; return 1; }
    fi
    printf '%s\n' "$new"
}

token_read() { rsh "$ARM_READ_CMD" 2>/dev/null; }   # -> ARM_READ_CMD output (token_state reads it)
delete_token() {   # delete navi48-arm and read back; exit 1 unless it reads back absent
    rsh "$ARM_DELETE_CMD" >/dev/null 2>&1
    [ "$(token_read | token_state /dev/stdin)" = absent ]
}

if [ "$MODE" = disarm ]; then
    rc=0
    a=$(disarm_nvram) && say "NVRAM boot-args now: $a" || rc=1
    delete_token && say "navi48-arm token: absent" || { say "could not confirm the navi48-arm token is gone"; rc=1; }
    exit $rc
fi

# --- preflight ----------------------------------------------------------------------------------------
P=$(kext_path_problem "$KEXT"); [ -z "$P" ] || { echo "REFUSING --kext: $P" >&2; exit 2; }
for t in ${EXTRA[@]+"${EXTRA[@]}"}; do
    is_ladder_arg "$t" || { echo "REFUSING --arg $t: not a ladder boot-arg (see LADDER_ARGS in lib.sh)" >&2; exit 2; }
done
TOKEN_VALUE=$(arm_token "$STAGE") || { echo "bad --stage $STAGE (1..17)" >&2; exit 2; }
cycle_args "" ${EXTRA[@]+"${EXTRA[@]}"} >/dev/null \
    || { echo "bad --arg (navi48bringup / navi48-stage are not used any more: the navi48-arm token arms)" >&2; exit 2; }

say "preflight on $HOST"
rsh true || { echo "SSH to $HOST failed" >&2; exit 2; }
CUR=$(nvram_bootargs) || { echo "could not read NVRAM boot-args" >&2; exit 2; }
R=$(refusal_message "$CUR"); [ -z "$R" ] || { echo "$R" >&2; exit 2; }
say "NVRAM boot-args: $CUR"
case "$(token_read | token_state /dev/stdin)" in
    absent) ;;
    present*) say "a stale navi48-arm token is in NVRAM: deleting it"
        delete_token || { echo "could not delete the stale navi48-arm token" >&2; exit 2; } ;;
    *) echo "could not read NVRAM (navi48-arm)" >&2; exit 2 ;;
esac

if [ -n "$RESTAGE" ]; then
    cmd=$(kext_restage_cmd "$RESTAGE" "$KEXT") || { echo "REFUSING --restage $RESTAGE: $(kext_path_problem "$RESTAGE")" >&2; exit 2; }
    say "restaging $RESTAGE -> $KEXT"
    say "WARNING: the cycle loads the AuxKC's copy, not this bundle: unless the owner rebuilds and approves the AuxKC, the cycle tests the old build"
    rsh "test -d '$RESTAGE'" || { echo "no build at $RESTAGE on $HOST" >&2; exit 2; }
    rsh "$cmd" || { echo "restage failed (sudo -n refused chown -R 501:20? see harness notes)" >&2; exit 2; }
fi
# The load needs `sudo -n chown -R 0:0 <kext>` (/etc/sudoers.d/whateverred-chown); check it is allowed.
msg=""
rsh "test -d '$KEXT'" || msg="kext not found at $KEXT"
[ -n "$msg" ] || rsh "sudo -n -l $(kext_chown_cmd root "$KEXT" | sed 's/^sudo -n //')" >/dev/null 2>&1 \
    || msg="sudo -n may not run chown -R 0:0 $KEXT (is /etc/sudoers.d/whateverred-chown installed?)"
if [ -n "$msg" ]; then
    if [ "$FORCE" = 1 ]; then say "WARNING: $msg (--force-cycle: running anyway, kext-loaded will FAIL)"
    else echo "REFUSING: $msg. See the harness notes, or pass --force-cycle." >&2; exit 2; fi
fi

rsh "mkdir -p ~/$REMOTE_DIR" && scp -q "$HERE/fbread.c" "$HOST:$REMOTE_DIR/fbread.c" \
    && rsh "cd ~/$REMOTE_DIR && clang -O2 -Wall -framework IOKit -framework CoreFoundation fbread.c -o fbread" \
    || { echo "could not build fbread on $HOST" >&2; exit 2; }

RUN="$REPO/runs/acceptance-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$RUN"
{ echo "host $HOST cycles $CYCLES token $ARM_KEY=$TOKEN_VALUE kext $KEXT settle $SETTLE"
  echo "extra ${EXTRA[*]+${EXTRA[*]}}"; echo "criteria:"; printf '%s\n' "$CRITERIA"; } > "$RUN/params.txt"
say "run directory: $RUN"

# --- the safety net -------------------------------------------------------------------------------------
# ARGS=1 while NVRAM boot-args carry the cycle's extras (from the write until the post-boot disarm), TOKEN=1
# from the token write until it reads back absent. Any exit disarms the boot-args (if ARGS=1) and ALWAYS
# deletes the token (if the bench answers). If it never comes back: a cycle boot is unarmed anyway, and a
# token left in NVRAM is consumed and refused by the next boot's probe (90 s floor); run --disarm later.
ARGS=0 TOKEN=0
on_exit() {
    if [ "$ARGS" = 1 ]; then
        say "exit with the cycle's boot-args in NVRAM: disarming"
        disarm_nvram >/dev/null && say "disarmed" || say "COULD NOT DISARM: run tools/acceptance/accept.sh --disarm when $HOST answers"
    fi
    if delete_token; then [ "$TOKEN" = 1 ] && say "exit: navi48-arm token deleted"
    else say "COULD NOT CONFIRM the navi48-arm token is gone: run tools/acceptance/accept.sh --disarm when $HOST answers"; fi
}
trap on_exit EXIT
trap 'exit 130' INT TERM

snapshot_safety() {   # snapshot_safety <file>
    rsh 'echo "aux $(md5 -q /Library/KernelCollections/AuxiliaryKernelExtensions.kc 2>/dev/null || echo none)"
         p=$(find /Library/Extensions /Library/StagedExtensions/Library/Extensions /Library/LaunchDaemons \
                  /Library/LaunchAgents "$HOME/Library/LaunchAgents" -maxdepth 1 -iname "*navi48*" 2>/dev/null | tr "\n" " ")
         a=$(kmutil inspect -A /Library/KernelCollections/AuxiliaryKernelExtensions.kc 2>/dev/null | grep -i navi48 | awk "{print \$1 \"@\" \$2}" | tr "\n" " ")
         echo "boot-paths $p"; echo "auxkc $a"' > "$1" 2>/dev/null
}
kernel_log() {   # kernel_log <file>: this boot's "Navi48Bringup: " kernel log lines, prefix stripped
    rsh 'sudo -n log show --start "$(date -r $(sysctl -n kern.boottime | sed -E "s/.*[{] sec = ([0-9]+).*/\1/") "+%Y-%m-%d %H:%M:%S")" \
            --predicate "eventMessage CONTAINS \"Navi48Bringup\"" --style compact 2>/dev/null' \
        | sed -n 's/.*Navi48Bringup: //p' > "$1"
}
uptime_s() { rsh 'echo $(( $(date +%s) - $(sysctl -n kern.boottime | sed -E "s/.*[{] sec = ([0-9]+).*/\1/") ))' 2>/dev/null; }
capture_console() {   # capture_console <file.ppm>
    rsh "~/$REMOTE_DIR/fbread" > "$1" 2> "$1.err" || say "fbread failed: $(cat "$1.err")"
}

run_cycle() {   # run_cycle <n> <dir>; returns 3 if the bench did not come back
    local n=$1 D=$2 cur args b0 b1 t0 waited btime up
    mkdir -p "$D"
    cur=$(nvram_bootargs) || return 3
    R=$(refusal_message "$cur"); [ -z "$R" ] || { say "$R"; echo "$R" > "$D/refused.txt"; return 2; }
    args=$(cycle_args "$cur" ${EXTRA[@]+"${EXTRA[@]}"})
    printf '%s\n' "$cur" > "$D/nvram-before.txt"

    if [ "$args" != "$cur" ]; then
        say "cycle $n: boot-args for the (unarmed) boot: $args"
        ARGS=1
        set_nvram_bootargs "$args" || { say "boot-args did not read back; stopping"; return 2; }
    fi
    delete_token || { say "cycle $n: could not confirm NVRAM has no navi48-arm token; stopping"; return 2; }
    b0=$(boottime)
    snapshot_safety "$D/safety-boot.txt"
    say "cycle $n: cold boot, UNARMED (sudo shutdown -r now)"
    t0=$(date +%s)
    rsh 'sudo -n shutdown -r now' >/dev/null 2>&1
    while :; do
        sleep 5
        waited=$(( $(date +%s) - t0 ))
        b1=$(boottime)
        [ -n "$b1" ] && [ "$b1" != "$b0" ] && break
        if [ "$waited" -ge "$RETURN_TIMEOUT" ]; then
            echo timeout > "$D/ssh-return.txt"; say "cycle $n: $HOST did not return within $RETURN_TIMEOUT s; STOPPING"
            return 3
        fi
    done
    echo "$waited" > "$D/ssh-return.txt"; say "cycle $n: back over SSH after $waited s (new boot $b1)"
    rsh 'sysctl -n kern.bootargs' > "$D/boot-bootargs.txt"

    disarm_nvram > "$D/nvram-after-disarm.txt" && ARGS=0
    say "cycle $n: NVRAM disarmed: $(cat "$D/nvram-after-disarm.txt")"

    # The kext refuses a token seen < 90 s after boot, and the desktop keeps changing for a while after
    # login (Dock, login items, notifications); wait, then until two captures 15 s apart agree.
    while up=$(uptime_s) && [ "${up:-0}" -lt "$ARM_MIN_UPTIME" ] 2>/dev/null; do sleep 5; done
    capture_console "$D/console-before.ppm"
    local tries=0 v
    while [ "$tries" -lt 12 ]; do
        sleep 15; tries=$((tries + 1))
        mv "$D/console-before.ppm" "$D/console-prev.ppm"
        capture_console "$D/console-before.ppm"
        v=$(console_compare "$D/console-prev.ppm" "$D/console-before.ppm")
        echo "settle $tries: $v" >> "$D/console-settle.txt"
        case "$v" in PASS*) break ;; esac
    done
    rm -f "$D/console-prev.ppm"
    say "cycle $n: console baseline after $tries settle capture(s): $(tail -n 1 "$D/console-settle.txt")"
    snapshot_safety "$D/safety-before.txt"

    # The inert state the boot-time (AuxKC) match left: no token, no instance, no Navi48 properties, the inert line.
    token_read > "$D/token-before.txt"
    rsh 'out=$(ioreg -l -w0 2>/dev/null) && { printf "%s\n" "$out" | grep -E "\"Navi48,|<class Navi48Bringup"; echo ioreg-read-ok; }' > "$D/ioreg-before.txt" 2>/dev/null
    kernel_log "$D/log-before.txt"
    say "cycle $n: before arming: token $(token_state "$D/token-before.txt"); $(grep -cF "$INERT_LINE" "$D/log-before.txt") inert line(s)"
    case "$(token_state "$D/token-before.txt")" in absent) ;; *) say "cycle $n: NVRAM not clean before arming; stopping"; return 2 ;; esac
    boottime > "$D/alive.txt"

    # kmutil loads only a root:wheel bundle; hand it to root before the re-match.
    rsh "$(kext_chown_cmd root "$KEXT")" > "$D/kext-chown.txt" 2>&1
    echo $? > "$D/kext-chown.rc"
    [ "$(cat "$D/kext-chown.rc")" = 0 ] || say "cycle $n: sudo -n refused chown -R 0:0: $(head -n 1 "$D/kext-chown.txt")"

    say "cycle $n: arming: $ARM_KEY=$TOKEN_VALUE"
    TOKEN=1
    rsh "$(arm_set_cmd "$TOKEN_VALUE")" >/dev/null 2>&1
    token_read > "$D/token-set.txt"
    [ "$(token_state "$D/token-set.txt")" = "present $TOKEN_VALUE" ] \
        || { say "cycle $n: the token did not read back ($(token_state "$D/token-set.txt")); stopping"; return 2; }
    say "cycle $n: re-match: $REMATCH_UNLOAD_CMD; $REMATCH_LOAD_CMD"
    timeout 120 ssh "${SSHO[@]}" "$HOST" "$REMATCH_UNLOAD_CMD" > "$D/kmutil-unload.txt" 2>&1
    echo $? > "$D/kmutil-unload.rc"
    timeout 300 ssh "${SSHO[@]}" "$HOST" "$REMATCH_LOAD_CMD" > "$D/kmutil-load.txt" 2>&1
    echo $? > "$D/kmutil-load.rc"
    say "cycle $n: kmutil unload rc $(cat "$D/kmutil-unload.rc"), load rc $(cat "$D/kmutil-load.rc"); settling $SETTLE s"
    sleep "$SETTLE"

    token_read > "$D/token-after.txt"
    say "cycle $n: token after the re-match: $(token_state "$D/token-after.txt")"
    if delete_token; then TOKEN=0; else say "cycle $n: could not confirm the token is gone"; fi
    token_read > "$D/token-final.txt"

    btime=$(boottime); [ -n "$btime" ] && echo "$btime" >> "$D/alive.txt"
    rsh 'kmutil showloaded --list-only 2>/dev/null | grep -i navi48' > "$D/loaded.txt"
    rsh 'ioreg -rc Navi48Bringup -l -w0' > "$D/ioreg.txt" 2>&1
    kernel_log "$D/log.txt"
    capture_console "$D/console-after.ppm"
    snapshot_safety "$D/safety-after.txt"
    for c in $COLLECTORS; do "collect_$(printf '%s' "$c" | tr - _)" "$D" || say "collector $c failed"; done
    return 0
}

status=0
n=1
while [ "$n" -le "$CYCLES" ]; do
    D="$RUN/cycle-$n"
    run_cycle "$n" "$D"; rc=$?
    evaluate_cycle "$D"
    say "cycle $n results:"; sed 's/^/    /' "$D/results.tsv"
    if [ "$rc" -ne 0 ]; then status=$rc; break; fi
    n=$((n + 1))
done

# Final check: whatever happened above, NVRAM must end disarmed and without a token (the EXIT trap deletes it again).
if [ "$status" -ne 3 ]; then
    fin=$(disarm_nvram) && { ARGS=0; say "final NVRAM boot-args: $fin"; echo "$fin" > "$RUN/nvram-final.txt"; }
    delete_token && { TOKEN=0; say "final navi48-arm token: absent"; }
fi

echo
result_table "$RUN" "$CYCLES" | tee "$RUN/table.txt"
rc=${PIPESTATUS[0]}
[ "$status" -eq 3 ] && exit 3
[ "$status" -eq 2 ] && exit 2
exit "$rc"
