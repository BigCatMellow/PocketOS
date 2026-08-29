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

printf '%s\n' "$SECONDS" > "$SDROOT/.tmp_update/pocketos_stress_test_seconds"

echo "Stress test queued for $MINUTES minute(s)."
echo "Press MENU once to close Terminal. PocketOS will then start the test."
echo "It cycles launcher screens, fonts, and theme previews without launching games"
echo "or saving appearance changes. Do not press buttons until PocketOS returns to Onion."
