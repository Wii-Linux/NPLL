/*
 * NPLL - DOL handling
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "DOL"

#include <string.h>
#include <npll/cache.h>
#include <npll/console.h>
#include <npll/cpu.h>
#include <npll/dol.h>
#include <npll/elf.h>
#include <npll/fs.h>
#include <npll/log.h>
#include <npll/utils.h>

static bool dolAddrRangeOk(u32 addr, u32 size) {
	u32 end;

	if (!size)
		return true;
	if (size > 0x10000000u)
		return false;

	end = addr + size;
	if (end < addr)
		return false; /* overflow */

	switch (H_ConsoleType) {
	case CONSOLE_TYPE_GAMECUBE:
		return ((addr & 0xf0000000u) == 0x80000000u) &&
			(end - 0x80000000u) <= MEM1_SIZE_GCN;
	case CONSOLE_TYPE_WII:
		if ((addr & 0xf0000000u) == 0x80000000u)
			return (end - 0x80000000u) <= MEM1_SIZE_WII;
		if ((addr & 0xf0000000u) == 0x90000000u)
			return (end - 0x90000000u) <= MEM2_SIZE_WII;
		return false;
	case CONSOLE_TYPE_WII_U:
		if ((addr & 0xf0000000u) == 0x80000000u)
			return (end - 0x80000000u) <= MEM1_SIZE_WIIU;
		if ((addr & 0xf0000000u) == 0x90000000u)
			return (end - 0x90000000u) <= MEM2_SIZE_WIIU;
		return false;
	}
	return false;
}

int DOL_LoadFile(int fd) {
	struct dolHdr ALIGN(32) hdr;
	void *addr;
	u32 size, off, entry;
	uint i;
	ssize_t ret;
	int fileSz;

	ret = FS_Seek(fd, 0);
	if (ret != 0) {
		log_printf("FS_Seek for dol hdr returned: %d\r\n", (int)ret);
		ret = DOL_ERR_FS_ERROR;
		goto fail;
	}

	ret = FS_Read(fd, &hdr, sizeof(hdr));
	if (ret != (ssize_t)sizeof(hdr)) {
		log_printf("FS_Read for dol hdr returned: %d\r\n", (int)ret);
		ret = DOL_ERR_FS_ERROR;
		goto fail;
	}

	fileSz = FS_GetSize(fd);
	if (fileSz <= (int)sizeof(hdr)) {
		log_printf("DOL too short: %d\r\n", fileSz);
		ret = DOL_ERR_INVALID_EXEC;
		goto fail;
	}

	entry = hdr.entry;
	if (!dolAddrRangeOk(entry, 4)) {
		log_printf("DOL entry 0x%08x is not in a valid memory range\r\n", entry);
		ret = DOL_ERR_INVALID_EXEC;
		goto fail;
	}

	if (hdr.bssSize && !dolAddrRangeOk(hdr.bssAddr, hdr.bssSize)) {
		log_printf("DOL bss range 0x%08x+0x%x is not valid\r\n", hdr.bssAddr, hdr.bssSize);
		ret = DOL_ERR_INVALID_EXEC;
		goto fail;
	}

	/* validate every section first so we either load all or none */
	for (i = 0; i < DOL_NUM_SECTIONS; i++) {
		size = hdr.sectSize[i];
		off = hdr.sectOff[i];

		if (!size)
			continue;
		if (off < sizeof(hdr) || off >= (u32)fileSz || (off + size) > (u32)fileSz) {
			log_printf("DOL section %u offset/size (0x%x+0x%x) out of file (size %d)\r\n",
				i, off, size, fileSz);
			ret = DOL_ERR_INVALID_EXEC;
			goto fail;
		}
		if (!dolAddrRangeOk(hdr.sectAddr[i], size)) {
			log_printf("DOL section %u addr 0x%08x size 0x%x out of range\r\n",
				i, hdr.sectAddr[i], size);
			ret = DOL_ERR_INVALID_EXEC;
			goto fail;
		}
	}

	/* load every non-empty section */
	for (i = 0; i < DOL_NUM_SECTIONS; i++) {
		size = hdr.sectSize[i];
		if (!size)
			continue;

		off = hdr.sectOff[i];
		addr = physToCached(hdr.sectAddr[i]);

		log_printf("Loading section %u from offset %u to addr %08x, size %u\r\n",
			i, off, (u32)addr, size);

		ret = FS_Seek(fd, (ssize_t)off);
		if (ret != (ssize_t)off) {
			log_printf("FS_Seek for section %u returned: %d\r\n", i, (int)ret);
			ret = DOL_ERR_FS_ERROR;
			goto fail;
		}

		ret = FS_Read(fd, addr, size);
		if (ret != (ssize_t)size) {
			log_printf("FS_Read for section %u returned: %d\r\n", i, (int)ret);
			ret = DOL_ERR_FS_ERROR;
			goto fail;
		}

		if (i < DOL_NUM_TEXT)
			dcache_flush_icache_invalidate(addr, size);
		else
			dcache_flush(addr, size);
	}

	/* zero BSS if present */
	if (hdr.bssSize) {
		addr = physToCached(hdr.bssAddr);
		memset(addr, 0, hdr.bssSize);
		dcache_flush(addr, hdr.bssSize);
	}

	FS_Close(fd);

	/* get ready to jump ship (shut down subsystems, ack, mask, and disable IRQs, etc) */
	H_PrepareForExecEntry();

	if (H_PreEntryHook)
		H_PreEntryHook();

	CPU_DCacheFlushAll();

	ELF_DoEntry(0, 0, 0, virtToPhys(entry), false);
	__builtin_unreachable();

fail:
	FS_Close(fd);
	return (int)ret;
}
