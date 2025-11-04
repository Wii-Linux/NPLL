# NPLL - Nintendo® PowerPC Linux® Loader

NPLL is a bootloader for all 3 of Nintendo's PowerPC video game consoles:
- GameCube®
- Wii®
- Wii U®

It provides an interface to load Linux on these platforms, all in one universal binary.

## Boot Process
1. `_start` in src/entry.S, then that entire thing executes from top to bottom (set up virtual memory, stack, SDA/SDA2, clear BSS)
2. `init` in src/init.c, does hardware detection then hands off below

A. `initGamecube` in src/gamecube/init.c

    1. Go to top-level step 3
  
  B. `initWii` in src/wii/init.c
  
    1. Check AHBPROT
    2. Set up GPIOs
    3. Map MEM2
    4. Set HID4[SBE]
    5. Clear upper BATs
    6. Go to top-level step 3
    
  C. `initWiiU` in src/wiiu/init.c
  
    1. Check AHBPROT
    2. Set up GPIOs
    3. Set HID4[SBE], needed to map the majority of MEM2
    4. Map (most of) MEM2
    5. Go to top-level step 3
    
3. Common initialization, like exception handling
4. Driver initialization - in-order:
  a. critical (mainly for logging, e.g. EXI, USB Gecko)
  b. block (e.g. SDGecko, Hollywood/Latte Front SD, USB Mass Storage)
  c. partitions (MBR, GPT, APM, etc)
  d. filesystem (e.g. FAT, ext[2/3/4], btrfs, xfs, iso9660)
  e. graphics (VI, DRC, GX, GPU7)
  f. input (SI, GPIO, PI, DRC, USB HID, Bluetooth)
  g. other (e.g. GPIO)
5. Enter main loop - in order, try to:
  a. Check for and handle any pending I/O operations
  b. Check for and handle any inputs
  c. Update the screen if necessary

## Subsystem IDs
This codebase uses subsystem IDs for global variables and functions - similar in style to DOOM.
- `I`  - Initialization
- `D`  - Drivers
- `H`  - Current Hardware
- `B`  - Block devices
- `M`  - Menu
- `FS` - Filesystem
- `IN` - Input
- `O`  - Output
- `V`  - Video
- `E`  - PowerPC Exception handling

## Copyright / Legal / Disclaimers
"Nintendo®" is a registered trademark of Nintendo of America Inc.  
"GameCube®" is a registered trademark of Nintendo of America Inc.  
"Wii®" is a registered trademark of Nintendo of America Inc.  
"Wii U®" is a registered trademark of Nintendo of America Inc.  
"Linux®" is the registered trademark of Linus Torvalds in the U.S. and other countries.  

All code unless otherwise stated is Copyright (C) 2025 Techflash and NPLL contributors.  See the relevant file for additional copyright information.


