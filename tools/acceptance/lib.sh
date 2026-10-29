# lib.sh: the acceptance harness's pure logic. Sourced by accept.sh and test.sh; no SSH, no side effects.
# Portable to bash 3.2 (the Mac) and Git Bash (the laptop).

# --- boot-args ------------------------------------------------------------------------------------
# The bring-up kext's boot-args that the first spec's ladder (stages 1-17) may use. Every OTHER
# navi48* boot-arg is post-ladder (native S1b, accel gate/experiment, boot chain, the optional
# self-tests, G5/M6, Metal, display...) and makes the harness refuse to run. An allowlist, so a
# boot-arg added upstream later is refused until someone classifies it here (fail closed).
LADDER_ARGS="navi48bringup navi48-stage navi48-psp navi48-smu navi48-interrupts navi48-smu-full
navi48-smu-basic navi48-mes-nonfatal navi48-cp-legacy-rb navi48-idle-clamp navi48-kcq navi48-hsa-abi
navi48-doorbell-legacy navi48-gart-high navi48-pdb0"

# The stale NVIDIA boot-args the bench's NVRAM still carries (classified against the bench's real
# args on 2026-10-29). Disarm also drops any other nv*/-nv* arg.
NVIDIA_ARGS="nvfb nvaccel nvfbheads -nvkmsnosmooth gfxrst nvrmsettle -nvoff"

# arg_name <token>: the name PE_parse_boot_argn matches ("-v" stays "-v", "a=1" is "a").
arg_name() { printf '%s\n' "${1%%=*}"; }

_in_list() {   # _in_list <word> <list>
    case " $(printf '%s' "$2" | tr '\n' ' ') " in *" $1 "*) return 0 ;; esac
    return 1
}

is_bringup_arg() { case "$(arg_name "$1")" in navi48*) return 0 ;; esac; return 1; }
is_ladder_arg()  { _in_list "$(arg_name "$1")" "$LADDER_ARGS"; }
is_nvidia_arg() {
    local n; n=$(arg_name "$1")
    _in_list "$n" "$NVIDIA_ARGS" && return 0
    case "$n" in nv*|-nv*) return 0 ;; esac
    return 1
}

# post_ladder_args <boot-args>: the names of the post-ladder boot-args set, space-separated, in order.
post_ladder_args() {
    local t out=""
    for t in $1; do
        is_bringup_arg "$t" && ! is_ladder_arg "$t" && out="$out $(arg_name "$t")"
    done
    printf '%s\n' "${out# }"
}

# refusal_message <boot-args>: empty when the harness may run, else the line that names the offenders.
refusal_message() {
    local bad; bad=$(post_ladder_args "$1")
    [ -n "$bad" ] && printf 'REFUSING: post-ladder boot-arg(s) set in NVRAM: %s\n' "$bad"
    return 0
}

