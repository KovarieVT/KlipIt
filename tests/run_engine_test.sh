#!/usr/bin/env bash
# Runs the capture-engine test on Linux under a virtual X display.
# Usage: tests/run_engine_test.sh <build-dir>
BUILD="${1:-build}"
export LIBGL_ALWAYS_SOFTWARE=1
Xvfb :97 -screen 0 1280x800x24 >/dev/null 2>&1 & XV=$!
sleep 1
export DISPLAY=:97 QT_QPA_PLATFORM=xcb
"$BUILD/tests/klipit_engine_test" -platform xcb
RC=$?
kill $XV 2>/dev/null
exit $RC
