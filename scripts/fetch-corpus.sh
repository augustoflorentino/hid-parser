#!/usr/bin/env bash
set -euo pipefail

out=${1:-build/corpus}
kernel=https://raw.githubusercontent.com/torvalds/linux/v6.12/tools/testing/selftests/hid/tests
hidtools=https://gitlab.freedesktop.org/libevdev/hid-tools/-/raw/0.10/tests
root=$(cd "$(dirname "$0")/.." && pwd)
src=$(mktemp -d)
trap 'rm -rf "$src"' EXIT

for f in base_gamepad descriptors_wacom test_apple_keyboard test_gamepad test_hid_core \
    test_ite_keyboard test_keyboard test_mouse test_multitouch test_sony test_tablet \
    test_usb_crash test_wacom_generic; do
    curl -fsSL --retry 5 --retry-delay 10 "$kernel/$f.py" -o "$src/$f.py"
done
curl -fsSL --retry 5 --retry-delay 10 "$hidtools/test_hid.py" -o "$src/test_hid.py"
mkdir -p "$out"
python3 -I "$root/tests/oracle/extract.py" "$out" "$src"/*.py
ls "$out" | wc -l
