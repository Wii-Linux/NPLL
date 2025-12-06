/*
 * NPLL - ELF handling
 *
 * Copyright (C) 2025 Techflash
 */

#include <npll/elf_abi.h>
#include <npll/elf.h>
#include <npll/cache.h>
#include <stdio.h>
#include <string.h>

extern void __attribute__((noreturn)) ELF_DoEntry(const void *entry);

int ELF_CheckValid(const void *data) {
	const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)data;

	if (memcmp(data, ELFMAG, SELFMAG))
		return ELF_ERR_WRONG_MAGIC;

	/* check that it's for 32-bit BE PPC, and a standard executable */
	if (ehdr->e_ident[EI_CLASS] != ELFCLASS32 ||
	    ehdr->e_ident[EI_DATA] != ELFDATA2MSB ||
	    ehdr->e_type != ET_EXEC ||
	    ehdr->e_machine != EM_PPC)
		return ELF_ERR_INVALID_EXEC;

	return 0;
}

int ELF_LoadMem(const void *data) {
	const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)data;
	const Elf32_Phdr *phdr;
	Elf32_Addr addr;
	u32 size;
	int i;

	/* get start of phdrs */
	phdr = (const Elf32_Phdr *)((u32)data + ehdr->e_phoff);

	for (i = 0; i < ehdr->e_phnum; i++) {
		/* sanity check */
		if (phdr->p_type != PT_LOAD) {
			printf("ELF: Skipping segment: type %d != %d (PT_LOAD)\r\n", phdr->p_type, PT_LOAD);
			continue;
		}

		/* get the address */
		addr = phdr->p_paddr;
		if (!addr)
			addr = phdr->p_vaddr;
		if (!addr) {
			puts("ELF: Skipping segment: no address");
			continue;
		}

		/* verify there's anything to copy */
		if (!phdr->p_filesz || !phdr->p_memsz) {
			puts("ELF: Skipping segment: no data to copy");
			continue;
		}

		/* get size of smaller value if non-equal */
		size = phdr->p_filesz;
		if (size > phdr->p_memsz)
			size = phdr->p_memsz;


		/* make it virtual */
		addr |= 0x80000000;

		/* TODO: check that the address is in-range */

		/* copy it into memory */
		memcpy((void *)addr, (void *)((u32)data + phdr->p_offset), size);
		printf("ELF: Loading segment %d from offset %u to addr %08x, size %u\r\n", i, phdr->p_offset, addr, size);

		/* and flush the cache - ELF_DoEntry will turn caches off */
		dcache_flush((void *)addr, size);

		phdr = (const Elf32_Phdr *)((u32)phdr + ehdr->e_phentsize);
	}

	/* lets do this thing */
	ELF_DoEntry((void *)(ehdr->e_entry & (~0x80000000)));

	/* ELF_DoEntry does not return */
	__builtin_unreachable();
	return -1;
}
