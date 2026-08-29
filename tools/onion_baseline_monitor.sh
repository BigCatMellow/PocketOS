#!/bin/sh
# Run from Onion Terminal:
#   sh /mnt/SDCARD/onion-baseline-monitor.sh start 60
#   sh /mnt/SDCARD/onion-baseline-monitor.sh status
#   sh /mnt/SDCARD/onion-baseline-monitor.sh stop

SDROOT=/mnt/SDCARD
LOGDIR="$SDROOT/.tmp_update/logs"
LOG="$LOGDIR/onion_baseline_health.csv"
PIDFILE="$LOGDIR/onion_baseline_monitor.pid"
MAX_BYTES=$((512 * 1024))

find_mainui_pid() {
    for proc in /proc/[0-9]*; do
        name=$(cat "$proc/comm" 2>/dev/null)
        case "$name" in
            MainUI|mainui) echo "${proc#/proc/}"; return 0 ;;
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
    value=$(awk '/^MemAvailable:/ { print $2; found=1; exit } END { if (!found) print -1 }' /proc/meminfo)
    echo "$value"
}

sample() {
    pid=$(find_mainui_pid)
    if [ -n "$pid" ]; then
        rss=$(awk '/^VmRSS:/ { print $2; exit }' "/proc/$pid/status" 2>/dev/null)
        [ -n "$rss" ] || rss=-1
    else
        rss=-1
    fi
    governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null)
    [ -n "$governor" ] || governor=unknown
    printf '%s,minute,MainUI,%s,%s,%s,%s\n' "$(date +%s)" "$rss" \
        "$(read_mem_available)" "$(read_battery)" "$governor" >> "$LOG"
}

start() {
    minutes=$1
    case "$minutes" in
        ''|*[!0-9]*) echo "Usage: start [whole minutes]"; exit 2 ;;
    esac
    if [ "$minutes" -lt 1 ] || [ "$minutes" -gt 360 ]; then
        echo "Choose a duration from 1 to 360 minutes."
        exit 2
    fi
    mkdir -p "$LOGDIR"
    if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
        echo "A baseline monitor is already running."
        exit 1
    fi
    if [ ! -f "$LOG" ] || [ "$(wc -c < "$LOG")" -gt "$MAX_BYTES" ]; then
        [ -f "$LOG" ] && mv "$LOG" "$LOG.prev"
        echo "timestamp,event,launcher,launcher_rss_kb,mem_available_kb,battery_percent,cpu_governor" > "$LOG"
    fi
    (
        end=$(( $(date +%s) + minutes * 60 ))
        while [ "$(date +%s)" -lt "$end" ]; do
            sample
            sleep 60
        done
        rm -f "$PIDFILE"
    ) &
    echo "$!" > "$PIDFILE"
    echo "Baseline monitor started for $minutes minute(s)."
    echo "Exit Terminal to return to Onion MainUI; the monitor keeps running."
}

case "$1" in
    start) start "$2" ;;
    status)
        if [ -f "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; then
            echo "Baseline monitor is running (PID $(cat "$PIDFILE"))."
        else
            echo "Baseline monitor is not running."
        fi
        ;;
    stop)
        if [ -f "$PIDFILE" ]; then
            kill "$(cat "$PIDFILE")" 2>/dev/null
            rm -f "$PIDFILE"
        fi
        echo "Baseline monitor stopped."
        ;;
    *) echo "Usage: $0 {start minutes|status|stop}"; exit 2 ;;
esac
