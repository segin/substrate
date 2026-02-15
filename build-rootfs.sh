#!/bin/bash
# Build script to create root filesystem and images

set -e

TOP="$(cd "$(dirname "$0")" && pwd)"
DIST="$TOP/dist"
IMAGE="$TOP/rootfs.img"
IMAGE_SIZE_MIB=100

usage() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --dist     Prepare the dist/ directory with all distribution files"
    echo "  --image    Create a ${IMAGE_SIZE_MIB}MiB ext2 filesystem image (rootfs.img)"
    echo "  --help     Show this help message"
    echo ""
    exit 1
}

clean_dist() {
    echo "Cleaning dist directory..."
    rm -rf "$DIST"
    mkdir -p "$DIST"/{bin,sbin,usr/{bin,lib,include},lib,dev,etc,proc,tmp,var,home,root,boot}
}

build_components() {
    echo "Building kernel..."
    make -C "$TOP/sys" -j4

    echo "Building libc..."
    make -C "$TOP/lib/c" -j4

    echo "Building userland..."
    make -C "$TOP/bin" -j4
}

install_to_dist() {
    echo "Installing kernel to dist/boot..."
    cp "$TOP/sys/kernel.bin" "$DIST/boot/"
    cp "$TOP/sys/kernel.multiboot" "$DIST/boot/"

    echo "Installing libc to dist/usr/lib..."
    cp "$TOP/lib/c/libc.a" "$DIST/usr/lib/"
    mkdir -p "$DIST/usr/include"
    cp -r "$TOP/include/"* "$DIST/usr/include/"

    echo "Installing userland binaries to dist/bin..."
    for dir in "$TOP/bin"/*/ ; do
        if [ -d "$dir" ]; then
            name=$(basename "$dir")
            if [ -f "$dir/$name" ]; then
                cp "$dir/$name" "$DIST/bin/"
            fi
        fi
    done

    echo "Installing configuration from etc/..."
    cp "$TOP/etc/passwd" "$DIST/etc/"
    cp "$TOP/etc/group" "$DIST/etc/"
    cp "$TOP/etc/fstab" "$DIST/etc/"
    cp "$TOP/etc/init.sh" "$DIST/sbin/init"
    chmod +x "$DIST/sbin/init"

    echo "Root filesystem prepared in: $DIST"
}

create_image() {
    echo "Creating ${IMAGE_SIZE_MIB}MiB ext2 filesystem image: $IMAGE"
    
    # Check if dist directory exists and is not empty
    if [ ! -d "$DIST" ] || [ -z "$(ls -A "$DIST")" ]; then
        echo "Error: dist/ directory is empty. Run with --dist first."
        exit 1
    fi

    # Create empty sparse file
    rm -f "$IMAGE"
    truncate -s "${IMAGE_SIZE_MIB}M" "$IMAGE"

    # Create ext2 filesystem and populate with dist/ content
    # Using -d option to populate the image root-less
    mkfs.ext2 -F -d "$DIST" "$IMAGE"

    echo "Image created: $IMAGE"
}

# Parse arguments
if [ $# -eq 0 ]; then
    usage
fi

DO_DIST=false
DO_IMAGE=false

while [[ $# -gt 0 ]]; do
    case $1 in
        --dist)
            DO_DIST=true
            shift
            ;;
        --image)
            DO_IMAGE=true
            shift
            ;;
        --help)
            usage
            ;;
        *)
            echo "Unknown option: $1"
            usage
            ;;
    esac
done

if [ "$DO_DIST" = true ]; then
    clean_dist
    build_components
    install_to_dist
fi

if [ "$DO_IMAGE" = true ]; then
    create_image
fi

echo ""
if [ "$DO_DIST" = true ]; then
    echo "Dist contents:"
    du -sh "$DIST"
fi
if [ "$DO_IMAGE" = true ]; then
    echo "Image size:"
    ls -lh "$IMAGE"
fi
