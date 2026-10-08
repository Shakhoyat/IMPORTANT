#!/bin/sh
# Kept for old notes: the Windows cross build now lives in tools/build_windows.sh (incremental, build/obj-win).
exec "$(dirname "$0")/build_windows.sh" "$@"
