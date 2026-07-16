/*
 * NPLL - ELF handling
 *
 * Copyright (C) 2025-2026 Techflash
 */

#define MODULE "ELF"

#include <libfdt.h>
#include <string.h>
#include <npll/console.h>
#include <npll/cache.h>
#include <npll/cpu.h>
#include <npll/elf_abi.h>
#include <npll/elf.h>
#include <npll/fs.h>
#include <npll/irq.h>
#include <npll/linux.h>
#include <npll/log.h>
#include <npll/panic.h>
#include <npll/utils.h>

#define ARGV_MAGIC 0x5f617267u
#define WIIU_LOADER_MAGIC 0xcafefecau
#define WIIU_LOADER_EA    0xe9200000u

/*
 * Book3s32 has to allocate its initial hash table from MEM1.  Wii U's
 * memory size makes that table 8 MiB large and 8 MiB aligned, so leave the
 * final 8 MiB of MEM1 entirely available for it.  The boot wrapper naturally
 * leaves its DTB lower in memory; direct vmlinux loading has to do so itself.
 */
#define WIIU_LINUX_HASH_SIZE 0x00800000u
#define WIIU_LINUX_DTB_LIMIT (MEM1_PHYS_BASE + MEM1_SIZE_WIIU - WIIU_LINUX_HASH_SIZE)
#define LINUX_DTB_ALIGN      0x00001000u

struct dkpArgv {
	u32 magic;
	char *commandLine;
	u32 length;
	u32 argc;
	char **argv;
	char **endARGV;
};

struct wiiuLoaderData {
	u32 magic;
	char cmdline[256];
	void *initrd;
	u32 initrdSize;
};

static void ELF_InstallLinuxData(u32 entry, const void *initrd, u32 initrdSize, const char *cmdline, u32 flags) {
	size_t len, copyLen;
	u32 oldBatu, oldBatl;
	struct wiiuLoaderData *loader;
	void *entryCached = physToCached(entry);

	len = cmdline ? strlen(cmdline) + 1 : 0;

	if (cmdline && (flags & ELF_LINUX_CMDLINE_DKP)) {
		struct dkpArgv *argv = (struct dkpArgv *)((u8 *)entryCached + 8);

		if (*(u32 *)((u8 *)entryCached + 4) == ARGV_MAGIC) {
			argv->magic = ARGV_MAGIC;
			argv->commandLine = (char *)cmdline;
			argv->length = (u32)len;
			argv->argc = 0;
			argv->argv = NULL;
			argv->endARGV = NULL;
			dcache_flush(argv, sizeof(*argv));
			dcache_flush((void *)cmdline, len);
		} else
			log_puts("dkp_cmdline requested but executable has no argv magic");
	}

	if (flags & ELF_LINUX_CMDLINE_LNXLDR) {
		if (H_ConsoleType != CONSOLE_TYPE_WII_U) {
			log_puts("linux_loader_cmdline requested outside Wii U");
			return;
		}

		/*
		 * linux-loader places this structure at physical 0x89200000, well
		 * above NPLL's regular MEM2 BATs.  Borrow DBAT6 to expose the
		 * containing 256 MiB as an uncached window at 0xe0000000.
		 */
		oldBatu = mfspr(DBAT6U);
		oldBatl = mfspr(DBAT6L);
		setbat(6, SETBAT_TYPE_DATA, 0xe0001fffu, 0x8000002au);
		loader = (struct wiiuLoaderData *)WIIU_LOADER_EA;

		if (loader->magic == WIIU_LOADER_MAGIC) {
			if (cmdline) {
				copyLen = len;
				if (copyLen > sizeof(loader->cmdline))
					copyLen = sizeof(loader->cmdline);
				memcpy(loader->cmdline, cmdline, copyLen);
				loader->cmdline[sizeof(loader->cmdline) - 1] = '\0';
			}
			if (initrd) {
				loader->initrd = virtToPhys(initrd);
				loader->initrdSize = initrdSize;
			}
			sync();
		} else
			log_puts("linux_loader_cmdline requested but loader magic is absent");

		setbat(6, SETBAT_TYPE_DATA, oldBatu, oldBatl);
	}
}

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

