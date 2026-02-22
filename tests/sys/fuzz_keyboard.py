#!/usr/bin/env python3
# Simple fuzzing test for scancode/control-code processing logic

def simulate_scancode(scancode, shifted=False, ctrl=False):
    # This simulates the logic in keyboard.c
    kbd_us = [0] * 128
    kbd_us[0x1E] = 'a'  # A key
    kbd_us[0x2E] = 'c'  # C key
    kbd_us[0x2C] = 'z'  # Z key
    kbd_us[0x39] = ' '

    kbd_us_shifted = [0] * 128
    kbd_us_shifted[0x1E] = 'A'
    kbd_us_shifted[0x2E] = 'C'
    kbd_us_shifted[0x2C] = 'Z'

    if scancode >= 128:
        return None

    c = kbd_us_shifted[scancode] if shifted else kbd_us[scancode]

    if ctrl and isinstance(c, str):
        if 'a' <= c <= 'z':
            c = chr(ord(c) - ord('a') + 1)
        elif 'A' <= c <= 'Z':
            c = chr(ord(c) - ord('A') + 1)

    return c


def test_fuzz():
    print("Fuzzing scancode mapping...")
    for i in range(256):
        _ = simulate_scancode(i, False, False)
        _ = simulate_scancode(i, True, False)
        _ = simulate_scancode(i, False, True)
        _ = simulate_scancode(i, True, True)

    assert simulate_scancode(0x2E, ctrl=True) == '\x03', "Ctrl+C should map to ETX (0x03)"
    assert simulate_scancode(0x2C, ctrl=True) == '\x1a', "Ctrl+Z should map to SUB (0x1A)"
    assert simulate_scancode(0x2E, shifted=True, ctrl=True) == '\x03', "Ctrl+Shift+C should map to ETX"
    print("Fuzzing complete. Control-code mappings verified.")

if __name__ == "__main__":
    test_fuzz()
