#!/bin/sh
# Paired, manual launcher comparison for PocketOS and stock Onion.
# Run from Onion Terminal:
#   sh /mnt/SDCARD/launcher-comparison-monitor.sh start pocketos 30
#   sh /mnt/SDCARD/launcher-comparison-monitor.sh start onion 30
#   sh /mnt/SDCARD/launcher-comparison-monitor.sh status
#   sh /mnt/SDCARD/launcher-comparison-monitor.sh stop

SDROOT=/mnt/SDCARD
LOGDIR="$SDROOT/.tmp_update/logs"
PIDFILE="$LOGDIR/launcher_comparison_monitor.pid"
MAX_BYTES=$((512 * 1024))
KEEP_AWAKE=/tmp/stay_awake

usage() {
    echo "Usage: $0 {start pocketos|onion minutes|status|stop}"
    exit 2
}

find_launcher_pid() {
    target=$1
    for proc in /proc/[0-9]*; do
        name=$(cat "$proc/comm" 2>/dev/null)
        case "$target:$name" in
            pocketos:pocketOS|pocketos:pocketos|onion:MainUI|onion:mainui)
                echo "${proc#/proc/}"
                return 0
                ;;
        esac
    done
    return 1
}

read_battery() {
    for path in /sys/class/power_supply/axp20x-battery/capacity \
                /sys/class/power_supply/battery/capacity; do
        if [ -r "$path" ]; then cat "$path"; return; fi
    done
    echo -1
}

read_mem_available() {
    awk '/^MemAvailable:/ { print $2; found=1; exit } END { if (!found) print -1 }' /proc/meminfo
}

sample() {
    target=$1
    log=$2
    pid=$(find_launcher_pid "$target")
    if [ -n "$pid" ]; then
        rss=$(awk '/^VmRSS:/ { print $2; exit }' "/proc/$pid/status" 2>/dev/null)
        [ -n "$rss" ] || rss=-1
    else
        rss=-1
    fi
    governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)
    [ -n "$governor" ] || governor=unknown
    printf '%s,minute,%s,%s,%s,%s,%s\n' "$(date +%s)" "$target" "$rss" \
        "$(read_mem_available)" "$(read_battery)" "$governor" >> "$log"
}

start() {
    target=$1
    minutes=$2
    case "$target" in pocketos|onion) ;; *) usage ;; esac
    case "$minutes" in ''|*[!0-9]*) echo "Usage: start {pocketos|onion} [whole minutes]"; exit 2 ;; esac
    if [ "$minutes" -lt 1 ] || [ "$minutes" -gt 360 ]; then
        echo "Choose a duration from 1 to 360 minutes."
        exit 2
    fi
    mkdir -p "$LOGDIR"
    if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
        echo "A comparison monitor is already running. Stop it before starting another run."
        exit 1
    fi
    case "$target" in
        pocketos) log="$LOGDIR/pocketos_comparison_health.csv" ;;
        onion)    log="$LOGDIR/onion_comparison_health.csv" ;;
    esac
    if [ ! -f "$log" ] || [ "$(wc -c < "$log")" -gt "$MAX_BYTES" ]; then
        [ -f "$log" ] && mv "$log" "$log.prev"
        echo "timestamp,event,launcher,launcher_rss_kb,mem_available_kb,battery_percent,cpu_governor" > "$log"
    fi
    : > "$KEEP_AWAKE"
    (
        cleanup() { rm -f "$PIDFILE" "$KEEP_AWAKE"; }
        trap cleanup 0
        trap 'exit 0' INT TERM
        end=$(( $(date +%s) + minutes * 60 ))
        while [ "$(date +%s)" -lt "$end" ]; do
            sample "$target" "$log"
            sleep 60
        done
    ) &
    echo "$!" > "$PIDFILE"
    echo "Paired $target comparison started for $minutes minute(s)."
    echo "Exit Terminal, open $([ "$target" = pocketos ] && echo PocketOS || echo Onion MainUI), and follow the same comparison routine."
}

case "$1" in
    start) start "$2" "$3" ;;
    status)
        if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
            echo "Comparison monitor is running (PID $(cat "$PIDFILE"))."
        else
            echo "Comparison monitor is not running."
        fi
        ;;
    stop)
        if [ -f "$PIDFILE" ]; then kill "$(cat "$PIDFILE")" 2>/dev/null; rm -f "$PIDFILE"; fi
        rm -f "$KEEP_AWAKE"
        echo "Comparison monitor stopped."
        ;;
    *) usage ;;
esac