# kext_path_problem <path on the Mac>: empty when <path> is an acceptable non-boot staging path (an
# absolute .kext under /Users/), else why not. Nothing under a home directory loads at boot; the
# harness additionally checks the boot paths and the AuxKC around every load (never-at-boot).
kext_path_problem() {
    case "$1" in
        *" "*)        echo "path contains a space" ;;
        /*) case "$1" in
                */Library/Extensions/*|*/Library/StagedExtensions/*|*/LaunchDaemons/*|*/LaunchAgents/*|*/EFI/*|*/KernelCollections/*)
                    echo "refusing a boot path: $1" ;;
                /Users/*/*.kext|/Users/*/*.kext/) ;;
                /Users/*) echo "not a .kext bundle: $1" ;;
                *) echo "not under a home directory (/Users/...): $1" ;;
            esac ;;
        *)            echo "not an absolute path: $1" ;;
    esac
}

# kmutil loads only a root:wheel-owned bundle, and pc may chown exactly one path, both ways
# (/etc/sudoers.d/whateverred-chown). The load path and the rule must agree, so this is fixed.
STAGED_KEXT=/Users/pc/navi-stage/kext/Navi48Bringup.kext

# kext_chown_cmd root|pc <kext>: the remote command handing <kext> to root (0:0, for the load) or
# back to pc (501:20, for restaging). Exit 1 and print nothing for any other owner or path.
kext_chown_cmd() {
    local own
    case "$1" in root) own=0:0 ;; pc) own=501:20 ;; *) return 1 ;; esac
    [ "$2" = "$STAGED_KEXT" ] || return 1
    echo "sudo -n /usr/sbin/chown -R $own $2"
}

# kext_restage_cmd <built kext on the Mac> <staged kext>: the remote command replacing the staged
# bundle with a copy of a build. The old bundle is root-owned after a load, so pc gets it back first.
kext_restage_cmd() {
    local back
    [ -z "$(kext_path_problem "$1")" ] || return 1
    back=$(kext_chown_cmd pc "$2") || return 1
    echo "if test -e $2; then $back || exit 1; rm -rf $2 || exit 1; fi; mkdir -p ${2%/*} && cp -R $1 $2"
}

# disarmed_args <boot-args>: the args with every navi48* and every NVIDIA arg removed, order kept.
disarmed_args() {
    local t out=""
    for t in $1; do
        is_bringup_arg "$t" || is_nvidia_arg "$t" || out="$out $t"
    done
    printf '%s\n' "${out# }"
}

# armed_args <boot-args> [stage] [extra ladder args...]: disarmed args + navi48bringup=1, then
# navi48-stage=<stage> if given, then the extras. Returns 1 (prints nothing) on a bad stage or a
# non-ladder extra.
armed_args() {
    local cur=$1 stage=${2:-} t out
    shift; [ $# -gt 0 ] && shift
    case "$stage" in *[!0-9]*) return 1 ;; esac
    out="$(disarmed_args "$cur") navi48bringup=1"
    [ -n "$stage" ] && out="$out navi48-stage=$stage"
    for t in "$@"; do
        is_ladder_arg "$t" || return 1
        case "$(arg_name "$t")" in navi48bringup|navi48-stage) return 1 ;; esac
        out="$out $t"
    done
    printf '%s\n' "${out# }"
}

# --- console integrity ------------------------------------------------------------------------------
# console_compare <before.ppm> <after.ppm>: compares two fbread captures (P3, block-averaged scanout
# memory) and prints one line, "PASS <metrics>" (exit 0) or "FAIL <reason> <metrics>" (exit 1).
# Reasons, in the order they are tested:
#   missing    a capture file is absent or unreadable      size      the dimensions differ
#   baseline   the BEFORE capture is black or flat, so there is nothing to compare against
#   black      after: mean luma < 10                        blank     after: luma std-dev < 3 (one flat colour)
#   colour     a channel's mean moved > 12, or > 5% of cells changed while the picture's
#              brightness structure survived (luma correlation >= 0.9): swapped/tinted channels
#   corrupted  > 5% of cells changed (a channel moved > 32) and the structure did not survive
# A few changed cells (clock, cursor) stay under 5% and pass. Thresholds are on 0-255 cell averages.
CONSOLE_BLACK_LUMA=10 CONSOLE_FLAT_SD=3 CONSOLE_CELL_DELTA=32 CONSOLE_CHANGED_PCT=5
CONSOLE_SHIFT=12 CONSOLE_CORR=0.9
console_compare() {
    if [ ! -r "$1" ] || [ ! -r "$2" ]; then echo "FAIL missing capture (${1##*/} / ${2##*/})"; return 1; fi
    awk -v BL="$CONSOLE_BLACK_LUMA" -v FS_="$CONSOLE_FLAT_SD" -v CD="$CONSOLE_CELL_DELTA" \
        -v CP="$CONSOLE_CHANGED_PCT" -v SH="$CONSOLE_SHIFT" -v CR="$CONSOLE_CORR" '
        function ab(x) { return x < 0 ? -x : x }
        FNR == 1 { f++; k = 0 }
        { for (i = 1; i <= NF; i++) tok[f, k++] = $i; ntok[f] = k }
        END {
            if (tok[1,0] != "P3" || tok[2,0] != "P3") { print "FAIL missing capture (not a P3 PPM)"; exit 1 }
            w = tok[1,1]; h = tok[1,2]
            if (w != tok[2,1] || h != tok[2,2]) { printf "FAIL size %sx%s vs %sx%s\n", w, h, tok[2,1], tok[2,2]; exit 1 }
            n = w * h
            if (n < 1 || ntok[1] < 4 + 3 * n || ntok[2] < 4 + 3 * n) { print "FAIL missing capture (truncated)"; exit 1 }
            for (c = 0; c < n; c++) {
                for (f = 1; f <= 2; f++) {
                    r[f] = tok[f, 4 + 3*c]; g[f] = tok[f, 5 + 3*c]; b[f] = tok[f, 6 + 3*c]
                    l[f] = 0.299 * r[f] + 0.587 * g[f] + 0.114 * b[f]
                    sl[f] += l[f]; sll[f] += l[f] * l[f]
                    sr[f] += r[f]; sg[f] += g[f]; sb[f] += b[f]
                }
                slx += l[1] * l[2]
                d = ab(r[2] - r[1]); if (ab(g[2] - g[1]) > d) d = ab(g[2] - g[1]); if (ab(b[2] - b[1]) > d) d = ab(b[2] - b[1])
                if (d > CD) changed++
            }
            for (f = 1; f <= 2; f++) {
                ml[f] = sl[f] / n; v = sll[f] / n - ml[f] * ml[f]; sd[f] = v > 0 ? sqrt(v) : 0
            }
            corr = (sd[1] > 0 && sd[2] > 0) ? (slx / n - ml[1] * ml[2]) / (sd[1] * sd[2]) : 0
            dr = (sr[2] - sr[1]) / n; dg = (sg[2] - sg[1]) / n; db = (sb[2] - sb[1]) / n
            pct = 100 * changed / n
            m = sprintf("luma=%.1f->%.1f sd=%.1f->%.1f changed=%.1f%% shift(r,g,b)=%+.1f,%+.1f,%+.1f corr=%.3f cells=%dx%d",
                        ml[1], ml[2], sd[1], sd[2], pct, dr, dg, db, corr, w, h)
            if (ml[1] < BL || sd[1] < FS_)                                 { print "FAIL baseline " m; exit 1 }
            if (ml[2] < BL)                                                { print "FAIL black " m; exit 1 }
            if (sd[2] < FS_)                                               { print "FAIL blank " m; exit 1 }
            if (ab(dr) > SH || ab(dg) > SH || ab(db) > SH)                 { print "FAIL colour " m; exit 1 }
            if (pct > CP && corr >= CR)                                    { print "FAIL colour " m; exit 1 }
            if (pct > CP)                                                  { print "FAIL corrupted " m; exit 1 }
            print "PASS " m
        }' "$1" "$2"
}

