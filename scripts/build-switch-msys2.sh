#!/usr/bin/env bash
set -euo pipefail

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITA64="${DEVKITA64:-$DEVKITPRO/devkitA64}"
export PATH="/mingw64/bin:$DEVKITPRO/tools/bin:$DEVKITA64/bin:$DEVKITPRO/portlibs/switch/bin:$PATH"

cd "$(dirname "$0")/.."

# Unix Makefiles, not Ninja: extern/libpeer's CMakeLists.txt builds cjson/
# mbedtls/srtp2/usrsctp via nested ExternalProject_Add, each of which spawns
# its OWN nested cmake configure/build invocation. Ninja-inside-Ninja
# recursion is known to be fragile (mangled stamp/log filenames, e.g. a
# literal "-GNinja" turning into a duplicate ninja rule); Make handles this
# nesting cleanly and is what this vendored CMakeLists.txt was almost
# certainly authored/tested against.
NPROC="$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)"
cmake -B build/switch -G "Unix Makefiles"
cmake --build build/switch --target BoosterNX.nro -j"$NPROC"

printf '\nBuilt: %s\n' "$(pwd)/build/switch/BoosterNX.nro"
