#!/usr/bin/env bash
# accept.sh: the bring-up acceptance harness. RUN ON THE LAPTOP; it drives the test bench over SSH.
#
#   tools/acceptance/accept.sh [options]
#     --cycles N            cold-boot cycles (default 3); the run passes only if every cycle passes
#     --stage N             arm with navi48-stage=N as well (the ladder target)
#     --arg NAME=VALUE      an extra LADDER boot-arg for the armed boot (repeatable; post-ladder refused)
#     --kext PATH           the staged kext on the Mac (default /Users/pc/navi-stage/kext/Navi48Bringup.kext):
#                           absolute, under /Users/, root:wheel-owned (kmutil refuses anything else)
#     --expect KEY=VALUE    an IORegistry value the Navi48Bringup instance must publish (repeatable);
#     --expect KEY~REGEX      KEY~REGEX matches with grep -E instead
#     --expect-log REGEX    a kernel log line (Navi48Bringup lines, prefix stripped) must match (repeatable)
#     --expect-no-attach    no Navi48Bringup instance may attach
#     --settle S            seconds between the load and the collection (default 20)
#     --force-cycle         run even when the preflight finds the kext unloadable (the load then FAILs)
#     --host H              SSH host (default mac)
#     --disarm              only disarm NVRAM (recovery), then exit
#     --console-demo PPM    offline: show the console check on PPM and deliberately altered copies
#
# One cycle: refuse post-ladder boot-args -> arm NVRAM -> `sudo shutdown -r now` -> wait for SSH on
# a NEW boot (3 min, else stop) -> disarm NVRAM straight away (the running boot keeps its armed
# kern.bootargs, so a panic during the load comes back unarmed) -> console capture -> `sudo kmutil
# load -p` from the staging path -> settle -> collect ioreg, kernel log, console capture, boot-path
# and AuxKC snapshot -> evaluate the criteria (lib.sh) -> next cycle. Then a per-criterion table and
# one OVERALL line; exit 0 only on PASS (1 FAIL, 2 refused/usage, 3 bench did not come back).
#
# Never installs anything that loads at boot: the kext stays at --kext, it is loaded by hand after
# boot, and every cycle checks /Library/Extensions, the LaunchDaemons/Agents and the AuxKC.
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

HOST=mac CYCLES=3 STAGE="" EXTRA=() KEXT=/Users/pc/navi-stage/kext/Navi48Bringup.kext SETTLE=20
FORCE=0 MODE=run DEMO=""
REMOTE_DIR=navi-stage/acceptance          # relative to the Mac user's home
RETURN_TIMEOUT=180

usage() { sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 2; }
while [ $# -gt 0 ]; do
    case "$1" in
        --cycles) CYCLES=$2; shift ;;
        --stage) STAGE=$2; shift ;;
        --arg) EXTRA+=("$2"); shift ;;
        --kext) KEXT=$2; shift ;;
        --expect) register_criterion "ioreg $2"; shift ;;
        --expect-log) register_criterion "log $2"; shift ;;
        --expect-no-attach) register_criterion no-attach ;;
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
boottime() { rsh 'sysctl -n kern.boottime' 2>/dev/null | sed -nE 's/.*sec = ([0-9]+).*/\1/p'; }
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

if [ "$MODE" = disarm ]; then
    a=$(disarm_nvram) || exit 1
    say "NVRAM boot-args now: $a"; exit 0
fi

# --- preflight ----------------------------------------------------------------------------------------
P=$(kext_path_problem "$KEXT"); [ -z "$P" ] || { echo "REFUSING --kext: $P" >&2; exit 2; }
for t in ${EXTRA[@]+"${EXTRA[@]}"}; do
    is_ladder_arg "$t" || { echo "REFUSING --arg $t: not a ladder boot-arg (see LADDER_ARGS in lib.sh)" >&2; exit 2; }
done
armed_args "" "$STAGE" ${EXTRA[@]+"${EXTRA[@]}"} >/dev/null || { echo "bad --stage/--arg" >&2; exit 2; }

say "preflight on $HOST"
rsh true || { echo "SSH to $HOST failed" >&2; exit 2; }
CUR=$(nvram_bootargs) || { echo "could not read NVRAM boot-args" >&2; exit 2; }
R=$(refusal_message "$CUR"); [ -z "$R" ] || { echo "$R" >&2; exit 2; }
say "NVRAM boot-args: $CUR"

own=$(rsh "test -d '$KEXT' || echo MISSING; find '$KEXT' \\( ! -user 0 -o ! -group 0 \\) -print 2>/dev/null | head -n 1")
if [ -n "$own" ]; then
    case "$own" in MISSING*) msg="kext not found at $KEXT" ;; *) msg="kext is not root:wheel-owned ($own); kmutil will refuse it" ;; esac
    if [ "$FORCE" = 1 ]; then say "WARNING: $msg (--force-cycle: running anyway, kext-loaded will FAIL)"
    else echo "REFUSING: $msg. Stage it root-owned (see the harness notes) or pass --force-cycle." >&2; exit 2; fi
fi

rsh "mkdir -p ~/$REMOTE_DIR" && scp -q "$HERE/fbread.c" "$HOST:$REMOTE_DIR/fbread.c" \
    && rsh "cd ~/$REMOTE_DIR && clang -O2 -Wall -framework IOKit -framework CoreFoundation fbread.c -o fbread" \
    || { echo "could not build fbread on $HOST" >&2; exit 2; }

