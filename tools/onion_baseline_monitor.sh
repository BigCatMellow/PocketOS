#!/bin/sh
# Compatibility command for the original Onion-only baseline monitor.
# Collection and lifecycle behavior are owned by launcher-comparison-monitor.sh.
# Run from Onion Terminal:
#   sh /mnt/SDCARD/onion-baseline-monitor.sh start 60
#   sh /mnt/SDCARD/onion-baseline-monitor.sh status
#   sh /mnt/SDCARD/onion-baseline-monitor.sh stop

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
MONITOR="$SCRIPT_DIR/launcher-comparison-monitor.sh"

case "$1" in
    start) exec sh "$MONITOR" start onion "$2" ;;
    status|stop) exec sh "$MONITOR" "$1" ;;
    *) echo "Usage: $0 {start minutes|status|stop}"; exit 2 ;;
esac
