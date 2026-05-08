#!/bin/bash

set -e

ROOTFS="sandbox_root"

rm -rf "$ROOTFS"

mkdir -p "$ROOTFS/bin"
mkdir -p "$ROOTFS/lib"
mkdir -p "$ROOTFS/lib64"
mkdir -p "$ROOTFS/lib/x86_64-linux-gnu"

cp /bin/bash "$ROOTFS/bin/"
cp /bin/ls "$ROOTFS/bin/" || true
cp /bin/cat "$ROOTFS/bin/" || true
cp /bin/hostname "$ROOTFS/bin/" || true
cp /bin/ps "$ROOTFS/bin/" || true

copy_libs() {
    BIN="$1"

    ldd "$BIN" | awk '{print $3}' | grep '^/' | while read -r lib; do
        dest="$ROOTFS$(dirname "$lib")"
        mkdir -p "$dest"
        cp "$lib" "$dest/"
    done
}

copy_libs /bin/bash
copy_libs /bin/ls || true
copy_libs /bin/cat || true
copy_libs /bin/hostname || true
copy_libs /bin/ps || true

if [ -f /lib64/ld-linux-x86-64.so.2 ]; then
    cp /lib64/ld-linux-x86-64.so.2 "$ROOTFS/lib64/"
fi

echo "Rootfs created in $ROOTFS"
