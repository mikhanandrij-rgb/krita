#!/bin/bash
# SPDX-FileCopyrightText: 2026 Krita Mobile (unofficial fork) contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Krita Mobile (unofficial fork): runs the installed APK in the Android
# emulator in one interface mode and collects screenshots, the test driver's
# log, startup time and memory use. The test driver is switched on by files in
# the app's external files directory (see config::testSetting).
#
# Usage: emulator-run.sh <classic|phone> <output dir> <timeout seconds> [walk]
set -u
mode="$1"; out="$2"; limit="$3"; walk="${4:-}"
pkg=org.krita.mobilefork
activity="$pkg/org.krita.android.MainActivity"
ctl="/sdcard/Android/data/$pkg/files/krita-mobile-test"
mkdir -p "$out"

adb shell am force-stop "$pkg"
adb shell rm -rf "$ctl"
adb shell mkdir -p "$ctl"
adb shell "echo 1 > $ctl/KRITA_MOBILE_SCREENSHOTS"
adb shell "echo $mode > $ctl/KRITA_MOBILE_UI"
[ -n "$walk" ] && adb shell "echo 1 > $ctl/KRITA_MOBILE_ACTION_WALK"
adb logcat -c

# "am start -W" waits for the first frame of the activity.
adb shell am start -W -n "$activity" > "$out/am-start.txt" 2>&1
cat "$out/am-start.txt"

start=$(date +%s)
status=timeout
while [ $(( $(date +%s) - start )) -lt "$limit" ]; do
    sleep 10
    if adb shell "grep -q '^done' $ctl/out/testdriver.log 2>/dev/null"; then
        status=done
        break
    fi
    if [ -z "$(adb shell pidof "$pkg" | tr -d '\r')" ]; then
        status=died
        break
    fi
    # Memory while running (PSS in KiB) for the log.
    adb shell dumpsys meminfo "$pkg" 2>/dev/null | grep -m1 -E 'TOTAL( PSS)?:' >> "$out/meminfo-samples.txt"
done
echo "run $mode: $status after $(( $(date +%s) - start )) s" | tee "$out/result.txt"

adb shell dumpsys meminfo "$pkg" > "$out/meminfo-final.txt" 2>&1
adb pull "$ctl/out/." "$out/" > /dev/null 2>&1
adb logcat -d -b main -b crash > "$out/logcat.txt" 2>&1
adb logcat -d -b crash > "$out/logcat-crash.txt" 2>&1
adb shell am force-stop "$pkg"
ls -la "$out"
