#!/bin/bash
# Syntax-check all plugin sources inside WSL Debian (catches compile-level breakage).
#
# Usage from Windows shells:
#   Git Bash :  MSYS_NO_PATHCONV=1 wsl.exe -e bash -c "cd /mnt/d/Desktop/bvh && bash tools/check_syntax.sh"
#   cmd/pwsh :  tools\check_syntax.cmd
set -u

cd "$(dirname "$0")/.." || exit 1

FILES="src/config/projectile_class_config.cpp src/engine/bdsc_api.cpp src/engine/model_provider.cpp src/physics/collision_world.cpp src/runtime/debug_log.cpp src/runtime/perf_stats.cpp src/runtime/projectile_gate.cpp src/plugin/h_export.cpp src/plugin/meta_plugin.cpp src/plugin/game_hooks.cpp"
INC="-Isrc -Imetamod/hlsdk/common -Imetamod/hlsdk/dlls -Imetamod/hlsdk/pm_shared -Imetamod/hlsdk/engine -Imetamod/metamod -Imetamod/thirdparty/bullet3_fork/src"
# osdep.h switches on the lowercase predefined `linux`, which strict -std=c++17
# does not define; the real CMake build passes the same spellings.
DEFINES="-DLINUX -D_LINUX -Dlinux"

FAILED=0
LOG="$(mktemp)"
trap 'rm -f "$LOG"' EXIT

for f in $FILES; do
    echo "===== $f"
    if ! g++ -std=c++17 -fsyntax-only -Wall -Wextra -Wpedantic -Wno-deprecated-copy $INC $DEFINES "$f" 2>"$LOG"; then
        FAILED=1
        head -24 "$LOG"
    else
        grep -m 6 "warning" "$LOG" || true
    fi
done

if [ "$FAILED" -ne 0 ]; then
    echo "SYNTAX CHECK FAILED"
else
    echo "SYNTAX CHECK OK"
fi
exit "$FAILED"
