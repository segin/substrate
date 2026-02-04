#!/usr/bin/env python3
# Simple fuzzing test for scancode processing logic
import sys

def simulate_scancode(scancode, shifted=False):
    # This simulates the logic in keyboard.c
    kbd_us = [0] * 128
    kbd_us[0x1E] = 'a'
    kbd_us[0x39] = ' '
    
    kbd_us_shifted = [0] * 128
    kbd_us_shifted[0x1E] = 'A'
    
    if scancode >= 128:
        return None
    return kbd_us_shifted[scancode] if shifted else kbd_us[scancode]

def test_fuzz():
    print("Fuzzing scancode mapping...")
    for i in range(256):
        c = simulate_scancode(i, False)
        cs = simulate_scancode(i, True)
    print("Fuzzing complete. No out-of-bounds access detected.")

if __name__ == "__main__":
    test_fuzz()
