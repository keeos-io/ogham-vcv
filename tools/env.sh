# -----------------------------------------------------------------------------
# Ogham for VCV Rack — build environment
#
# SPDX-FileCopyrightText: 2026 Steven Collins <https://keeos.io>
# SPDX-License-Identifier: MIT
#
#   source tools/env.sh
#
# Puts the MinGW-w64 toolchain on PATH and points RACK_DIR at the Rack SDK.
# Both are outside the repo, and both are chosen to match what the installed
# Rack was itself built with:
#
#   Rack 2.6.6 ships libgcc_s_seh-1.dll, libstdc++-6.dll and libwinpthread-1.dll
#   and no UCRT DLLs, i.e. mingw-w64 with POSIX threads on the MSVCRT runtime.
#   The toolchain below is x86_64-w64-mingw32, msvcrt-posix-seh, GCC 14.2.0 —
#   the same profile, and the same GCC major version as the firmware's
#   arm-none-eabi 14.2.
#
# Override either by exporting it before sourcing.
# -----------------------------------------------------------------------------

_winlibs="/c/Users/steve/AppData/Local/Microsoft/WinGet/Packages/BrechtSanders.WinLibs.POSIX.MSVCRT.LLVM_Microsoft.Winget.Source_8wekyb3d8bbwe/mingw64/bin"

if [ -d "$_winlibs" ]; then
    case ":$PATH:" in
        *":$_winlibs:"*) ;;
        *) PATH="$_winlibs:$PATH" ;;
    esac
    export PATH
else
    echo "tools/env.sh: MinGW toolchain not found at $_winlibs" >&2
fi

export RACK_DIR="${RACK_DIR:-/d/VCV/sdk/Rack-SDK}"

if [ ! -f "$RACK_DIR/plugin.mk" ]; then
    echo "tools/env.sh: RACK_DIR=$RACK_DIR does not look like a Rack SDK" >&2
fi

unset _winlibs
