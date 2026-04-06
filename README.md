# NPLL - Nintendo® PowerPC® Linux® Loader

NPLL is a bootloader for all 3 of Nintendo's PowerPC video game consoles:
- GameCube™
- Wii™
- Wii U™

It provides an interface to load Linux on these platforms, all in one universal binary.

## Boot Process
1. `_start` in src/entry.S, then that entire thing executes from top to bottom (set up virtual memory, stack, SDA/SDA2, clear BSS)
2. `init` in src/init.c, does hardware detection then hands off below
	1. `initGamecube` in src/gamecube/init.c
		1. Go to top-level step 3
	2. `initWii` in src/wii/init.c
		1. Set up USB Gecko debug output for the following steps
		2. Map MEM2
		3. Set HID4[SBE]
		4. Seed initial state for state machine
		4. A state machine that does the following:
			- (`STATE_ANALYZE`) Analyze hardware
				- Save important state like whether AHBPROT, SRNPROT, and MEM2 are unrestricted
				- IOS? -> `STATE_ANALYZE_IOS`
				- MINI? -> `STATE_ANALYZE_MINI`
			- (`STATE_ANALYZE_IOS`) Analyze IOS state
				- Have not yet initialized IOS IPC core? -> `STATE_IOS_INIT`
				- Ran ARMBootNow already? -> panic
				- Ran `/dev/sha` exploit but still no hardware perms? -> panic
				- No perms? -> `STATE_IOS_PRIV_ESC`
				- Have perms? -> `STATE_IOS_ARMBOOTNOW`
			- (`STATE_IOS_INIT`) Initialize IOS IPC core
				- Save IOS version
				- Flush IPC transactions
				- Close all file descriptors
				- Open `/dev/es` and make sure it's sane; if it isn't, go back to "Flush IPC transactions" and try again.
				- -> `STATE_ANALYZE_IOS`
			- (`STATE_IOS_PRIV_ESC`) Gain permissions from IOS
				- Run the `/dev/sha` exploit to gain AHBPROT and SRNPROT permissions
				- -> `STATE_ANALYZE`
			- (`STATE_IOS_ARMBOOTNOW`) Boot MINI from IOS
				- Use ARMBootNow to hot-patch IOS in SRAM to cause an attempted IOS reload to start MINI instead
				- -> `STATE_ANALYZE`
			- (`STATE_HW_UNRESTRICT`) Ensure hardware is fully unrestricted
				- Enable Broadway access to SRAM
				- Detect vWii
				- Set up Broadway ownership of GPIOs
				- Unrestrict MEM1 and MEM2
				- -> `STATE_ANALYZE`
			- (`STATE_ANALYZE_MINI`) Analyze MINI state
				- MINI IPC core not initialized? -> `STATE_MINI_INIT`
				- IPC initialized, but missing any perms? -> `STATE_MINI_PRIV_ESC`
				- IPC initialized, have perms, booted with MINI, and have not reloaded? -> `STATE_MINI_RELOAD`
				- IPC initialized, have perms, booted with MINI, and have reloaded? -> `STATE_READY`
				- IPC initialized, have perms, booted with IOS, ran ARMBootNow? -> `STATE_POST_IOS_SANITIZE`
			- (`STATE_MINI_INIT`) Initialize MINI IPC core
				- Try to initialize MINI IPC
				- Success? -> `STATE_ANALYZE`
				- Timeout? -> `STATE_ANALYZE`
				- Other failure? -> panic
			- (`STATE_MINI_RELOAD`) Reload from unknown copy of MINI into NPLL's bundled copy
				- Send MINI IPC request to jump to our copy of MINI
				- Unmark flags for "MINI IPC initialized", and "currently running MINI"
				- -> `STATE_ANALYZE`
			- (`STATE_MINI_PRIV_ESC`) Gain permissions under MINI
				- Send MINI IPC requests to set up AHBPROT and SRNPROT
				- -> `STATE_HW_UNRESTRICT`
			- (`STATE_POST_IOS_SANITIZE`) Clean up hardware after abandoning IOS from ARMBootNow
				- Reset OHCIs
				- -> `STATE_READY`
			- (`STATE_READY`) Ready
				- Exit state machine
				
		5. Check for HBC return-to-loader stub
		6. Set up driver mask
		7. Sanity check everything and clean up
		8. Go to top-level step 3
	3. `initWiiU` in src/wiiu/init.c
		1. Check AHBPROT
		2. Set up GPIOs
		3. Set HID4[SBE], needed to map the majority of MEM2
		4. Map (most of) MEM2
		5. Go to top-level step 3
3. Common initialization, like exception handling
4. Driver initialization - in-order:
	1. critical (mainly for logging, e.g. EXI, USB Gecko)
	2. graphics (VI, Latte FB)
	3. input (SI, PI RSW)
	4. other (GPIO)
	5. block (e.g. Hollywood/Latte Front SD)
5. Enter main loop, running driver callbacks indefinitely

## Subsystem IDs
This codebase uses subsystem IDs for global variables and functions - similar in style to DOOM.
- `I`   - Initialization
- `D`   - Drivers
- `H`   - Current Hardware
- `B`   - Block devices
- `M`   - Memory
- `FS`  - Filesystem
- `IN`  - Input
- `O`   - Output
- `V`   - Video
- `E`   - PowerPC Exception handling
- `ELF` - ELF binary handling
- `UI`  - Menu UI
- `P`   - Block device partition management
- `C`   - Config file handling

## Copyright / Legal / Disclaimers
"Nintendo®" is a registered trademark of Nintendo of America Inc.  
"GameCube™" is a trademark of Nintendo of America Inc.  
"Wii™" is a trademark of Nintendo of America Inc.  
"Wii U™" is a trademark of Nintendo of America Inc.  
"PowerPC®" is a registered trademark of International Business Machines Corp.  
"Linux®" is the registered trademark of Linus Torvalds in the U.S. and other countries.  

All code unless otherwise stated is Copyright (C) 2025-2026 Techflash and NPLL contributors.  See the relevant file for additional copyright information.
