#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/fb.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    printf("FB Test: Opening /dev/fb0...\n");
    
    int fd = open("/dev/fb0", O_RDWR);
    if (fd < 0) {
        perror("Failed to open /dev/fb0");
        return 1;
    }
    
    struct fb_var_screeninfo vi;
    if (ioctl(fd, FBIOGET_VSCREENINFO, &vi) < 0) {
        perror("ioctl failed");
        close(fd);
        return 1;
    }
    
    printf("Framebuffer Info:\n");
    printf("  Resolution: %dx%d\n", vi.xres, vi.yres);
    printf("  Virtual:    %dx%d\n", vi.xres_virtual, vi.yres_virtual);
    printf("  BPP:        %d\n", vi.bits_per_pixel);
    printf("  Red:        %d/%d\n", vi.red.offset, vi.red.length);
    printf("  Green:      %d/%d\n", vi.green.offset, vi.green.length);
    printf("  Blue:       %d/%d\n", vi.blue.offset, vi.blue.length);
    
    size_t size = vi.xres_virtual * vi.yres_virtual * (vi.bits_per_pixel / 8);
    printf("  Mapping %zu bytes...\n", size);
    
    void *fb_ptr = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (fb_ptr == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        return 1;
    }
    
    printf("  Mapped at %p. Drawing test pattern...\n", fb_ptr);
    
    // Draw a red square at (100, 100)
    int square_size = 100;
    int start_x = 100;
    int start_y = 100;
    
    uint32_t *pixels = (uint32_t *)fb_ptr;
    // Assuming 32bpp for simplicity in test
    if (vi.bits_per_pixel == 32) {
        for (int y = start_y; y < start_y + square_size; y++) {
            for (int x = start_x; x < start_x + square_size; x++) {
                if (x < vi.xres && y < vi.yres) {
                    pixels[y * vi.xres + x] = 0x00FF0000; // Red (00RRGGBB)
                }
            }
        }
    } else {
        printf("  Skipping drawing (only 32bpp supported in test for now).\n");
    }
    
    printf("  Done. Verifying readback...\n");
    if (vi.bits_per_pixel == 32) {
        uint32_t color = pixels[start_y * vi.xres + start_x];
        if (color == 0x00FF0000) {
            printf("  Readback SUCCESS: 0x%08x\n", color);
        } else {
            printf("  Readback FAILED: Expected 0x00FF0000, got 0x%08x\n", color);
        }
    }

    munmap(fb_ptr, size);
    close(fd);
    return 0;
}
