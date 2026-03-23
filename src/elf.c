/*
 * NPLL - ELF handling
 *
 * Copyright (C) 2025-2026 Techflash
 */

#define MODULE "ELF"

#include <string.h>
#include <npll/cache.h>
#include <npll/elf_abi.h>
#include <npll/elf.h>
#include <npll/fs.h>
#include <npll/log.h>
#include <npll/utils.h>

extern void __attribute__((noreturn)) ELF_DoEntry(const void *entry);

int ELF_CheckValid(const void *data) {
	const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)data;

	if (memcmp(data, ELFMAG, SELFMAG)) {
		log_puts("Invalid magic");
		return ELF_ERR_WRONG_MAGIC;
	}

	/* check that it's for 32-bit BE PPC, and a standard executable */
	if (ehdr->e_ident[EI_CLASS] != ELFCLASS32) {
		log_puts("not 32-bit");
		return ELF_ERR_INVALID_EXEC;
	}

	if (ehdr->e_ident[EI_DATA] != ELFDATA2MSB) {
		log_puts("not big-endian");
		return ELF_ERR_INVALID_EXEC;
	}
	if (ehdr->e_type != ET_EXEC) {
		log_puts("not a standard executable");
		return ELF_ERR_INVALID_EXEC;
	}
	if (ehdr->e_machine != EM_PPC) {
		log_puts("not for PowerPC");
		return ELF_ERR_INVALID_EXEC;
	}

	return 0;
}

bool ELF_LoadPhdr(const Elf32_Phdr *phdr, void **dest, u32 *loadSz, ssize_t *off, int *bail) {
	void *addr;
	u32 size;

	/* sanity check */
	if (phdr->p_type != PT_LOAD) {
		log_printf("Skipping segment: type %d != %d (PT_LOAD)\r\n", phdr->p_type, PT_LOAD);
		*bail = 0;
		return false;
	}

	/* get the address */
	addr = (void *)phdr->p_paddr;
	if (!addr)
		addr = (void *)phdr->p_vaddr;
	if (!addr) {
		log_puts("Skipping segment: no address");
		*bail = 0;
		return false;
	}

	/* verify there's anything to copy */
	if (!phdr->p_filesz || !phdr->p_memsz) {
		log_puts("Skipping segment: no data to copy");
		*bail = 0;
		return false;
	}

	/* get size of smaller value if non-equal */
	size = phdr->p_filesz;
	if (size > phdr->p_memsz)
		size = phdr->p_memsz;

	/* make it virtual */
	addr = physToCached(addr);

	if (!addrIsValidCached(addr)) {
		log_printf("address 0x%08x is not valid on this platform\r\n", addr);
		*bail = ELF_ERR_INVALID_EXEC;
		return false;
	}

	*bail = 0;
	*dest = addr;
	*loadSz = size;
	*off = phdr->p_offset;
	return true;
}

int ELF_LoadMem(const void *data) {
	const Elf32_Ehdr *ehdr = (const Elf32_Ehdr *)data;
	const Elf32_Phdr *phdr;
	void *addr;
	u32 size;
	int i, ret;
	bool load;
	ssize_t off;

	/* get start of phdrs */
	phdr = (const Elf32_Phdr *)((u32)data + ehdr->e_phoff);

	for (i = 0; i < ehdr->e_phnum; i++) {
		load = ELF_LoadPhdr(phdr, &addr, &size, &off, &ret);
		if (ret)
			return ret;
		else if (!load)
			continue;

		/* copy it into memory */
		memcpy(addr, (void *)((u32)data + off), size);
		log_printf("Loading segment %d from offset %u to addr %08x, size %u\r\n", i, phdr->p_offset, addr, size);

		/* and flush the cache - ELF_DoEntry will turn caches off */
		dcache_flush(addr, size);

		if (phdr->p_memsz > phdr->p_filesz) {
			memset((u8 *)addr + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz);
			dcache_flush((u8 *)addr + phdr->p_filesz, phdr->p_memsz - phdr->p_filesz);
		}

		phdr = (const Elf32_Phdr *)((u32)phdr + ehdr->e_phentsize);
	}

	/* get ready to jump ship (shut down subsystems, ack, mask, and disable IRQs, etc) */
	H_PrepareForExecEntry();

	/* lets do this thing */
	ELF_DoEntry(virtToPhys(ehdr->e_entry));

	/* ELF_DoEntry does not return */
	__builtin_unreachable();
}

int ELF_LoadFile(int fd) {
	/* should be aligned since we're reading into these */
	Elf32_Ehdr ALIGN(32) ehdr;
	Elf32_Phdr ALIGN(32) phdr;
	void *addr;
	u32 size;
	int i;
	bool load;
	ssize_t ret, off;

	/* read in the ehdr */
	ret = FS_Seek(fd, 0);
	if (ret != 0) {
		log_printf("FS_Seek for ehdr returned: %d\r\n", ret);
		return ELF_ERR_FS_ERROR;
	}
	ret = FS_Read(fd, &ehdr, sizeof(ehdr));
	if (ret != sizeof(ehdr)) {
		log_printf("FS_Read for ehdr returned: %d\r\n", ret);
		return ELF_ERR_FS_ERROR;
	}

	for (i = 0; i < ehdr.e_phnum; i++) {
		ret = FS_Seek(fd, ehdr.e_phoff + (i * ehdr.e_phentsize));
		if (ret != (ssize_t)(ehdr.e_phoff + (i * ehdr.e_phentsize))) {
			log_printf("FS_Seek for phdr returned: %d\r\n", ret);
			return ELF_ERR_FS_ERROR;
		}
		ret = FS_Read(fd, &phdr, sizeof(phdr));
		if (ret != sizeof(phdr)) {
			log_printf("FS_Read for phdr returned: %d\r\n", ret);
			return ELF_ERR_FS_ERROR;
		}

		load = ELF_LoadPhdr(&phdr, &addr, &size, &off, &ret);
		if (ret)
			return ret;
		else if (!load)
			continue;

		ret = FS_Seek(fd, off);
		if (ret != off) {
			log_printf("FS_Seek for segment %d returned: %d\r\n", i, ret);
			return ELF_ERR_FS_ERROR;
		}
		log_printf("Loading segment %d from offset %u to addr %08x, size %u\r\n", i, phdr.p_offset, addr, size);
		ret = FS_Read(fd, addr, size);
		if (ret != (ssize_t)size) {
			log_printf("FS_Read for segment %d returned: %d\r\n", i, ret);
			return ELF_ERR_FS_ERROR;
		}

		/* flush the cache - ELF_DoEntry will turn caches off */
		dcache_flush(addr, size);

		if (phdr.p_memsz > phdr.p_filesz) {
			memset((u8 *)addr + phdr.p_filesz, 0, phdr.p_memsz - phdr.p_filesz);
			dcache_flush((u8 *)addr + phdr.p_filesz, phdr.p_memsz - phdr.p_filesz);
		}
	}

	/* close the file */
	FS_Close(fd);

	/* get ready to jump ship (shut down subsystems, ack, mask, and disable IRQs, etc) */
	H_PrepareForExecEntry();

	/* lets do this thing */
	ELF_DoEntry(virtToPhys(ehdr.e_entry));

	/* ELF_DoEntry does not return */
	__builtin_unreachable();
}
