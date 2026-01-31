#!/usr/bin/env python3
import random

def fuzz_mouse_logic():
    print("Fuzzing Mouse Packet Logic...")
    
    # State
    cycle = 0
    byte = [0, 0, 0]
    
    # Random stream
    for i in range(10000):
        data = random.randint(0, 255)
        
        if cycle == 0:
            byte[0] = data
            if data & 0x08:
                cycle += 1
        elif cycle == 1:
            byte[1] = data
            cycle += 1
        elif cycle == 2:
            byte[2] = data
            cycle = 0
            
            # Simulated Decode
            buttons = byte[0] & 0x07
            dx = byte[1]
            dy = byte[2]
            
            if byte[0] & 0x10:
                dx -= 256
            if byte[0] & 0x20:
                dy -= 256
                
            # Verify basic constraints (e.g. no crash on any input)
            assert -256 <= dx <= 255
            assert -256 <= dy <= 255
            
    print("Fuzzing complete. 10000 random bytes processed.")

if __name__ == "__main__":
    fuzz_mouse_logic()
