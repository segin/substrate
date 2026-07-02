#!/usr/bin/env python3
import math
import shutil
import subprocess
import sys
from pathlib import Path

FLOPPY_SIZE = 1474560
SECTOR_SIZE = 512
FLOPPY_SECTORS = FLOPPY_SIZE // SECTOR_SIZE


def run(cmd):
    subprocess.run(cmd, check=True)


def assemble(nasm, source, output, defines):
    cmd = [nasm, '-f', 'bin']
    for key, value in defines.items():
        cmd.append(f'-D{key}={value}')
    cmd.extend([str(source), '-o', str(output)])
    run(cmd)


def read_setup_sectors(zimage):
    if len(zimage) < 0x1F2:
        raise RuntimeError('kernel.zimage is too small to contain setup_sects')
    setup_sects = zimage[0x1F1]
    if setup_sects == 0:
        return 4
    return setup_sects + 1


def main():
    if len(sys.argv) != 3:
        print(f'Usage: {sys.argv[0]} <kernel.zimage> <kernel.flp>', file=sys.stderr)
        return 1

    zimage_path = Path(sys.argv[1]).resolve()
    output_path = Path(sys.argv[2]).resolve()
    workdir = Path(__file__).resolve().parent
    stage1_src = workdir / 'stage1.asm'
    stage2_src = workdir / 'stage2.asm'
    stage1_bin = workdir / 'stage1.bin'
    stage2_bin = workdir / 'stage2.bin'
    nasm = shutil.which('nasm') or shutil.which('yasm')
    if nasm is None:
        raise RuntimeError('nasm or yasm is required to build kernel.flp')

    zimage = zimage_path.read_bytes()
    kernel_sectors = math.ceil(len(zimage) / SECTOR_SIZE)
    setup_sectors = read_setup_sectors(zimage)

    stage2_sectors = None
    while True:
        if stage2_sectors is None:
            stage2_lba = 1
        else:
            stage2_lba = 1
        kernel_lba = stage2_lba + (stage2_sectors or 1)
        assemble(
            nasm,
            stage2_src,
            stage2_bin,
            {
                'KERNEL_LBA': kernel_lba,
                'KERNEL_SECTORS': kernel_sectors,
                'KERNEL_SETUP_SECTORS': setup_sectors,
                'FLOPPY_DISK_SECTORS': FLOPPY_SECTORS,
            },
        )
        built_stage2 = stage2_bin.read_bytes()
        new_stage2_sectors = math.ceil(len(built_stage2) / SECTOR_SIZE)
        if new_stage2_sectors == stage2_sectors:
            break
        stage2_sectors = new_stage2_sectors

    kernel_lba = 1 + stage2_sectors
    total_used = kernel_lba + kernel_sectors

    assemble(
        nasm,
        stage1_src,
        stage1_bin,
        {
            'STAGE2_SEG': 0x0800,
            'STAGE2_SECTORS': stage2_sectors,
        },
    )

    stage1 = stage1_bin.read_bytes()
    stage2 = stage2_bin.read_bytes()
    if len(stage1) != SECTOR_SIZE:
        raise RuntimeError(f'stage1 size must be exactly 512 bytes, got {len(stage1)}')

    # Disk 1 always begins with stage1 (sector 0) + stage2 (sectors 1..) and
    # then as much of the kernel zimage as fits before the end of the disk.
    image = bytearray(FLOPPY_SIZE)
    image[0:SECTOR_SIZE] = stage1

    stage2_offset = SECTOR_SIZE
    image[stage2_offset:stage2_offset + len(stage2)] = stage2

    kernel_offset = kernel_lba * SECTOR_SIZE

    if total_used <= FLOPPY_SECTORS:
        # Single-disk: byte-for-byte the classic stage1|stage2|kernel layout.
        image[kernel_offset:kernel_offset + len(zimage)] = zimage
        output_path.write_bytes(image)
        print(f'Wrote {output_path}')
        print(f'  stage2 sectors: {stage2_sectors}')
        print(f'  kernel sectors: {kernel_sectors}')
        print(f'  kernel start LBA: {kernel_lba}')
        print(f'  setup sectors: {setup_sectors}')
        return 0

    # Multi-disk: the kernel does not fit on one floppy.  Disk 1 is filled to
    # its end with the leading kernel bytes; the remainder is written to raw
    # continuation disks that the loader reads from LBA 0 after each swap.
    first_disk_kernel_sectors = FLOPPY_SECTORS - kernel_lba
    first_disk_kernel_bytes = first_disk_kernel_sectors * SECTOR_SIZE
    image[kernel_offset:FLOPPY_SIZE] = zimage[:first_disk_kernel_bytes]
    output_path.write_bytes(image)

    continuation = zimage[first_disk_kernel_bytes:]
    cont_disks = []
    pos = 0
    disk_no = 2
    while pos < len(continuation):
        chunk = continuation[pos:pos + FLOPPY_SIZE]
        disk_img = bytearray(FLOPPY_SIZE)  # last disk stays zero-padded to full
        disk_img[0:len(chunk)] = chunk
        cont_path = output_path.with_name(output_path.name + f'.{disk_no}')
        cont_path.write_bytes(disk_img)
        cont_disks.append((disk_no, cont_path, len(chunk)))
        pos += FLOPPY_SIZE
        disk_no += 1

    total_disks = 1 + len(cont_disks)
    print(f'Wrote {output_path} (disk 1 of {total_disks})')
    print(f'  stage2 sectors: {stage2_sectors}')
    print(f'  kernel sectors: {kernel_sectors} total across {total_disks} disks')
    print(f'  kernel start LBA: {kernel_lba}')
    print(f'  setup sectors: {setup_sectors}')
    print(f'  {total_disks}-disk layout ({FLOPPY_SECTORS} sectors/disk):')
    print(f'    disk 1: {output_path.name} -- {first_disk_kernel_sectors} '
          f'kernel sectors at LBA {kernel_lba}')
    placed = first_disk_kernel_sectors
    for no, path, nbytes in cont_disks:
        nsec = (nbytes + SECTOR_SIZE - 1) // SECTOR_SIZE
        print(f'    disk {no}: {path.name} -- {nsec} kernel sectors '
              f'(kernel LBA {placed}..{placed + nsec}) at disk LBA 0')
        placed += nsec
    for no, path, nbytes in cont_disks:
        print(f'Wrote {path}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