# ppm_alter <black|swap|tint|garbage> <in.ppm>: a deliberately damaged copy of a capture, on stdout.
# swap exchanges red and blue, tint moves 40 from red into green, garbage overwrites the middle
# third of the rows with a pseudo-random pattern. Used by the tests and by `accept.sh --console-demo`.
ppm_alter() {
    awk -v mode="$1" '
        FNR == 1 { print; next }
        FNR == 2 { w = $1; print; next }
        FNR == 3 { print; next }
        {
            c = FNR - 4; y = int(c / w); r = $1; g = $2; b = $3
            if (mode == "black") { r = g = b = 0 }
            else if (mode == "swap") { t = r; r = b; b = t }
            else if (mode == "tint") { r = r > 40 ? r - 40 : 0; g = g < 215 ? g + 40 : 255 }
            else if (mode == "garbage" && h3 > 0 && y >= h3 && y < 2 * h3) {
                s = (c * 1103515245 + 12345) % 2147483648
                r = s % 256; g = int(s / 256) % 256; b = int(s / 65536) % 256
            }
            print r, g, b
        }' h3="$(awk 'NR == 2 { print int($2 / 3); exit }' "$2")" "$2"
}

# --- criteria -----------------------------------------------------------------------------------------
# A criterion is a function crit_<name> <cycle-dir> that reads ONLY files accept.sh collected into the
# cycle directory and prints one line, "PASS <detail>" or "FAIL <detail>". To add one (later tickets:
# stage results, SMU version, BAR0 log, thermal watch): define crit_<name> (dashes in <name> become
# underscores), call `register_criterion <name>`, and if it needs data the bench does not collect yet,
# define collect_<name> <cycle-dir> (it may call rsh) and `register_collector <name>`. Put both in
# tools/acceptance/criteria.d/<name>.sh; accept.sh sources every file there. Expected values from the command line become
# criteria too: "ioreg KEY=VALUE", "ioreg KEY~REGEX", "log REGEX", "no-attach".
#
# Files in a cycle directory (written by accept.sh):
#   armed-bootargs.txt      kern.bootargs of the armed boot     ssh-return.txt   seconds, or "timeout"
#   nvram-after-disarm.txt  NVRAM boot-args after the disarm    kmutil-load.{txt,rc}  load output, exit code
#   kext-chown.{txt,rc}     output and exit code of `sudo -n chown -R 0:0 <kext>` just before the load
#   loaded.txt              kmutil showloaded lines for the kext
#   safety-{before,after}.txt  "aux <AuxKC md5>" + "boot-paths <navi48 kexts/plists found in boot paths>"
#   alive.txt               kern.boottime seconds before the load, then after (missing line = no answer)
#   console-{before,after}.ppm  fbread captures      ioreg.txt  `ioreg -rc Navi48Bringup -l -w0`
#   log.txt                 this boot's kernel log lines containing "Navi48Bringup: " (prefix stripped)
CRITERIA="armed-boot
ssh-return
disarmed
never-at-boot
kext-loaded
ssh-alive
console"

