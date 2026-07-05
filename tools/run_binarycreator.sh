#!/usr/bin/env bash
# Bash-side helper for tools\deploy.bat
# Invokes binarycreator through bash (which works correctly here) instead of
# cmd.exe (which silently no-ops it on this build box).
#
# All 4 args are absolute Windows paths (backslashes). We convert them to
# MSYS-style paths so bash + binarycreator can find them.
set -e
WIN_BC="$1"
WIN_CFG="$2"
WIN_PKG="$3"
WIN_OUT="$4"
# cygpath -u converts a Windows path like `C:\Qt\Tools\...` to `/c/Qt/Tools/...`
BC=$(cygpath -u "$WIN_BC")
CFG=$(cygpath -u "$WIN_CFG")
PKG=$(cygpath -u "$WIN_PKG")
OUT=$(cygpath -u "$WIN_OUT")
"$BC" --offline-only -c "$CFG" -p "$PKG" "$OUT"