static bool ELF_LoadPhdr(const Elf32_Phdr *phdr, bool linuxDirect, void **dest, u32 *loadSz, size_t *off, int *bail) {
	void *addr;
	u32 size;
	uintptr_t start, end;

	/* sanity check */
	if (phdr->p_type != PT_LOAD) {
		log_printf("Skipping segment: type %d != %d (PT_LOAD)\r\n", phdr->p_type, PT_LOAD);
		*bail = 0;
		return false;
	}

	/*
	 * A raw 32-bit PowerPC vmlinux is linked at PAGE_OFFSET (normally
	 * 0xc0000000), but its PT_LOAD p_paddr is the physical destination and
	 * may legitimately be zero.  Generic ELF files do not reliably give
	 * p_paddr meaning, so fallback to p_vaddr for them.
	 */
	addr = (void *)(uintptr_t)phdr->p_paddr;
	if (!linuxDirect && !addr)
		addr = (void *)(uintptr_t)phdr->p_vaddr;
	if (!linuxDirect && !addr) {
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
	start = (uintptr_t)addr;
	end = start + phdr->p_memsz;

	/* Check the complete in-memory segment, including its zero-filled tail. */
	if (end < start || !phdr->p_memsz || !addrIsValidCached(addr) || !addrIsValidCached((void *)(end - 1))) {
		log_printf("address 0x%08x w/ size %u is not valid on this platform\r\n", addr, phdr->p_memsz);
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
	size_t off;

	/* get start of phdrs */
	phdr = (const Elf32_Phdr *)((uintptr_t)data + ehdr->e_phoff);

	for (i = 0; i < ehdr->e_phnum; i++) {
		load = ELF_LoadPhdr(phdr, false, &addr, &size, &off, &ret);
		if (ret)
			return ret;
		else if (!load) {
			phdr = (const Elf32_Phdr *)((uintptr_t)phdr + ehdr->e_phentsize);
			continue;
		}

		/* copy it into memory */
		memcpy(addr, (void *)((uintptr_t)data + off), size);
		log_printf("Loading segment %d from offset %u to addr %08x, size %u\r\n", i, phdr->p_offset, addr, size);

		/* Executable data must be visible to an already-enabled I-cache. */
		if (phdr->p_flags & PF_X)
			dcache_flush_icache_invalidate(addr, size);
		else
			dcache_flush(addr, size);

		if (phdr->p_memsz > phdr->p_filesz) {
			memset((u8 *)addr + phdr->p_filesz, 0, phdr->p_memsz - phdr->p_filesz);
			if (phdr->p_flags & PF_X)
				dcache_flush_icache_invalidate((u8 *)addr + phdr->p_filesz, phdr->p_memsz - phdr->p_filesz);
			else
				dcache_flush((u8 *)addr + phdr->p_filesz, phdr->p_memsz - phdr->p_filesz);
		}

		phdr = (const Elf32_Phdr *)((uintptr_t)phdr + ehdr->e_phentsize);
	}

	/* get ready to jump ship (shut down subsystems, ack, mask, and disable IRQs, etc) */
	H_PrepareForExecEntry();

	if (H_PreEntryHook)
		H_PreEntryHook();

	/* Do not let the entry stub's L1 invalidation discard dirty handoff data. */
	CPU_DCacheFlushAll();

	/* lets do this thing */
	ELF_DoEntry(0, 0, 0, virtToPhys((uintptr_t)ehdr->e_entry), false);

	/* ELF_DoEntry does not return */
	__builtin_unreachable();
}

static int _elfLoadFile(int fd, const void *dtb, const void *initrd, u32 initrdSize, const char *cmdline, u32 cmdlineFlags) {
	/* should be aligned since we're reading into these */
	Elf32_Ehdr ALIGN(32) ehdr;
	Elf32_Phdr ALIGN(32) phdr;
	void *addr;
	u32 size;
	uint i;
	bool load, linuxEntryFound = false;
	int ret;
	ssize_t res;
	size_t off;
	u32 linuxEntry = 0, linuxLoadEnd = 0;
	bool irqWasEnabled = false, irqStateSaved = false, loadStarted = false;

	/* read in the ehdr */
	res = FS_Seek(fd, 0);
	if (res != 0) {
		log_printf("FS_Seek for ehdr returned: %d\r\n", res);
		ret = ELF_ERR_FS_ERROR;
		goto fail;
	}
	res = FS_Read(fd, &ehdr, sizeof(ehdr));
	if (res != sizeof(ehdr)) {
		log_printf("FS_Read for ehdr returned: %d\r\n", res);
		ret = ELF_ERR_FS_ERROR;
		goto fail;
	}
	ret = ELF_CheckValid(&ehdr);
	if (ret) {
		goto fail;
	}

	/*
	 * Validate every direct-Linux segment before physical zero is overwritten.
	 * This keeps malformed images and DT reservation conflicts recoverable.
	 */
	if (dtb) {
		for (i = 0; i < ehdr.e_phnum; i++) {
			res = FS_Seek(fd, (ssize_t)(ehdr.e_phoff + (i * ehdr.e_phentsize)));
			if (res != (ssize_t)(ehdr.e_phoff + (i * ehdr.e_phentsize))) {
				ret = ELF_ERR_FS_ERROR;
				goto fail;
			}
			res = FS_Read(fd, &phdr, sizeof(phdr));
			if (res != sizeof(phdr)) {
				ret = ELF_ERR_FS_ERROR;
				goto fail;
			}
			load = ELF_LoadPhdr(&phdr, true, &addr, &size, &off, &ret);
			if (ret)
				goto fail;
			if (!load)
				continue;
			if (L_RangeReserved(dtb, phdr.p_paddr, phdr.p_memsz)) {
				log_printf("Linux PT_LOAD %08x-%08x overlaps DT-reserved memory\r\n",
					   phdr.p_paddr, phdr.p_paddr + phdr.p_memsz);
				ret = ELF_ERR_INVALID_EXEC;
				goto fail;
			}
			if (ehdr.e_entry >= phdr.p_vaddr &&
			    ehdr.e_entry - phdr.p_vaddr < phdr.p_memsz) {
				u32 entryOff = ehdr.e_entry - phdr.p_vaddr;

				if (phdr.p_paddr + entryOff < phdr.p_paddr) {
					log_puts("Linux physical entry address overflows");
					ret = ELF_ERR_INVALID_EXEC;
					goto fail;
				}
				linuxEntry = phdr.p_paddr + entryOff;
				linuxEntryFound = true;
			}
			if (phdr.p_paddr <= 0xffffffffu - phdr.p_memsz &&
			    phdr.p_paddr + phdr.p_memsz > linuxLoadEnd)
				linuxLoadEnd = phdr.p_paddr + phdr.p_memsz;
		}
		if (!linuxEntryFound) {
			log_printf("Linux entry %08x is not in a PT_LOAD segment\r\n", ehdr.e_entry);
			ret = ELF_ERR_INVALID_EXEC;
			goto fail;
		}
	}

	/*
	 * A direct Linux image is loaded at physical zero and replaces NPLL's
	 * exception vectors.  No interrupt may be taken from that point onward.
	 * The storage drivers used below are polled, and normal executable cleanup
	 * disables IRQs before touching them as well.
	 */
	if (dtb) {
		irqWasEnabled = IRQ_DisableSave();
		irqStateSaved = true;
	}

	for (i = 0; i < ehdr.e_phnum; i++) {
		res = FS_Seek(fd, (ssize_t)(ehdr.e_phoff + (i * ehdr.e_phentsize)));
		if (res != (ssize_t)(ehdr.e_phoff + (i * ehdr.e_phentsize))) {
			log_printf("FS_Seek for phdr returned: %d\r\n", res);
			ret = ELF_ERR_FS_ERROR;
			goto fail;
		}
		res = FS_Read(fd, &phdr, sizeof(phdr));
		if (res != sizeof(phdr)) {
			log_printf("FS_Read for phdr returned: %d\r\n", res);
			ret = ELF_ERR_FS_ERROR;
			goto fail;
		}

		load = ELF_LoadPhdr(&phdr, dtb != NULL, &addr, &size, &off, &ret);
		if (ret)
			goto fail;
		else if (!load)
			continue;

		res = FS_Seek(fd, (ssize_t)off);
		if (res != (ssize_t)off) {
			log_printf("FS_Seek for segment %d returned: %d\r\n", i, res);
			ret = ELF_ERR_FS_ERROR;
			goto fail;
		}
		log_printf("Loading segment %d from offset %u to addr %08x, size %u\r\n", i, phdr.p_offset, addr, size);
		loadStarted = true;
		res = FS_Read(fd, addr, size);
		if (res != (ssize_t)size) {
			log_printf("FS_Read for segment %d returned: %d\r\n", i, res);
			ret = ELF_ERR_FS_ERROR;
			goto fail;
		}

		/* Linux enters with L1 enabled, so synchronize executable lines too. */
		if (phdr.p_flags & PF_X)
			dcache_flush_icache_invalidate(addr, size);
		else
			dcache_flush(addr, size);

		if (phdr.p_memsz > phdr.p_filesz) {
			memset((u8 *)addr + phdr.p_filesz, 0, phdr.p_memsz - phdr.p_filesz);
			if (phdr.p_flags & PF_X)
				dcache_flush_icache_invalidate((u8 *)addr + phdr.p_filesz, phdr.p_memsz - phdr.p_filesz);
			else
				dcache_flush((u8 *)addr + phdr.p_filesz, phdr.p_memsz - phdr.p_filesz);
		}
	}

	/* close the file */
	FS_Close(fd);
	if (dtb && H_ConsoleType == CONSOLE_TYPE_WII_U) {
		u32 dtbSize = (u32)fdt_totalsize(dtb);
		u32 dtbPhys, dtbEnd;
		void *newDtb;

		if (linuxLoadEnd > 0xffffffffu - (LINUX_DTB_ALIGN - 1u)) {
			log_puts("Linux load end cannot be aligned for DTB placement");
			ret = ELF_ERR_INVALID_EXEC;
			goto fail_closed;
		}
		dtbPhys = (linuxLoadEnd + LINUX_DTB_ALIGN - 1u) & ~(LINUX_DTB_ALIGN - 1u);
		if (!dtbSize || dtbPhys > 0xffffffffu - dtbSize) {
			log_puts("Linux DTB placement overflows");
			ret = ELF_ERR_INVALID_EXEC;
			goto fail_closed;
		}
		dtbEnd = dtbPhys + dtbSize;
		if (dtbEnd > WIIU_LINUX_DTB_LIMIT) {
			log_printf("Linux image/DTB ends at %08x, cannot preserve hash window at %08x\r\n",
			           dtbEnd, WIIU_LINUX_DTB_LIMIT);
			ret = ELF_ERR_INVALID_EXEC;
			goto fail_closed;
		}

		newDtb = physToCached(dtbPhys);
		memmove(newDtb, dtb, dtbSize);
		dcache_flush(newDtb, dtbSize);
		dtb = newDtb;
		log_printf("ELF: Linux PT_LOAD end %08x, moved DTB to %08x-%08x\r\n",
		           linuxLoadEnd, dtbPhys, dtbEnd);
	}

	/* get ready to jump ship (shut down subsystems, ack, mask, and disable IRQs, etc) */
	H_PrepareForExecEntry();

	if (H_PreEntryHook)
		H_PreEntryHook();

	/* Do not let the entry stub's L1 invalidation discard dirty handoff data. */
	CPU_DCacheFlushAll();

	/* lets do this thing */
	if (dtb) {
		u32 entry = linuxEntry;
		u32 dtbPhys = (u32)(uintptr_t)virtToPhys(dtb);

		ELF_InstallLinuxData(entry, initrd, initrdSize, cmdline, cmdlineFlags);
		log_printf("ELF: entering Linux at %08x with DTB %08x\r\n", entry, dtbPhys);
		ELF_DoEntry(dtbPhys, entry, 0, (void *)(uintptr_t)entry, true);
	} else {
		u32 entry = (u32)(uintptr_t)virtToPhys((uintptr_t)ehdr.e_entry);
		ELF_InstallLinuxData(entry, initrd, initrdSize, cmdline, cmdlineFlags);
		ELF_DoEntry(0, 0, 0, virtToPhys((uintptr_t)ehdr.e_entry), false);
	}

	/* ELF_DoEntry does not return */
	__builtin_unreachable();

fail:
	FS_Close(fd);
fail_closed:
	if (irqStateSaved) {
		/* Physical zero may now contain a partial kernel instead of vectors. */
		/* FIXME: this might be recoverable by re-initializing the vectors */
		if (loadStarted)
			panic("Linux load failed after overwriting exception vectors");
		IRQ_Restore(irqWasEnabled);
	}
	return ret;
}

int ELF_LoadFile(int fd) {
	return _elfLoadFile(fd, NULL, NULL, 0, NULL, 0);
}

int ELF_LoadLinuxFile(int fd, const void *dtb, const void *initrd, u32 initrdSize, const char *cmdline, u32 cmdlineFlags) {
	return _elfLoadFile(fd, dtb, initrd, initrdSize, cmdline, cmdlineFlags);
}
