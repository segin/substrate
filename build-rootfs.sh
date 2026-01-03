#!/bin/bash
# Build script to create root filesystem in dist/

set -e

TOP="$(cd "$(dirname "$0")" && pwd)"
DIST="$TOP/dist"

echo "Building TestUnix root filesystem..."

# Clean dist directory
rm -rf "$DIST"

# Create directory structure
mkdir -p "$DIST"/{bin,sbin,usr/{bin,lib,include},lib,dev,etc,proc,tmp,var,home,root}

# Build kernel
echo "Building kernel..."
make -C "$TOP/sys" -j4

# Copy kernel
cp "$TOP/sys/kernel.bin" "$DIST/boot/"
cp "$TOP/sys/kernel.multiboot" "$DIST/boot/"

# Build libc
echo "Building libc..."
make -C "$TOP/lib/c" -j4

# Install libc
cp "$TOP/lib/c/libc.a" "$DIST/usr/lib/"
mkdir -p "$DIST/usr/include"
cp -r "$TOP/lib/c/include"/* "$DIST/usr/include/"

# Build and install userland binaries
echo "Building userland..."
make -C "$TOP/bin" -j4

# Install binaries to dist/bin
for dir in "$TOP/bin"/*/ ; do
    if [ -d "$dir" ]; then
        name=$(basename "$dir")
        if [ -f "$dir/$name" ]; then
            cp "$dir/$name" "$DIST/bin/"
        fi
    fi
done

# Create essential device nodes (placeholders - real system would use devfs)
echo "Creating device nodes..."
mkdir -p "$DIST/dev"
# These would normally be created by the kernel/devfs

# Create basic etc files
echo "root:x:0:0:root:/root:/bin/sh" > "$DIST/etc/passwd"
echo "root:x:0:" > "$DIST/etc/group"

cat > "$DIST/etc/fstab" << 'EOF'
# <device>       <mount>  <type>  <options>  <dump> <pass>
/dev/storage/hda1  /        ext2    rw         0      1
EOF

# Create init script
cat > "$DIST/sbin/init" << 'EOF'
#!/bin/sh
# System initialization script

echo "TestUnix initializing..."

# Mount filesystems
mount -t proc proc /proc

# Start getty on console
/sbin/getty /dev/console

exec /bin/sh
EOF

chmod +x "$DIST/sbin/init"

echo ""
echo "Root filesystem created in: $DIST"
echo "Contents:"
du -sh "$DIST"
