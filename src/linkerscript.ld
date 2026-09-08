/*
 * linkerscript for GameCube/Wii/Wii U
 */

OUTPUT_FORMAT("elf32-powerpc", "elf32-powerpc", "elf32-powerpc");
OUTPUT_ARCH(powerpc:common);
_entry = _start;
ENTRY(_entry);

MEMORY {
	/*
	 * We load in low MEM1, so loaders can load us, then
	 * relocate to high MEM1, so we can load a kernel in low
	 * MEM1, without clobbering ourselves.
	 * We also start at 0x4000 to make room since loaders generally
	 * don't like putting us at 0x0.
	 */

	BS1VIRT (rwx): ORIGIN = 0x80003400, LENGTH = 0x00000c00 /* cached MEM1, BS1 */
	LOWVIRT (rwx): ORIGIN = 0x80004000, LENGTH = 0x016fc000 /* ELF load/link addresses, including the bootstrap */
	HIVIRT (rwx): ORIGIN = 0x81700000, LENGTH = 0x000c0000 /* runtime reservation after relocation */
}


PHDRS {
	bs1  PT_LOAD FLAGS(5);
	init PT_LOAD FLAGS(5);
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
	bss  PT_LOAD FLAGS(6);
}

SECTIONS
{
	/* start linker VMA at the virtual address where the BS1 stub will run */
	. = ORIGIN(BS1VIRT);
	.bs1 : {
		KEEP(*(.bs1 .bs1*))
		. = ALIGN(32);
	} > BS1VIRT :bs1

	/* start linker VMA at the virtual address where init code will run */
	. = ORIGIN(LOWVIRT);
	.init : {
		KEEP(*(.init .init*))
		. = ALIGN(32);
	} > LOWVIRT :init

	/*
	 * Link the runtime at its load address so ELF VMA == LMA.  The
	 * bootstrap copies it to high MEM1 (or native Wii U MEM2) and applies
	 * the load bias to its pointers.  Destination symbols describe the
	 * linked image; adding the bias gives the actual runtime addresses.
	 */
	PROVIDE(__reloc_source_start = .);
	PROVIDE(__reloc_mem1_bias = ORIGIN(HIVIRT) - __reloc_dest_start);
	PROVIDE(__reloc_dest_start = .);

	.text : {
		*(.text)
		*(.text*)
		KEEP(*(.ctors))
		. = ALIGN(32);
	} > LOWVIRT :text

	.rodata : {
		*(.rodata)
		*(.rodata*)
		. = ALIGN(32);
	} > LOWVIRT :data

	.data : {
		*(.data)
		*(.data*)
		. = ALIGN(32);
	} > LOWVIRT :data

	.sdata : {
		PROVIDE(__sda_start = .);
		*(.sdata)
		*(.sdata.*)
		*(.gnu.linkonce.s.*)
		. = ALIGN(32);
	} > LOWVIRT :data

	.sdata2 : {
		PROVIDE(__sda2_start = .);
		*(.sdata2)
		*(.sdata2.*)
		*(.gnu.linkonce.s2.*)
		. = ALIGN(32);
	} > LOWVIRT :data

	/*
	 * GCC's embedded PowerPC -mrelocatable ABI puts load-bias-sensitive
	 * pointers in .got2 and the addresses of all other pointer slots in
	 * .fixup.  The low-MEM1 bootstrap adjusts both after copying the image.
	 */
	.got2 : {
		PROVIDE(__got2_start = .);
		*(.got2)
		PROVIDE(__got2_end = .);
		. = ALIGN(32);
	} > LOWVIRT :data

	.fixup : {
		PROVIDE(__fixup_start = .);
		KEEP(*(.fixup))
		PROVIDE(__fixup_end = .);
		. = ALIGN(32);
	} > LOWVIRT :data

	.drivers : {
		PROVIDE(__drivers_start = .);
		KEEP(*(.drivers))
		PROVIDE(__drivers_end = .);
		. = ALIGN(32);
	} > LOWVIRT :data

	/* GNU ld complains if we discard it sadly */
	.note.gnu.build-id : {
		*(.note.gnu.build-id);
		. = ALIGN(32);
	} > LOWVIRT :data

	PROVIDE(__reloc_source_end = .);
	PROVIDE(__reloc_dest_end = .);

	.bss (NOLOAD) : {
		__bss_start = .;
		PROVIDE (__bss_start = .);
		*(.bss)
		*(.bss*)
		*(COMMON)
		. = ALIGN(32);
		PROVIDE (__bss_end = .);
		__bss_end = .;
	} > LOWVIRT :bss

	.sbss (NOLOAD) : {
		PROVIDE(__sbss_start = .);
		*(.dynsbss)
		*(.sbss)
		*(.sbss.*)
		*(.gnu.linkonce.sb.*)
		*(.scommon)
		. = ALIGN(32);
		PROVIDE(__sbss_end = .);
	} > LOWVIRT :bss

	/DISCARD/ : {
		*(.eh_frame)
		*(.eh_frame_hdr)
		*(.interp)
		*(.dynamic)
		*(.dynsym)
		*(.dynstr)
		*(.gnu.hash)
		*(.got)
	}
}

__stack_top = (__bss_start + SIZEOF(.bss) + 0x20000 + 7) & (-8);
__stack_bottom = (__bss_start + SIZEOF(.bss));

PROVIDE(__stack_top = __stack_top);
PROVIDE(__stack_bottom = __stack_bottom);

/* the relocation code assumes cache-alignment and same size */
ASSERT(__reloc_source_start % 32 == 0, "cache-align __reloc_source_start");
ASSERT(__reloc_source_end % 32 == 0, "cache-align __reloc_source_end");
ASSERT(__reloc_dest_start % 32 == 0, "cache-align __reloc_dest_start");
ASSERT(__reloc_dest_end % 32 == 0, "cache-align __reloc_dest_end");
ASSERT(__reloc_dest_end - __reloc_dest_start == __reloc_source_end - __reloc_source_start, "fix relocation size");
ASSERT(__stack_top + __reloc_mem1_bias <= ORIGIN(HIVIRT) + LENGTH(HIVIRT), "runtime image and stack exceed relocation reservation");
