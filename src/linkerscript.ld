/*
 * linkerscript for GameCube/Wii/Wii U
 */

OUTPUT_FORMAT("elf32-powerpc", "elf32-powerpc", "elf32-powerpc");
OUTPUT_ARCH(powerpc:common);
EXTERN(_start);
ENTRY(_start);

PHDRS {
	stub PT_LOAD FLAGS(5);
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
	bss1 PT_LOAD FLAGS(6);
	bss2 PT_LOAD FLAGS(6);
}

SECTIONS {
	/* stub is loaded at physical address 0x00003400 (though both 0x80003400 and 0x00003400 are equivalent for IOS) */
	/* This can also be used to load an arbitrary standalone stub at an arbitrary address in memory, for any purpose */
	/* Use -Wl,--section-start,.stub=0xADDRESS to change */
	. = 0x00003400;

	.stub :
	{
		KEEP(*(.stub))
	} :stub = 0

 	/* default base address */
	/* use -Wl,--section-start,.init=0xADDRESS to change */
 	. = 0x80004000;
 
	/* Program */
	.init          :
	{
		KEEP (*crt0.o(*.init))
		KEEP (*(.init))
	} :text = 0

	/* default base address */
	. = 0x80004000;

	.interp			: { *(.interp) 	}
	.hash			: { *(.hash) }
	.dynsym			: { *(.dynsym) }
	.dynstr			: { *(.dynstr) }
	.gnu.version	: { *(.gnu.version) }
	.gnu.version_d	: { *(.gnu.version_d) }
	.gnu.version_r	: { *(.gnu.version_r) }
	.rel.text		: { *(.rel.text .rel.text.* .rel.gnu.linkonce.t.*) }
	.rela.text		: { *(.rela.text .rela.text.* .rela.gnu.linkonce.t.*) }
	.rel.rodata		: { *(.rel.rodata .rel.rodata.* .rel.gnu.linkonce.r.*) }
	.rela.rodata	: { *(.rela.rodata .rela.rodata.* .rela.gnu.linkonce.r.*) }
	.rel.data		: { *(.rel.data .rel.data.* .rel.gnu.linkonce.d.*) }
	.rela.data		: { *(.rela.data .rela.data.* .rela.gnu.linkonce.d.*) }
	.rel.tdata		: { *(.rel.tdata .rel.tdata.* .rel.gnu.linkonce.td.*) }
	.rela.tdata		: { *(.rela.tdata .rela.tdata.* .rela.gnu.linkonce.td.*) }
	.rel.tbss		: { *(.rel.tbss .rel.tbss.* .rel.gnu.linkonce.tb.*) }
	.rela.tbss		: { *(.rela.tbss .rela.tbss.* .rela.gnu.linkonce.tb.*) }
	.rel.got		: { *(.rel.got)	}
	.rela.got		: { *(.rela.got) }
	.rela.got1		: { *(.rela.got1) }
	.rela.got2		: { *(.rela.got2) }
	.rel.sdata		: { *(.rel.sdata .rel.sdata.* .rel.gnu.linkonce.s.*) }
	.rela.sdata		: { *(.rela.sdata .rela.sdata.* .rela.gnu.linkonce.s.*) }
	.rel.sbss		: { *(.rel.sbss .rel.sbss.* .rel.gnu.linkonce.sb.*) }
	.rela.sbss		: { *(.rela.sbss .rela.sbss.* .rel.gnu.linkonce.sb.*) }
	.rel.sdata2		: { *(.rel.sdata2 .rel.sdata2.* .rel.gnu.linkonce.s2.*) }
	.rela.sdata2	: { *(.rela.sdata2 .rela.sdata2.* .rela.gnu.linkonce.s2.*) }
	.rel.sbss2		: { *(.rel.sbss2 .rel.sbss2.* .rel.gnu.linkonce.sb2.*) }
	.rela.sbss2		: { *(.rela.sbss2 .rela.sbss2.* .rela.gnu.linkonce.sb2.*) }
	.rel.bss		: { *(.rel.bss .rel.bss.* .rel.gnu.linkonce.b.*) }
	.rela.bss		: { *(.rela.bss .rela.bss.* .rela.gnu.linkonce.b.*) }
	.rel.plt		: { *(.rel.plt) }
	.rela.plt		: { *(.rela.plt) }

	.text      :
	{
		*(.text)
		*(.text.*)
		/* .gnu.warning sections are handled specially by elf32.em.  */
		*(.gnu.warning)
		*(.gnu.linkonce.t.*)
		. = ALIGN(32);   /* REQUIRED. LD is flaky without it. */
	} = 0

	PROVIDE (__etext = .);
	PROVIDE (_etext = .);
	PROVIDE (etext = .);

	.rodata   : { *(.rodata) *(.rodata.*) *(.gnu.linkonce.r.*) } :data
	.rodata1   : { *(.rodata1) }
	.sdata2   : {
		PROVIDE(__sda2_start = .);
		*(.sdata2)
		*(.sdata2.*)
		*(.gnu.linkonce.s2.*)
	}
	.sbss2   : { *(.sbss2) *(.sbss2.*) *(.gnu.linkonce.sb2.*) }
  /* Adjust the address for the data segment.  We want to adjust up to
     the same address within the page on the next page up.  */
  /* Ensure the __preinit_array_start label is properly aligned.  We
     could instead move the label definition inside the section, but
     the linker would then create the section even if it turns out to
     be empty, which isn't pretty.  */
	. = ALIGN(32 / 8);
	.data    :
	{
		*(.data)
		*(.data.*)
		*(.gnu.linkonce.d.*)
		. = ALIGN(32);   /* REQUIRED. LD is flaky without it. */
	}

	.data1   : { *(.data1) }
	.tdata	  : { *(.tdata .tdata.* .gnu.linkonce.td.*) }
	.tbss		  : { *(.tbss .tbss.* .gnu.linkonce.tb.*) *(.tcommon) }
	.eh_frame : { KEEP (*(.eh_frame)) }
	.gcc_except_table : { *(.gcc_except_table) }
	.fixup          : { *(.fixup) }
	.got1           : { *(.got1) }
	.got2           : { *(.got2) }
	.dynamic       : { *(.dynamic) }

	.jcr            : { KEEP (*(.jcr)) }
	.got		  : { *(.got.plt) *(.got) }


	/*	We want the small data sections together, so single-instruction offsets
		can access them all, and initialized data all before uninitialized, so
		we can shorten the on-disk segment size.  */

	.sdata     :
	{
		PROVIDE(__sda_start = .);
		*(.sdata)
		*(.sdata.*)
		*(.gnu.linkonce.s.*)
		. = ALIGN(32);   /* REQUIRED. LD is flaky without it. */
	}

	_edata = .;
	PROVIDE (edata = .);

	.drivers   :
	{
		__drivers_start = .;
		PROVIDE(__drivers_start = .);
		*(.drivers)
		__drivers_end = .;
		PROVIDE(__drivers_end = .);
		. = ALIGN(32);
	}
	
	.sbss      :
	{
		__sbss_start = .;
		PROVIDE (__sbss_start = .);
		PROVIDE (___sbss_start = .);
		*(.dynsbss)
		*(.sbss)
		*(.sbss.*)
		*(.gnu.linkonce.sb.*)
		*(.scommon)
		PROVIDE (__sbss_end = .);
		PROVIDE (___sbss_end = .);
		. = ALIGN(32);   /* REQUIRED. LD is flaky without it. */
		__sbss_end = .;
	} :bss1

	.bss       :
	{
		__bss_start = .;
		PROVIDE (__bss_start = .);
		*(.dynbss)
		*(.bss)
		*(.bss.*)
		*(.gnu.linkonce.b.*)
		*(COMMON)
	/*	Align here to ensure that the .bss section occupies space up to
		_end.  Align after .bss to ensure correct alignment even if the
		.bss section disappears because there are no input sections.  */

		. = ALIGN(32);

		PROVIDE (__bss_end = .);
		__bss_end = .;
	} :bss2

	_end = .;
	PROVIDE(end = .);
	/* Stabs debugging sections.  */
	.stab 0 : { *(.stab) }
	.stabstr 0 : { *(.stabstr) }
	.stab.excl 0 : { *(.stab.excl) }
	.stab.exclstr 0 : { *(.stab.exclstr) }
	.stab.index 0 : { *(.stab.index) }
	.stab.indexstr 0 : { *(.stab.indexstr) }
	.comment 0 : { *(.comment) }
	/*	DWARF debug sections.
		Symbols in the DWARF debugging sections are relative to the beginning
		of the section so we begin them at 0.  */
	/* DWARF 1 */
	.debug          0 : { *(.debug) }
	.line           0 : { *(.line) }
	/* GNU DWARF 1 extensions */
	.debug_srcinfo  0 : { *(.debug_srcinfo) }
	.debug_sfnames  0 : { *(.debug_sfnames) }
	/* DWARF 1.1 and DWARF 2 */
	.debug_aranges  0 : { *(.debug_aranges) }
	.debug_pubnames 0 : { *(.debug_pubnames) }
	/* DWARF 2 */
	.debug_info     0 : { *(.debug_info) }
	.debug_abbrev   0 : { *(.debug_abbrev) }
	.debug_line     0 : { *(.debug_line) }
	.debug_frame    0 : { *(.debug_frame) }
	.debug_str      0 : { *(.debug_str) }
	.debug_loc      0 : { *(.debug_loc) }
	.debug_macinfo  0 : { *(.debug_macinfo) }
	/* SGI/MIPS DWARF 2 extensions */
	.debug_weaknames 0 : { *(.debug_weaknames) }
	.debug_funcnames 0 : { *(.debug_funcnames) }
	.debug_typenames 0 : { *(.debug_typenames) }
	.debug_varnames  0 : { *(.debug_varnames) }
	/* These must appear regardless of  .  */
}

__stack_top = (__bss_start + SIZEOF(.bss) + 0x20000 + 7) & (-8);
__stack_bottom = (__bss_start + SIZEOF(.bss));

PROVIDE(__stack_top = __stack_top);
PROVIDE(__stack_bottom = __stack_bottom);

