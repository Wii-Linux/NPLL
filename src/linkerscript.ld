/*
 * linkerscript for GameCube/Wii/Wii U
 */

OUTPUT_FORMAT("elf32-powerpc", "elf32-powerpc", "elf32-powerpc");
OUTPUT_ARCH(powerpc:common);
_entry = 0x00004000;
ENTRY(_entry); /* we can't rely on the linker since technically _start = 0x80004000 */

MEMORY {
	/*
	 * We load in low MEM1, so loaders can load us, then
	 * relocate to high MEM1, so we can load a kernel in low
	 * MEM1, without clobbering ourselves.
	 * We also start at 0x4000 to make room since loaders generally
	 * don't like putting us at 0x0.
	 */

	LOWPHYS (rwx): ORIGIN = 0x00004000, LENGTH = 0x017fc000 /* physical MEM1, the load address of our ELF */
	HIPHYS (rwx): ORIGIN = 0x01200000, LENGTH = 0x00600000 /* physical MEM1, the load address of our relocated code+data */
	LOWVIRT (rwx): ORIGIN = 0x80004000, LENGTH = 0x017fc000 /* virtual runtime addresses after BAT setup */
	HIVIRT (rwx): ORIGIN = 0x81200000, LENGTH = 0x00600000 /* virtual runtime address after relocation */
}


PHDRS {
	init PT_LOAD FLAGS(5);
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
	bss  PT_LOAD FLAGS(6);
}

/* keep track of current LMA */
_lma = ORIGIN(LOWPHYS);

SECTIONS
{
	/* start linker VMA at the virtual address where init code will run */
	. = ORIGIN(LOWVIRT);
	.init : AT(_lma) {
		*(.init)
		*(.init*)
		. = ALIGN(32);
	} > LOWVIRT :init
	_lma = _lma + SIZEOF(.init);

	/* start linker VMA at the virtual address where relocated code will run */
	. = ORIGIN(HIVIRT);
	PROVIDE(__reloc_source_start = _lma);
	PROVIDE(__reloc_dest_start = .);

	.text : AT(_lma) {
		*(.text)
		*(.text*)
		KEEP(*(.ctors))
		. = ALIGN(32);
	} > HIVIRT :text
	_lma = _lma + SIZEOF(.text);

	.rodata : AT(_lma) {
		*(.rodata)
		*(.rodata*)
		. = ALIGN(32);
	} > HIVIRT :data
	_lma = _lma + SIZEOF(.rodata);

	.data : AT(_lma) {
		*(.data)
		*(.data*)
		. = ALIGN(32);
	} > HIVIRT :data
	_lma = _lma + SIZEOF(.data);

	.sdata : AT(_lma) {
		PROVIDE(__sda_start = .);
		*(.sdata)
		*(.sdata.*)
		*(.gnu.linkonce.s.*)
		. = ALIGN(32);
	} > HIVIRT :data
	_lma = _lma + SIZEOF(.sdata);

	.sdata2 : AT(_lma) {
		PROVIDE(__sda2_start = .);
		*(.sdata2)
		*(.sdata2.*)
		*(.gnu.linkonce.s2.*)
		. = ALIGN(32);
	} > HIVIRT :data
	_lma = _lma + SIZEOF(.sdata2);

	.drivers : AT(_lma) {
		PROVIDE(__drivers_start = .);
		*(.drivers)
		PROVIDE(__drivers_end = .);
		. = ALIGN(32);
	} > HIVIRT :data
	_lma = _lma + SIZEOF(.drivers);

	/* GNU ld complains if we discard it sadly */
	.note.gnu.build-id : AT(_lma) {
		*(.note.gnu.build-id);
		. = ALIGN(32);
	} > HIVIRT :data
	_lma = _lma + SIZEOF(.note.gnu.build-id);

	PROVIDE(__reloc_source_end = _lma);
	PROVIDE(__reloc_dest_end = .);

	.bss (NOLOAD) : AT(_lma) {
		__bss_start = .;
		PROVIDE (__bss_start = .);
		*(.bss)
		*(.bss*)
		*(COMMON)
		. = ALIGN(32);
		PROVIDE (__bss_end = .);
		__bss_end = .;
	} > HIVIRT :bss
	_lma = _lma + SIZEOF(.bss);

	.sbss (NOLOAD) : AT(_lma) {
		PROVIDE(__sbss_start = .);
		*(.dynsbss)
		*(.sbss)
		*(.sbss.*)
		*(.gnu.linkonce.sb.*)
		*(.scommon)
		. = ALIGN(32);
		PROVIDE(__sbss_end = .);
	} > HIVIRT :bss

	/DISCARD/ : {
		*(.eh_frame)
		*(.eh_frame_hdr)
	}
}

__stack_top = (__bss_start + SIZEOF(.bss) + 0x20000 + 7) & (-8);
__stack_bottom = (__bss_start + SIZEOF(.bss));

PROVIDE(__stack_top = __stack_top);
PROVIDE(__stack_bottom = __stack_bottom);

