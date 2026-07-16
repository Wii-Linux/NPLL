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

static int loadAuxFile(int fd, enum pool_idx pool, void **dataOut, u32 extra, u32 *sizeOut,
		       const struct memRange *avoid, size_t avoidCount) {
	ssize_t size, got;
	void *data;

	if (!dataOut || !sizeOut)
		return -1;

	size = FS_GetSize(fd);
	if (size <= 0 || (u64)(size_t)size + extra > 0xffffffffu) {
		FS_Close(fd);
		return -1;
	}

	data = M_PoolAllocAvoid(pool, (size_t)size + extra, 64, avoid, avoidCount);
	if (!data) {
		FS_Close(fd);
		return -1;
	}
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

int L_LoadAuxFile(int fd, enum pool_idx pool, void **dataOut, u32 extra, u32 *sizeOut) {
	return loadAuxFile(fd, pool, dataOut, extra, sizeOut, NULL, 0);
}

int L_LoadAuxFileAvoid(int fd, enum pool_idx pool, void **dataOut, u32 extra, u32 *sizeOut,
		       const struct memRange *avoid, size_t avoidCount) {
	return loadAuxFile(fd, pool, dataOut, extra, sizeOut, avoid, avoidCount);
}

static int addReservedRange(struct memRange *ranges, size_t capacity, size_t *count,
			    u64 start, u64 size) {
	if (!size || start > 0xffffffffu || size > 0xffffffffu ||
	    start + size > 0x100000000ull)
		return 0;
	if (*count >= capacity)
		return -FDT_ERR_NOSPACE;
	ranges[*count].start = (u32)start;
	ranges[*count].size = (u32)size;
	(*count)++;
	return 0;
}

static u64 readCells(const fdt32_t *cells, int count) {
	u64 value = 0;
	int i;

	for (i = 0; i < count; i++)
		value = (value << 32) | fdt32_to_cpu(cells[i]);
	return value;
}

static int readNodeCellCount(const void *fdt, int node, const char *wanted, int fallback) {
	const struct fdt_property *prop;
	const char *name;
	u32 tag;
	int next, offset, propLen;

	tag = fdt_next_tag(fdt, node, &next);
	if (tag != FDT_BEGIN_NODE || next < 0)
		return -FDT_ERR_BADSTRUCTURE;
	offset = next;
	for (;;) {
		tag = fdt_next_tag(fdt, offset, &next);
		if (next < 0)
			return next;
		if (tag != FDT_PROP)
			return fallback;
		prop = fdt_offset_ptr(fdt, offset, sizeof(*prop));
		if (!prop)
			return -FDT_ERR_TRUNCATED;
		propLen = (int)fdt32_to_cpu(prop->len);
		if (fdt32_to_cpu(prop->nameoff) >= fdt_size_dt_strings(fdt))
			return -FDT_ERR_BADSTRUCTURE;
		name = (const char *)fdt + fdt_off_dt_strings(fdt) +
		       fdt32_to_cpu(prop->nameoff);
		if (!strcmp(name, wanted)) {
			if (propLen != (int)sizeof(fdt32_t))
				return -FDT_ERR_BADNCELLS;
			return (int)fdt32_to_cpu(*(const fdt32_t *)prop->data);
		}
		offset = next;
	}
}

int L_CollectReserved(const void *fdt, struct memRange *ranges, size_t capacity, size_t *count) {
	const struct fdt_property *prop;
	const fdt32_t *reg;
	const char *name;
	u64 start, size;
	u32 tag;
	int addrCells, sizeCells, child, depth, entries, i, next, node, regLen, ret;
	bool disabled;

	if (!fdt || !ranges || !count)
		return -FDT_ERR_BADVALUE;
	ret = fdt_check_header(fdt);
	if (ret)
		return ret;
	*count = 0;

	entries = fdt_num_mem_rsv(fdt);
	if (entries < 0)
		return entries;
	for (i = 0; i < entries; i++) {
		ret = fdt_get_mem_rsv(fdt, i, &start, &size);
		if (ret)
			return ret;
		ret = addReservedRange(ranges, capacity, count, start, size);
		if (ret)
			return ret;
	}

	node = fdt_path_offset(fdt, "/reserved-memory");
	if (node == -FDT_ERR_NOTFOUND)
		return 0;
	if (node < 0)
		return node;
	addrCells = readNodeCellCount(fdt, node, "#address-cells", 2);
	sizeCells = readNodeCellCount(fdt, node, "#size-cells", 1);
	if (addrCells < 1 || addrCells > 2 || sizeCells < 1 || sizeCells > 2)
		return -FDT_ERR_BADNCELLS;

	/*
	 * Traverse forward from /reserved-memory and use fdt_next_node()'s
	 * relative depth to select direct children.  fdt_first_subnode() has been
	 * observed to return no children on Broadway for a valid blob.
	 */
	depth = 0;
	child = node;
	for (;;) {
		child = fdt_next_node(fdt, child, &depth);
		if (child < 0 || depth <= 0)
			break;
		if (depth != 1)
			continue;

		reg = NULL;
		regLen = 0;
		disabled = false;
		tag = fdt_next_tag(fdt, child, &next);
		if (tag != FDT_BEGIN_NODE || next < 0)
			return -FDT_ERR_BADSTRUCTURE;
		for (;;) {
			int propLen;

			tag = fdt_next_tag(fdt, next, &ret);
			if (ret < 0)
				return ret;
			if (tag != FDT_PROP)
				break;
			prop = fdt_offset_ptr(fdt, next, sizeof(*prop));
			if (!prop)
				return -FDT_ERR_TRUNCATED;
			propLen = (int)fdt32_to_cpu(prop->len);
			if (fdt32_to_cpu(prop->nameoff) >= fdt_size_dt_strings(fdt))
				return -FDT_ERR_BADSTRUCTURE;
			name = (const char *)fdt + fdt_off_dt_strings(fdt) +
			       fdt32_to_cpu(prop->nameoff);
			if (!strcmp(name, "reg")) {
				reg = (const fdt32_t *)prop->data;
				regLen = propLen;
			} else if (!strcmp(name, "status") && propLen >= 8 &&
				   !memcmp(prop->data, "disabled", 8)) {
				disabled = true;
			}
			next = ret;
		}
		if (!reg || disabled)
			continue; /* Dynamic or disabled reservation. */
		if (regLen % ((addrCells + sizeCells) * (int)sizeof(*reg)))
			return -FDT_ERR_BADVALUE;
		entries = regLen / ((addrCells + sizeCells) * (int)sizeof(*reg));
		for (i = 0; i < entries; i++) {
			start = readCells(reg, addrCells);
			reg += addrCells;
			size = readCells(reg, sizeCells);
			reg += sizeCells;
			ret = addReservedRange(ranges, capacity, count, start, size);
			if (ret)
				return ret;
		}
	}
	if (child < 0 && child != -FDT_ERR_NOTFOUND)
		return child;
	return 0;
}

bool L_RangeReserved(const void *fdt, u32 start, u32 size) {
	struct memRange ranges[64];
	u32 end, rangeEnd;
	size_t count, i;

	if (!size || start > 0xffffffffu - size)
		return true;
	if (L_CollectReserved(fdt, ranges, 64, &count))
		return true;
	end = start + size;
	for (i = 0; i < count; i++) {
		if (ranges[i].start > 0xffffffffu - ranges[i].size)
			continue;
		rangeEnd = ranges[i].start + ranges[i].size;
		if (start < rangeEnd && ranges[i].start < end)
			return true;
	}
	return false;
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
