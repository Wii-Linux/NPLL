/*
 * NPLL - PowerPC Linux direct-entry boot support
 *
 * Copyright (C) 2026 Techflash
 */

#define MODULE "linux"

#include <stdlib.h>
#include <string.h>
#include <libfdt.h>
#include <npll/cache.h>
#include <npll/console.h>
#include <npll/fs.h>
#include <npll/linux.h>
#include <npll/log.h>
#include <npll/utils.h>

#define DTB_SLACK 1024u

int L_LoadAuxFile(int fd, enum pool_idx pool, void **dataOut, u32 extra, u32 *sizeOut) {
	ssize_t size, got;
	void *data;

	if (!dataOut || !sizeOut)
		return -1;

	size = FS_GetSize(fd);
	if (size <= 0 || (u64)(size_t)size + extra > 0xffffffffu) {
		FS_Close(fd);
		return -1;
	}

	data = M_PoolAlloc(pool, (size_t)size + extra, 64);
	got = FS_Read(fd, data, (size_t)size);
	FS_Close(fd);
	if (got != size) {
		free(data);
		return -1;
	}

	*dataOut = data;
	*sizeOut = (u32)size;
	return 0;
}

static int setAddressProp(void *fdt, int node, const char *name, u32 addr) {
	int cells = fdt_address_cells(fdt, 0);

	if (cells == 2)
		return fdt_setprop_u64(fdt, node, name, addr);
	if (cells == 1)
		return fdt_setprop_u32(fdt, node, name, addr);
	return -FDT_ERR_BADNCELLS;
}

/* Truncate MEM2 to avoid trashing MINI, like the bootwrapper does */
static int fixupWiiMemory(void *fdt) {
	const fdt32_t *prop;
	fdt32_t reg[4];
	u32 mem2Base, mem2Size, boundary;
	int memory, len;

	if (H_ConsoleType != CONSOLE_TYPE_WII || !H_WiiMEM2Top)
		return 0;

	memory = fdt_path_offset(fdt, "/memory");
	if (memory < 0)
		return memory;
	prop = fdt_getprop(fdt, memory, "reg", &len);
	if (!prop || len != (int)sizeof(reg))
		return 0; /* A different memory layout is not one the wrapper adjusts. */

	memcpy(reg, prop, sizeof(reg));
	mem2Base = fdt32_to_cpu(reg[2]);
	mem2Size = fdt32_to_cpu(reg[3]);
	boundary = (u32)(uintptr_t)virtToPhys(H_WiiMEM2Top);
	if (boundary > mem2Base && boundary - mem2Base < mem2Size) {
		reg[3] = cpu_to_fdt32(boundary - mem2Base);
		return fdt_setprop(fdt, memory, "reg", reg, sizeof(reg));
	}
	return 0;
}

int L_PrepareDTB(struct linuxBootFiles *files, const char *cmdline) {
	void *fdt;
	u32 capacity, initrdStart, initrdEnd;
	int chosen, ret;

	if (!files || !files->dtb)
		return -1;

	fdt = files->dtb;
	ret = fdt_check_header(fdt);
	if (ret)
		goto fail;

	if (fdt_totalsize(fdt) != files->dtbSize)
		return -1;

	capacity = files->dtbSize + DTB_SLACK + (cmdline ? (u32)strlen(cmdline) + 1u : 0u);
	ret = fdt_open_into(fdt, fdt, (int)capacity);
	if (ret)
		goto fail;
	ret = fixupWiiMemory(fdt);
	if (ret)
		goto fail;

	chosen = fdt_path_offset(fdt, "/chosen");
	if (chosen == -FDT_ERR_NOTFOUND)
		chosen = fdt_add_subnode(fdt, 0, "chosen");
	if (chosen < 0) {
		ret = chosen;
		goto fail;
	}

	if (cmdline) {
		ret = fdt_setprop_string(fdt, chosen, "bootargs", cmdline);
		if (ret)
			goto fail;
	}

	if (files->initrd) {
		initrdStart = (u32)(uintptr_t)virtToPhys(files->initrd);
		if (files->initrdSize > 0xffffffffu - initrdStart) {
			ret = -FDT_ERR_BADVALUE;
			goto fail;
		}
		initrdEnd = initrdStart + files->initrdSize;

		ret = setAddressProp(fdt, chosen, "linux,initrd-start", initrdStart);
		if (ret)
			goto fail;
		ret = setAddressProp(fdt, chosen, "linux,initrd-end", initrdEnd);
		if (ret)
			goto fail;
		ret = fdt_add_mem_rsv(fdt, initrdStart, files->initrdSize);
		if (ret && ret != -FDT_ERR_EXISTS)
			goto fail;
		dcache_flush(files->initrd, files->initrdSize);
	}

	ret = fdt_pack(fdt);
	if (ret)
		goto fail;
	dcache_flush(fdt, fdt_totalsize(fdt));
	return 0;

fail:
	log_printf("device tree preparation failed: %s\r\n", fdt_strerror(ret));
	return ret;
}
