#!/usr/bin/env bash
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if ! command -v psp-config >/dev/null 2>&1; then
    export PSPDEV="${PSPDEV:-$root/.toolchains/installed/pspdev}"
    export PATH="$PSPDEV/bin:$PATH"
fi
if ! command -v psp-g++ >/dev/null 2>&1; then
    echo 'PSPSDK with psp-g++ is required. Set PSPDEV and add its bin directory to PATH.' >&2
    exit 1
fi
make -C "$root/woody/runtime" -j2 "$@"
package="$root/dist/PSP/GAME/WOODYPSP"
mkdir -p "$package"
if [[ -f "$root/woody/runtime/EBOOT.PBP" ]]; then
    cp "$root/woody/runtime/EBOOT.PBP" "$package/EBOOT.PBP"
fi