register_criterion() { CRITERIA="$CRITERIA
$1"; }

_first_line() { [ -r "$1" ] && head -n 1 "$1"; }

crit_armed_boot() {
    local a; a=$(_first_line "$1/armed-bootargs.txt")
    if is_armed "$a"; then echo "PASS running boot has navi48bringup ($a)"
    else echo "FAIL running boot is not armed: '${a:-<not read>}'"; fi
}
crit_ssh_return() {
    local s; s=$(_first_line "$1/ssh-return.txt")
    case "$s" in ""|*[!0-9]*) echo "FAIL SSH did not return (${s:-not recorded})" ;; *) echo "PASS back over SSH in $s s" ;; esac
}
crit_disarmed() {
    local f="$1/nvram-after-disarm.txt" a t bad=""
    [ -r "$f" ] || { echo "FAIL NVRAM not read after the disarm"; return; }
    a=$(_first_line "$f")
    for t in $a; do is_bringup_arg "$t" || is_nvidia_arg "$t" && bad="$bad $t"; done
    if [ -n "$bad" ]; then echo "FAIL left in NVRAM:$bad"; else echo "PASS NVRAM boot-args: $a"; fi
}
crit_never_at_boot() {
    local b="$1/safety-before.txt" a="$1/safety-after.txt" ab aa paths
    [ -r "$b" ] && [ -r "$a" ] || { echo "FAIL safety snapshot missing"; return; }
    ab=$(awk '$1 == "aux" { print $2 }' "$b"); aa=$(awk '$1 == "aux" { print $2 }' "$a")
    paths=$(awk '$1 == "boot-paths" { $1 = ""; sub(/^ /, ""); print }' "$a")
    if [ -n "$paths" ]; then echo "FAIL kext/plist in a boot path: $paths"
    elif [ "$ab" != "$aa" ]; then echo "FAIL AuxKC changed ($ab -> $aa)"
    else echo "PASS AuxKC unchanged ($aa), nothing in boot paths"; fi
}
crit_kext_loaded() {
    local rc why
    rc=$(_first_line "$1/kext-chown.rc")
    if [ -n "$rc" ] && [ "$rc" != 0 ]; then
        echo "FAIL sudo -n refused chown -R 0:0 $STAGED_KEXT (is /etc/sudoers.d/whateverred-chown installed?): $(_first_line "$1/kext-chown.txt")"
        return
    fi
    rc=$(_first_line "$1/kmutil-load.rc")
    if [ "$rc" = 0 ] && [ -s "$1/loaded.txt" ]; then echo "PASS $(_first_line "$1/loaded.txt")"; return; fi
    # the specific reason (ownership, approval, ...) comes after kmutil's generic "Validating ... failed:"
    why=$(grep -iE 'failed to bind|could not use|invalid|not approved|denied|permission' "$1/kmutil-load.txt" 2>/dev/null | head -n 1)
    [ -n "$why" ] || why=$(grep -iE 'error|fail' "$1/kmutil-load.txt" 2>/dev/null | head -n 1)
    why=$(printf '%s' "$why" | sed 's/^[[:space:]]*[0-9]*:[[:space:]]*//')
    echo "FAIL kmutil load rc ${rc:-?}${why:+: $why}"
}
crit_ssh_alive() {
    local b a
    b=$(sed -n 1p "$1/alive.txt" 2>/dev/null); a=$(sed -n 2p "$1/alive.txt" 2>/dev/null)
    if [ -z "$a" ]; then echo "FAIL SSH did not answer after the load"
    elif [ "$a" != "$b" ]; then echo "FAIL the machine rebooted after the load (boottime $b -> $a): panic?"
    else echo "PASS same boot answers after the load"; fi
}
crit_console() { console_compare "$1/console-before.ppm" "$1/console-after.ppm"; }
crit_no_attach() {
    if grep -q '<class Navi48Bringup' "$1/ioreg.txt" 2>/dev/null; then echo "FAIL a Navi48Bringup instance attached"
    else echo "PASS no Navi48Bringup instance attached"; fi
}

