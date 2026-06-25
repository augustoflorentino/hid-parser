#!/usr/bin/env bash
set -euo pipefail

kernel=${KERNEL:-$(ls -d /usr/lib/modules/*-arch* | head -1)}
batch=${BATCH:-12}
out=${1:?usage: run-vm.sh outdir descriptor...}
shift
here=$(cd "$(dirname "$0")" && pwd)
work=$(mktemp -d "${TMPDIR:-$PWD}/vm.XXXXXX")
trap 'rm -rf "$work"' EXIT

mkdir -p "$out"
cc -static -O2 -o "$work/init" "$here/init.c"

run_batch() {
    local root=$work/root
    rm -rf "$root" && mkdir -p "$root/dev" "$root/proc" "$root/sys" "$root/descs"
    cp "$work/init" "$root/init"
    cp "$(find "$kernel" -name 'uhid.ko*' | head -1)" "$root/uhid.ko.zst"
    for f in "$@"; do
        cp "$f" "$root/descs/$(basename "${f%.*}")"
    done
    (cd "$root" && find . | cpio -o -H newc --quiet) > "$work/initramfs.cpio"
    : > "$work/serial.txt"
    timeout 300 qemu-system-x86_64 -enable-kvm -m 512 -nographic -no-reboot \
        -kernel "$kernel/vmlinuz" -initrd "$work/initramfs.cpio" \
        -append "console=ttyS0 quiet loglevel=0 panic=-1" \
        -serial null -serial "file:$work/serial.txt" -monitor none >/dev/null \
        || echo "vm timed out" >&2
    awk -v out="$out" '
        { sub(/\r$/, "") }
        /^@@ begin / { name = $3; file = out "/" name ".rdesc"; printf "" > file; next }
        /^@@ end / { close(file); file = ""; next }
        /^@@ done / { print "devices: " $3 }
        /^!! / { print }
        file != "" { print >> file }
    ' "$work/serial.txt"
}

files=("$@")
for ((i = 0; i < ${#files[@]}; i += batch)); do
    run_batch "${files[@]:i:batch}"
done
