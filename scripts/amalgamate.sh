#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "$0")/.." && pwd)
out=${1:-$root/dist/hid-parser/hid-parser.h}
mkdir -p "$(dirname "$out")"

{
    cat "$root/include/hid-parser/hid-parser.h"
    echo
    echo "#if defined(HIDP_IMPLEMENTATION) && !defined(HIDP_IMPLEMENTATION_DONE)"
    echo "#define HIDP_IMPLEMENTATION_DONE"
    for src in "$root"/src/*.h "$root"/src/*.c; do
        [ -f "$src" ] && grep -v '^#include "' "$src"
    done
    echo "#endif"
} > "$out"
echo "$out"