RUN="$REPO/runs/acceptance-$(date +%Y%m%d-%H%M%S)"; mkdir -p "$RUN"
{ echo "host $HOST cycles $CYCLES stage ${STAGE:-none} kext $KEXT settle $SETTLE"
  echo "extra ${EXTRA[*]+${EXTRA[*]}}"; echo "criteria:"; printf '%s\n' "$CRITERIA"; } > "$RUN/params.txt"
say "run directory: $RUN"

# --- the safety net -------------------------------------------------------------------------------------
# ARMED=1 from the NVRAM write until the post-boot disarm succeeds. Any exit in between disarms
# (if the bench answers). If the bench never comes back NVRAM stays armed, but nothing loads the kext
# at boot, so that boot is as safe as an unarmed one; run --disarm once it answers.
ARMED=0
on_exit() {
    if [ "$ARMED" = 1 ]; then
        say "exit while armed: disarming"
        disarm_nvram >/dev/null && say "disarmed" || say "COULD NOT DISARM: run tools/acceptance/accept.sh --disarm when $HOST answers"
    fi
}
trap on_exit EXIT
trap 'exit 130' INT TERM

snapshot_safety() {   # snapshot_safety <file>
    rsh 'echo "aux $(md5 -q /Library/KernelCollections/AuxiliaryKernelExtensions.kc 2>/dev/null || echo none)"
         p=$(find /Library/Extensions /Library/StagedExtensions/Library/Extensions /Library/LaunchDaemons \
                  /Library/LaunchAgents "$HOME/Library/LaunchAgents" -maxdepth 1 -iname "*navi48*" 2>/dev/null | tr "\n" " ")
         a=$(kmutil inspect -A /Library/KernelCollections/AuxiliaryKernelExtensions.kc 2>/dev/null | grep -i navi48 | awk "{print \"AuxKC:\" \$1}" | tr "\n" " ")
         echo "boot-paths $p$a"' > "$1" 2>/dev/null
}
capture_console() {   # capture_console <file.ppm>
    rsh "~/$REMOTE_DIR/fbread" > "$1" 2> "$1.err" || say "fbread failed: $(cat "$1.err")"
}

run_cycle() {   # run_cycle <n> <dir>; returns 3 if the bench did not come back
    local n=$1 D=$2 cur armed b0 b1 t0 waited btime
    mkdir -p "$D"
    cur=$(nvram_bootargs) || return 3
    R=$(refusal_message "$cur"); [ -z "$R" ] || { say "$R"; echo "$R" > "$D/refused.txt"; return 2; }
    armed=$(armed_args "$cur" "$STAGE" ${EXTRA[@]+"${EXTRA[@]}"})
    printf '%s\n' "$cur" > "$D/nvram-before.txt"

    say "cycle $n: arming: $armed"
    ARMED=1
    set_nvram_bootargs "$armed" || { say "arming did not read back; stopping"; return 2; }
    b0=$(boottime)
    snapshot_safety "$D/safety-boot.txt"
    say "cycle $n: cold boot (sudo shutdown -r now)"
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
    rsh 'sysctl -n kern.bootargs' > "$D/armed-bootargs.txt"

    disarm_nvram > "$D/nvram-after-disarm.txt" && ARMED=0
    say "cycle $n: NVRAM disarmed: $(cat "$D/nvram-after-disarm.txt")"

    # The desktop keeps changing for a while after login (Dock, login items, notifications), and the
    # console check compares against the baseline, so wait until two captures 15 s apart agree.
    while [ "$(rsh 'echo $(( $(date +%s) - $(sysctl -n kern.boottime | sed -E "s/.*sec = ([0-9]+).*/\1/") ))')" -lt 60 ] 2>/dev/null; do sleep 5; done
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
    boottime > "$D/alive.txt"

    say "cycle $n: sudo kmutil load -p $KEXT"
    timeout 300 ssh "${SSHO[@]}" "$HOST" "sudo -n kmutil load -p '$KEXT'" > "$D/kmutil-load.txt" 2>&1
    echo $? > "$D/kmutil-load.rc"
    say "cycle $n: kmutil rc $(cat "$D/kmutil-load.rc"); settling $SETTLE s"
    sleep "$SETTLE"

    btime=$(boottime); [ -n "$btime" ] && echo "$btime" >> "$D/alive.txt"
    rsh 'kmutil showloaded --list-only 2>/dev/null | grep -i navi48' > "$D/loaded.txt"
    rsh 'ioreg -rc Navi48Bringup -l -w0' > "$D/ioreg.txt" 2>&1
    rsh 'sudo -n log show --start "$(date -r $(sysctl -n kern.boottime | sed -E "s/.*sec = ([0-9]+).*/\1/") "+%Y-%m-%d %H:%M:%S")" \
            --predicate "eventMessage CONTAINS \"Navi48Bringup\"" --style compact 2>/dev/null' \
        | sed -n 's/.*Navi48Bringup: //p' > "$D/log.txt"
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

# Final check: whatever happened above, NVRAM must end disarmed.
if [ "$status" -ne 3 ]; then
    fin=$(disarm_nvram) && { ARMED=0; say "final NVRAM boot-args: $fin"; echo "$fin" > "$RUN/nvram-final.txt"; }
fi

echo
result_table "$RUN" "$CYCLES" | tee "$RUN/table.txt"
rc=${PIPESTATUS[0]}
[ "$status" -eq 3 ] && exit 3
[ "$status" -eq 2 ] && exit 2
exit "$rc"