# ioreg_value <ioreg.txt> <key>: the first value of "key" = value, quotes stripped.
ioreg_value() {
    awk -v k="\"$2\"" '{ i = index($0, k " = ") } i { v = substr($0, i + length(k) + 3)
        if (v ~ /^".*"$/) v = substr(v, 2, length(v) - 2); print v; exit }' "$1" 2>/dev/null
}
crit_ioreg() {   # crit_ioreg "KEY=VALUE" | "KEY~REGEX" <dir>
    local e=$1 key want op v
    case "$e" in *=*) key=${e%%=*}; want=${e#*=}; op="=" ;; *~*) key=${e%%~*}; want=${e#*~}; op="~" ;;
        *) echo "FAIL bad expectation '$e' (KEY=VALUE or KEY~REGEX)"; return ;; esac
    v=$(ioreg_value "$2/ioreg.txt" "$key")
    if [ -z "$v" ] && ! grep -qF "\"$key\" = " "$2/ioreg.txt" 2>/dev/null; then echo "FAIL $key absent"; return; fi
    if [ "$op" = "=" ] && [ "$v" = "$want" ]; then echo "PASS $key = $v"
    elif [ "$op" = "~" ] && printf '%s\n' "$v" | grep -Eq -- "$want"; then echo "PASS $key = $v"
    else echo "FAIL $key = $v, expected $op $want"; fi
}
crit_log() {   # crit_log REGEX <dir>
    local m; m=$(grep -E -- "$1" "$2/log.txt" 2>/dev/null | head -n 1)
    if [ -n "$m" ]; then echo "PASS $m"; else echo "FAIL no log line matches /$1/"; fi
}

