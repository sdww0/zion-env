#!/bin/sh

mount -t proc proc /proc 2>/dev/null || true
mount -t sysfs sysfs /sys 2>/dev/null || true
mkdir -p /tmp
chmod 1777 /tmp
printf '[ZION SQLITE] starting sqlite-speedtest1 --size 10 --memdb\n'
/benchmark/bin/sqlite-speedtest1 --size 10 --memdb
rc=$?
printf '[ZION SQLITE] sqlite-speedtest1 exit=%s\n' "$rc"
if [ "$rc" -eq 0 ]; then
    printf '[ZION SQLITE] PASS: speedtest1 completed\n'
else
    printf '[ZION SQLITE] FAIL: speedtest1 failed\n'
fi
# Keep PID 1 alive so the result remains available in the serial log.
while :; do sleep 3600; done
