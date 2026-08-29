#!/bin/sh
# Run from Onion Terminal: sh /mnt/SDCARD/pocketos-stress-test.sh [minutes]

SDROOT=/mnt/SDCARD
MINUTES=${1:-30}

case "$MINUTES" in
    ''|*[!0-9]*) echo "Usage: sh pocketos-stress-test.sh [whole minutes]"; exit 2 ;;
esac
if [ "$MINUTES" -lt 1 ] || [ "$MINUTES" -gt 180 ]; then
    echo "Choose a duration from 1 to 180 minutes."
    exit 2
fi

SECONDS=$((MINUTES * 60))
mkdir -p "$SDROOT/.tmp_update/logs"
touch "$SDROOT/.tmp_update/logs/pocketos_health.csv"

echo "PocketOS stress test: $MINUTES minute(s)"
echo "It cycles launcher screens, fonts, and theme previews."
echo "It will not launch games or save appearance changes."

POCKETOS_STRESS_TEST=1 POCKETOS_STRESS_TEST_SECONDS="$SECONDS" \
    "$SDROOT/.tmp_update/bin/pocketOS"

echo "Finished. Copy .tmp_update/logs/pocketos_health.csv to your computer"
echo "and run pocketos-health-report.py against the SD card."
