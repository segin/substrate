#!/bin/bash
# Build script to create root filesystem and images

set -e

TOP="$(cd "$(dirname "$0")" && pwd)"
DIST="$TOP/dist"
IMAGE="$TOP/rootfs.img"
IMAGE_SIZE_MIB=100
BOOT_DIR="$TOP/sys/boot"
EXT2BOOT_DIR="$TOP/contrib/ext2-boot"

usage() {
    echo "Usage: $0 [options]"
    echo ""
    echo "Options:"
    echo "  --dist     Prepare the dist/ directory with all distribution files"
    echo "  --image    Create a ${IMAGE_SIZE_MIB}MiB ext2 filesystem image (rootfs.img)"
    echo "  --no-boot  Skip ext2-boot bootloader installation"
    echo "  --help     Show this help message"
    echo ""
    exit 1
}

clean_dist() {
    echo "Cleaning dist directory..."
    rm -rf "$DIST"
    mkdir -p "$DIST"/{bin,sbin,usr/{bin,lib,include},lib,dev,etc,proc,tmp,var,home,root,boot}
}

build_ext2boot() {
    if [ ! -d "$EXT2BOOT_DIR" ]; then
        echo "Warning: contrib/ext2-boot not found (run: git submodule update --init)"
        return 1
    fi
    echo "Building ext2-boot bootloader..."
    make -C "$EXT2BOOT_DIR" -f Makefile.substrate
}

build_bootloader() {
    echo "Building Substrate bootloader..."
    make -C "$BOOT_DIR"
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
    echo "Installing kernel to dist/boot and dist/vmunix..."
    cp "$TOP/sys/kernel.bin" "$DIST/boot/"
    cp "$TOP/sys/kernel.multiboot" "$DIST/boot/"
    # Install kernel as /vmunix for the ext2 bootloader
    cp "$TOP/sys/kernel.multiboot" "$DIST/vmunix"

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

    # Create ext2 filesystem with 1024-byte blocks and 128-byte inodes
    # (required by ext2-boot bootloader)
    mkfs.ext2 -F -b 1024 -I 128 -O ^resize_inode -d "$DIST" "$IMAGE"

    echo "Image created: $IMAGE"
}

install_bootloader() {
    local stage1="$BOOT_DIR/stage1.bin"
    local stage2="$BOOT_DIR/stage2.bin"

    if [ ! -f "$stage1" ] || [ ! -f "$stage2" ]; then
        build_bootloader
    fi

    echo "Installing Substrate bootloader into $IMAGE..."
    python3 "$TOP/tools/ext2-install-boot" "$IMAGE" "$stage1" "$stage2"
}

# Parse arguments
if [ $# -eq 0 ]; then
    usage
fi

DO_DIST=false
DO_IMAGE=false
DO_BOOT=true

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
        --no-boot)
            DO_BOOT=false
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
    if [ "$DO_BOOT" = true ]; then
        install_bootloader
    fi
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
