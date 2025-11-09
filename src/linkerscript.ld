/*
 * linkerscript for GameCube/Wii/Wii U
 */

OUTPUT_FORMAT("elf32-powerpc", "elf32-powerpc", "elf32-powerpc");
OUTPUT_ARCH(powerpc:common);
_entry = 0x00004000;
ENTRY(_entry); /* we can't rely on the linker since technically _start = 0x80004000 */

MEMORY {
	/* XXX: we start @ 0x4000 to give the exception handlers room */
	PHYS (rxx): ORIGIN = 0x00004000, LENGTH = 0x01800000 /* physical MEM1, the load address of our ELF */
	VIRT (rwx): ORIGIN = 0x80004000, LENGTH = 0x01800000 /* virtual runtime addresses after BAT setup */
}


PHDRS {
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
	bss  PT_LOAD FLAGS(6);
}

/* keep track of current LMA */
_lma = ORIGIN(PHYS);

SECTIONS
{
	/* start linker VMA at the virtual address where code will run */
	. = ORIGIN(VIRT);

	.text : AT(_lma) {
		KEEP(*(.init))
		*(.text)
		*(.text*)
		KEEP(*(.ctors))
		. = ALIGN(32);
	} > VIRT :text
	_lma = _lma + SIZEOF(.text);

	.rodata : AT(_lma) {
		*(.rodata)
		*(.rodata*)
		. = ALIGN(32);
	} > VIRT :data
	_lma = _lma + SIZEOF(.rodata);

	.data : AT(_lma) {
		*(.data)
		*(.data*)
		. = ALIGN(32);
	} > VIRT :data
	_lma = _lma + SIZEOF(.data);

	.sdata : AT(_lma) {
		PROVIDE(__sda_start = .);
		*(.sdata)
		*(.sdata.*)
		/**(.gnu.linkonce.s.*)*/
		. = ALIGN(32);
	} > VIRT :data
	_lma = _lma + SIZEOF(.sdata);

	.sdata2 : AT(_lma) {
		PROVIDE(__sda2_start = .);
		*(.sdata2)
		*(.sdata2.*)
		/**(.gnu.linkonce.s2.*)*/
		. = ALIGN(32);
	} > VIRT :data
	_lma = _lma + SIZEOF(.sdata2);

	.eh_frame_hdr : AT(_lma) {
		KEEP (*(.eh_frame_hdr))
	} > VIRT :data
	_lma = _lma + SIZEOF(.eh_frame_hdr);

	.eh_frame : AT(_lma) {
		KEEP (*(.eh_frame))
	} > VIRT :data
	_lma = _lma + SIZEOF(.eh_frame);

	.drivers : AT(_lma) {
		__drivers_start = .;
		PROVIDE(__drivers_start = .);
		*(.drivers)
		__drivers_end = .;
		PROVIDE(__drivers_end = .);
		. = ALIGN(32);
	} > VIRT :data

	.bss (NOLOAD) : {
		__bss_start = .;
		PROVIDE (__bss_start = .);
		*(.bss)
		*(.bss*)
		*(COMMON)
		. = ALIGN(32);
		PROVIDE (__bss_end = .);
		__bss_end = .;
	} > VIRT :bss
}

__stack_top = (__bss_start + SIZEOF(.bss) + 0x20000 + 7) & (-8);
__stack_bottom = (__bss_start + SIZEOF(.bss));

PROVIDE(__stack_top = __stack_top);
PROVIDE(__stack_bottom = __stack_bottom);

