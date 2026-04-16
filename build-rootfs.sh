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
    mkdir -p "$DIST"/{bin,sbin,usr/{bin,lib,include},lib,dev,etc,proc,tmp,var,var/empty,home,root,boot}
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
    cp -r "$TOP/etc/." "$DIST/etc/"
    cp "$TOP/etc/init.sh" "$DIST/sbin/init"
    chmod +x "$DIST/sbin/init"

    echo "Installing toolchain to dist/usr/bin..."
    mkdir -p "$DIST/usr/bin"
    # C compiler and preprocessor
    if [ -f "$TOP/usr.bin/cc/cc" ]; then
        cp "$TOP/usr.bin/cc/cc" "$DIST/usr/bin/"
        ln -sf cc "$DIST/usr/bin/cpp"
    fi
    # Assembler
    if [ -f "$TOP/usr.bin/as/as" ]; then
        cp "$TOP/usr.bin/as/as" "$DIST/usr/bin/"
    fi
    # Linker
    if [ -f "$TOP/usr.bin/ld/ld" ]; then
        cp "$TOP/usr.bin/ld/ld" "$DIST/usr/bin/"
    fi
    # Archive tools
    for tool in ar ranlib nm objdump objcopy readelf strip strings size addr2line elfedit; do
        if [ -f "$TOP/usr.bin/$tool/$tool" ]; then
            cp "$TOP/usr.bin/$tool/$tool" "$DIST/usr/bin/"
        fi
    done
    # CC resource directory (SIMD headers)
    if [ -d "$TOP/usr.bin/cc/resource" ]; then
        mkdir -p "$DIST/usr/bin/resource"
        cp -r "$TOP/usr.bin/cc/resource/"* "$DIST/usr/bin/resource/"
    fi

    echo "Installing usr.sbin binaries..."
    mkdir -p "$DIST/usr/sbin"
    for dir in "$TOP/usr.sbin"/*/ ; do
        if [ -d "$dir" ]; then
            name=$(basename "$dir")
            if [ -f "$dir/$name" ]; then
                cp "$dir/$name" "$DIST/usr/sbin/"
            fi
        fi
    done

    echo "Installing sbin binaries..."
    for dir in "$TOP/sbin"/*/ ; do
        if [ -d "$dir" ]; then
            name=$(basename "$dir")
            if [ -f "$dir/$name" ]; then
                cp "$dir/$name" "$DIST/sbin/"
            fi
        fi
    done

    echo "Installing libraries to dist/usr/lib..."
    # crt0 and libsys needed for linking
    [ -f "$TOP/lib/c/crt0.o" ] && cp "$TOP/lib/c/crt0.o" "$DIST/usr/lib/"
    [ -f "$TOP/lib/sys/libsys.a" ] && cp "$TOP/lib/sys/libsys.a" "$DIST/usr/lib/"
    [ -f "$TOP/lib/m/libm.a" ] && cp "$TOP/lib/m/libm.a" "$DIST/usr/lib/"
    # elfobj library (needed by ld, as)
    [ -f "$TOP/usr.lib/elfobj/libelfobj.a" ] && cp "$TOP/usr.lib/elfobj/libelfobj.a" "$DIST/usr/lib/"

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

    # 1. Create empty ext2 filesystem (no -d population yet)
    mkfs.ext2 -F -b 1024 -I 128 -O ^resize_inode "$IMAGE"

    # 2. Install bootloader into pristine filesystem (gets group 0 blocks)
    if [ "$DO_BOOT" = true ]; then
        install_bootloader
    fi

    # 3. Populate filesystem from dist/ via debugfs
    echo "Populating image from $DIST..."
    local cmdfile
    cmdfile=$(mktemp)

    # Create directories first (sorted so parents come before children)
    (cd "$DIST" && find . -mindepth 1 -type d | sort) | while IFS= read -r d; do
        rel="${d#./}"
        echo "mkdir $rel"
    done >> "$cmdfile"

    # Write regular files
    (cd "$DIST" && find . -mindepth 1 -type f) | while IFS= read -r f; do
        rel="${f#./}"
        echo "write $DIST/$rel $rel"
    done >> "$cmdfile"

    # Create symlinks
    (cd "$DIST" && find . -mindepth 1 -type l) | while IFS= read -r l; do
        rel="${l#./}"
        target=$(readlink "$DIST/$rel")
        echo "symlink $rel $target"
    done >> "$cmdfile"

    debugfs -w -f "$cmdfile" "$IMAGE" > /dev/null 2>&1
    rm -f "$cmdfile"

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
    # Build bootloader first so it's ready for image creation
    if [ "$DO_BOOT" = true ]; then
        build_bootloader
    fi
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
