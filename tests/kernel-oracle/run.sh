#!/bin/sh
set -eu
here=$(cd "$(dirname "$0")" && pwd)
hiddump=${1:-$here/../../build/hiddump}
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
oracle() {
    python3 "$here/../../scripts/kernel-oracle.py" --hiddump "$hiddump" --hid-debug "$here/usage-table.c" "$@"
}
expect_diff() {
    if oracle "$work/case.rdesc" > "$work/out"; then
        echo "FAIL: no difference for $1"; exit 1
    fi
    grep -q "$2" "$work/out" || { echo "FAIL: $1"; cat "$work/out"; exit 1; }
}
mutate() {
    sed "$2" "$here/$1" > "$work/case.rdesc"
}
oracle "$here/mouse.rdesc" "$here/ids.rdesc"
mutate mouse.rdesc 's/Report Offset(16)/Report Offset(17)/'
expect_diff offset "offset=17: only in kernel"
mutate mouse.rdesc 's/GenericDesktop.Y/GenericDesktop.Z/'
expect_diff usage "usage\[1\] kernel=GenericDesktop.Z hid-parser=GenericDesktop.Y"
mutate mouse.rdesc 's/Usage(5)/Usage(4)/; 0,/        ff00.00f1/{/        ff00.00f1/d}'
expect_diff repeat "kernel has 4 usages, hid-parser 5"
mutate mouse.rdesc 's/Logical Maximum(255)/Logical Maximum(127)/'
expect_diff logical "kernel=Logical Maximum(127) hid-parser=Logical Maximum(255)"
mutate mouse.rdesc 's/Flags( Array Absolute )/Flags( Constant Array Absolute )/'
expect_diff flags "kernel=Flags( Constant Array Absolute ) hid-parser=Flags( Array Absolute )"
mutate ids.rdesc 's/Unit(SI Linear : Centimeter)/Unit(SI Linear : Centimeter^2)/'
expect_diff unit "Centimeter^2"
mutate ids.rdesc '/Unit Exponent(-2)/d'
expect_diff exponent "kernel=- hid-parser=Unit Exponent(-2)"
mutate ids.rdesc 's/  OUTPUT(2)/  OUTPUT(3)/'
expect_diff id "O id=2 offset=0: only in hid-parser"
echo "kernel oracle: all tests passed"