# evaluate_criterion <name> <cycle-dir>
evaluate_criterion() {
    local fn
    case "$1" in
        "ioreg "*) crit_ioreg "${1#ioreg }" "$2"; return ;;
        "log "*)   crit_log "${1#log }" "$2"; return ;;
    esac
    fn=crit_$(printf '%s' "$1" | tr - _)
    if type "$fn" >/dev/null 2>&1; then "$fn" "$2"; else echo "FAIL unknown criterion '$1'"; fi
}

# evaluate_cycle <cycle-dir>: every criterion in $CRITERIA -> <cycle-dir>/results.tsv
evaluate_cycle() {
    local name line
    : > "$1/results.tsv"
    while IFS= read -r name; do
        [ -n "$name" ] || continue
        line=$(evaluate_criterion "$name" "$1" | head -n 1)
        case "$line" in PASS*|FAIL*) ;; *) line="FAIL criterion printed nothing usable: $line" ;; esac
        printf '%s\t%s\t%s\n' "$name" "${line%% *}" "${line#* }" >> "$1/results.tsv"
    done <<EOF
$CRITERIA
EOF
}

# result_table <run-dir> <cycles-requested>: per-criterion table over <run-dir>/cycle-N/results.tsv,
# the failure details, then "OVERALL: PASS|FAIL ...". Exit 0 only when every requested cycle ran and
# every criterion passed in every one.
result_table() {
    local run=$1 want=$2 i files=""
    i=1; while [ "$i" -le "$want" ]; do files="$files $run/cycle-$i/results.tsv"; i=$((i + 1)); done
    awk -F '\t' -v want="$want" -v files="$files" '
        BEGIN {
            nf = split(files, fl, " ")
            for (c = 1; c <= nf; c++) {
                if ((getline line < fl[c]) <= 0) continue
                ran++
                do {
                    split(line, p, "\t")
                    if (!(p[1] in seen)) { seen[p[1]] = 1; order[++nr] = p[1]; if (length(p[1]) > wn) wn = length(p[1]) }
                    v[p[1], c] = p[2]
                    if (p[2] != "PASS") { bad[p[1]] = 1; det[++nd] = sprintf("  cycle %d %s: %s", c, p[1], p[3]) }
                } while ((getline line < fl[c]) > 0)
                close(fl[c])
            }
            if (wn < 9) wn = 9
            printf "%-" wn "s", "criterion"; for (c = 1; c <= want; c++) printf "  %-8s", "cycle " c; printf "\n"
            for (r = 1; r <= nr; r++) {
                printf "%-" wn "s", order[r]
                for (c = 1; c <= want; c++) printf "  %-8s", ((order[r], c) in v) ? v[order[r], c] : "-"
                printf "\n"
            }
            if (nd) { print "failures:"; for (d = 1; d <= nd; d++) print det[d] }
            names = ""; for (r = 1; r <= nr; r++) if (order[r] in bad) names = names " " order[r]
            if (ran == want && nr > 0 && names == "") { printf "OVERALL: PASS (%d/%d cycles, %d criteria)\n", ran, want, nr; exit 0 }
            printf "OVERALL: FAIL (%d/%d cycles ran)%s\n", ran, want, (names == "" ? "" : "; failing:" names)
            exit 1
        }'
}

# is_armed <boot-args>: true when navi48bringup is set to anything but 0 (bare = 1, as PE_parse_boot_argn).
is_armed() {
    local t v
    for t in $1; do
        [ "$(arg_name "$t")" = navi48bringup ] || continue
        case "$t" in *=*) v=${t#*=} ;; *) v=1 ;; esac
        case "$v" in 0|0x0|0X0|00) ;; *) return 0 ;; esac
    done
    return 1
}
