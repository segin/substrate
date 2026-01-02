# Legacy Video Drivers Specification

## Overview
TestUnix provides support for legacy video hardware, including CGA, Hercules, and EGA adapters. These drivers allow the system to function on older x86 machines.

## Drivers
- **CGA (Color Graphics Adapter):**
    - **Memory:** 0xB8000.
    - **Mode:** 80x25 text mode.
- **Hercules Graphics Card:**
    - **Memory:** 0xB0000.
    - **Mode:** 720x348 monochrome graphics mode.
    - **Interleaving:** Uses 4 banks of memory.
- **EGA (Enhanced Graphics Adapter):**
    - **Memory:** 0xA0000.
    - **Mode:** Planar graphics modes.

## Constraints
- Drivers are currently basic skeletons for the initial prototype.
- No support for color palette switching on EGA.
- Hercules driver only supports `putpixel` in graphics mode.
