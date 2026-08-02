/*
 * NPLL - Wii U File System (WFS), read-only
 *
 * Copyright (C) 2026 Techflash
 *
 * The WFS on-disk format handling is based on wfslib by koolkdev,
 * Copyright (C) 2017-2026 koolkdev, licensed under the MIT license.
 */

#define MODULE "wfs"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <npll/allocator.h>
#include <npll/block.h>
#include <npll/console.h>
#include <npll/endian.h>
#include <npll/fs.h>
#include <npll/hollywood/aes.h>
#include <npll/hollywood/otp.h>
#include <npll/hollywood/sha1.h>
#include <npll/log.h>
#include <npll/partition.h>
#include <npll/scfm.h>
#include <npll/types.h>
#include <npll/utils.h>

#define WFS_VERSION                 0x01010800u
#define WFS_PHYSICAL_LOG2            12u
#define WFS_LOGICAL_LOG2             13u
#define WFS_SECTOR_LOG2              9u   /* 512-byte device sector */
#define WFS_PHYSICAL_SIZE            (1u << WFS_PHYSICAL_LOG2)
#define WFS_MAX_BLOCK_SIZE           (1u << 19) /* 8 logical blocks * 8 large blocks */
#define WFS_HASH_SIZE                20u
#define WFS_MAX_FILES                16
#define WFS_MAX_ENTRY_SIZE           1024u
#define WFS_MAX_PATH                 512u
#define WFS_MAX_TREE_DEPTH           128u

#define WFS_METADATA_DIRECTORY_LEAF  0x20000000u
#define WFS_ENTRY_UNENCRYPTED        0x02000000u
#define WFS_ENTRY_LINK               0x04000000u
#define WFS_ENTRY_AREA_BASIC         0x10000000u
#define WFS_ENTRY_AREA_REGULAR       0x20000000u
#define WFS_ENTRY_QUOTA              0x40000000u
#define WFS_ENTRY_DIRECTORY          0x80000000u

enum wfsDeviceType {
	WFS_DEVICE_MLC = 0x136a,
	WFS_DEVICE_USB = 0x16a2
};

enum wfsFileLayout {
	WFS_LAYOUT_INLINE = 0,
	WFS_LAYOUT_BLOCKS = 1,
	WFS_LAYOUT_LARGE_BLOCKS = 2,
	WFS_LAYOUT_CLUSTERS = 3,
	WFS_LAYOUT_CLUSTER_METADATA = 4
};

struct wfsMetadataHeader {
	u32 flags;
	u8 hash[WFS_HASH_SIZE];
} __attribute__((packed));

struct wfsEntry {
	u32 flags;
	u32 sizeOnDisk;
	u32 ctime;
	u32 mtime;
	u32 unknown;
	u32 fileSize;
	u32 directoryBlock;
	u32 owner;
	u32 group;
	u32 mode;
	u8 metadataLog2Size;
	u8 sizeCategory;
	u8 filenameLength;
	u8 caseBitmap;
} __attribute__((packed));

struct wfsDeviceHeader {
	u32 iv;
	u32 version;
	u16 deviceType;
	u16 pad;
	struct wfsEntry rootQuota;
	u32 transactionsBlock;
	u32 transactionsBlocks;
	u32 unknown[2];
} __attribute__((packed));

struct wfsAreaHeader {
	u32 iv;
	u32 blocksCount;
	u32 rootDirectoryBlock;
	u32 shadowDirectory1;
	u32 shadowDirectory2;
	u8 depth;
	u8 blockSizeLog2;
	u8 largeBlockSizeLog2;
	u8 clusterBlockSizeLog2;
	u8 areaType;
	u8 zero;
	u16 remainderBlocks;
	u8 fragments[8 * 8];
	u32 fragmentsLog2BlockSize;
} __attribute__((packed));

struct wfsDirectoryTreeHeader {
	u16 root;
	u16 recordsCount;
} __attribute__((packed));

struct wfsDirectoryNodeHeader {
	u8 prefixLength;
	u8 keysCount;
} __attribute__((packed));

struct wfsDataBlockMetadata {
	u32 blockNumber;
	u8 hash[WFS_HASH_SIZE];
} __attribute__((packed));

struct wfsClusterMetadata {
	u32 blockNumber;
	u8 hash[8][WFS_HASH_SIZE];
} __attribute__((packed));

struct wfsArea {
	u32 physicalBlock;
	u32 blocksCount;
	u32 iv;
	u8 blockLog2;
};

struct wfsFd {
	bool open;
	struct wfsArea area;
	u32 metadataBlock;
	u16 metadataOffset;
	u32 size;
	u32 sizeOnDisk;
	u32 pos;
};

struct wfsState {
	struct partition *part;
	struct wfsArea rootArea;
	u32 deviceIv;
	u32 sectors;
	u16 deviceType;
	bool encrypted;
	bool valid;
	u32 key[4];
	/* Extra 64 bytes hold SHA-1 padding. */
	u8 *buf;
	u8 shaDigest[WFS_HASH_SIZE] ALIGN(4);
	u8 savedHash[WFS_HASH_SIZE];
	struct wfsFd files[WFS_MAX_FILES];
};

static struct wfsState state;

static u32 wfsPow2Ceil(u32 value) {
	u32 out = 1;
	if (!value)
		return 1;
	while (out < value && out < WFS_MAX_ENTRY_SIZE)
		out <<= 1;
	return out < value ? 0 : out;
}

static bool wfsRange(u32 offset, u32 size, u32 limit) {
	return offset <= limit && size <= limit - offset;
}

/* WFS always hashes sector-aligned (and therefore 64-byte-aligned) data. */
static int wfsHash(u8 *data, u32 len, u8 *inBlockHash, u8 digest[WFS_HASH_SIZE]) {
	u8 *pad;
	u64 bits;
	int ret;

	if (!data || !digest || !len || (len & 63))
		return -EINVAL;
	if (inBlockHash) {
		memcpy(state.savedHash, inBlockHash, sizeof(state.savedHash));
		memset(inBlockHash, 0xff, sizeof(state.savedHash));
	}

	pad = data + len;
	pad[0] = 0x80;
	memset(pad + 1, 0, 55);
	bits = (u64)len << 3;
	*(u64 *)(pad + 56) = npll_cpu_to_be64(bits);
	ret = H_SHA1Process(data, (u32 *)digest, len + 64);

	if (inBlockHash)
		memcpy(inBlockHash, state.savedHash, sizeof(state.savedHash));
	return ret;
}

static bool wfsCheckHash(u8 *data, u32 len, u8 *inBlockHash, const u8 expected[WFS_HASH_SIZE]) {
	if (wfsHash(data, len, inBlockHash, state.shaDigest))
		return false;
	return !memcmp(state.shaDigest, expected, WFS_HASH_SIZE);
}

static u32 wfsPhysicalBlock(const struct wfsArea *area, u32 areaBlock) {
	return area->physicalBlock + (areaBlock << (area->blockLog2 - WFS_PHYSICAL_LOG2));
}

static bool wfsAreaBlockValid(const struct wfsArea *area, u32 areaBlock, u32 blocks) {
	return areaBlock <= area->blocksCount && blocks <= area->blocksCount - areaBlock;
}

static int wfsReadBlock(const struct wfsArea *area, u32 areaBlock, u8 blockLog2, u32 len, bool encrypted, const u8 expectedHash[WFS_HASH_SIZE], bool metadata) {
	u32 physical, capacity, iv[4];
	ssize_t ret;

	if (!state.buf) {
		log_puts("wfsReadBlock: !state.buf");
		return -EINVAL;
	}

	if (blockLog2 < WFS_PHYSICAL_LOG2 || blockLog2 > 19) {
		log_printf("wfsReadBlock: blockLog2 (%u) invalid\r\n", blockLog2);
		return -EINVAL;
	}

	capacity = 1u << blockLog2;
	if (!len || len > capacity || (len & 511) || capacity > WFS_MAX_BLOCK_SIZE) {
		log_printf("wfsReadBlock: len (%u) and capacity (%u) invalid\r\n", len, capacity);
		return -ERANGE;
	}
	if (!wfsAreaBlockValid(area, areaBlock, 1u << (blockLog2 - area->blockLog2))) {
		log_printf("wfsReadBlock: !wfsAreaBlockValid()\r\n");
		return -ERANGE;
	}

	physical = wfsPhysicalBlock(area, areaBlock);
	/*
	 * Read through the SCFM overlay so recent-but-unflushed writes (cached on
	 * the SLC) are seen. SCFM indexes whole-device sectors, so use the
	 * device-absolute offset. With no overlay installed this is a plain read.
	 */
	ret = S_SCFMReadMLC(state.part->bdev, state.buf,
		state.part->offset + (u64)physical * WFS_PHYSICAL_SIZE, len);
	if (ret != (ssize_t)len) {
		log_printf("wfsReadBlock: read failed: %d\r\n", (int)ret);
		return -EIO;
	}

	if (encrypted) {
		iv[0] = len;
		/*
		 * wfslib's CalcIV adds the block's offset from the area start in
		 * 512-byte-sector units, i.e. the physical-block delta shifted by
		 * (physical block log2 - sector log2) = 12 - 9 = 3.
		 */
		iv[1] = (area->iv ^ state.deviceIv) +
			((physical - area->physicalBlock) << (WFS_PHYSICAL_LOG2 - WFS_SECTOR_LOG2));
		iv[2] = state.sectors;
		iv[3] = state.part->bdev->blockSize;
		ret = H_AESDecrypt(state.buf, state.buf, iv, state.key, len);
		if (ret) {
			log_printf("wfsReadBlock: H_AESDecrypt failed: %d\r\n", ret);
			return -EIO;
		}
	}

	if (metadata) {
		struct wfsMetadataHeader *header = (struct wfsMetadataHeader *)state.buf;
		if (!wfsCheckHash(state.buf, len, header->hash, header->hash)) {
			log_puts("wfsReadBlock: wfsCheckHash failed on metadata");
			return -EIO;
		}
	}
	else if (expectedHash && !wfsCheckHash(state.buf, len, NULL, expectedHash)) {
		log_puts("wfsReadBlock: wfsCheckHash failed on data");
		return -EIO;
	}

	return 0;
}

static int wfsLoadMetadata(const struct wfsArea *area, u32 areaBlock, u8 blockLog2) {
	return wfsReadBlock(area, areaBlock, blockLog2, 1u << blockLog2, state.encrypted, NULL, true);
}

static int wfsLoadRoot(bool encrypted, u8 blockLog2) {
	struct wfsArea root = { .physicalBlock = 0, .blocksCount = state.sectors >> 3,
		.iv = 0, .blockLog2 = WFS_PHYSICAL_LOG2 };
	return wfsReadBlock(&root, 0, blockLog2, 1u << blockLog2, encrypted, NULL, true);
}

static bool wfsValidateRoot(u8 blockLog2, bool encrypted) {
	struct wfsDeviceHeader *device;
	struct wfsAreaHeader *area;
	int ret;

	ret = wfsLoadRoot(encrypted, blockLog2);
	if (ret) {
		log_printf("wfsValidateRoot: wfsLoadRoot failed (%d)\r\n", ret);
		log_printf("WFS root: version=%08x type=%04x hash=%02x%02x...\r\n",
			((struct wfsDeviceHeader *)(state.buf + 0x18))->version,
			((struct wfsDeviceHeader *)(state.buf + 0x18))->deviceType,
			state.buf[4], state.buf[5]);
		return false;
	}
	device = (struct wfsDeviceHeader *)(state.buf + sizeof(struct wfsMetadataHeader));
	area = (struct wfsAreaHeader *)((u8 *)device + sizeof(*device));
	if (device->version != WFS_VERSION) {
		log_printf("wfsValidateRoot: unsupported version: 0x%08x\r\n", device->version);
		return false;
	}
	/*
	 * deviceType is informational: the known constants are not confirmed to
	 * be exhaustive, and a correct hash + version already proves this is a
	 * valid WFS image decrypted with the right key. Just note unexpected ones.
	 */
	if (device->deviceType != WFS_DEVICE_MLC && device->deviceType != WFS_DEVICE_USB)
		log_printf("wfsValidateRoot: unrecognized type 0x%04x (continuing)\r\n", device->deviceType);
	if (area->blockSizeLog2 != WFS_PHYSICAL_LOG2 && area->blockSizeLog2 != WFS_LOGICAL_LOG2) {
		log_printf("wfsValidateRoot: unsupported blockSizeLog2: 0x%04x\r\n", area->blockSizeLog2);
		return false;
	}
	if (area->blocksCount == 0) {
		log_puts("wfsValidateRoot: blocksCount is 0");
		return false;
	}

	state.deviceIv = device->iv;
	state.deviceType = device->deviceType;
	state.encrypted = encrypted;
	state.rootArea.physicalBlock = 0;
	state.rootArea.blocksCount = area->blocksCount;
	state.rootArea.iv = area->iv;
	state.rootArea.blockLog2 = area->blockSizeLog2;
	state.valid = true;
	return true;
}

static int wfsLoadArea(const struct wfsArea *parent, u32 areaBlock, u8 blockLog2, struct wfsArea *out) {
	struct wfsAreaHeader *header;
	int ret;

	ret = wfsLoadMetadata(parent, areaBlock, blockLog2);
	if (ret)
		return ret;
	header = (struct wfsAreaHeader *)(state.buf + sizeof(struct wfsMetadataHeader));
	if ((header->blockSizeLog2 != WFS_PHYSICAL_LOG2 && header->blockSizeLog2 != WFS_LOGICAL_LOG2) ||
		!header->blocksCount)
		return -EIO;
	out->physicalBlock = wfsPhysicalBlock(parent, areaBlock);
	out->blocksCount = header->blocksCount;
	out->iv = header->iv;
	out->blockLog2 = header->blockSizeLog2;
	return 0;
}

static u32 wfsNodeSize(const struct wfsDirectoryNodeHeader *node, bool parent) {
	u32 size;
	bool hasLeaf;

	hasLeaf = node->keysCount && *((const u8 *)node + sizeof(*node) + node->prefixLength) == 0;
	size = sizeof(*node) + node->prefixLength + (u32)node->keysCount * 3;
	if (parent && hasLeaf)
		size += 2;
	return wfsPow2Ceil(size);
}

static int wfsTreeFind(const u8 *block, u32 blockSize, const char *key, bool parent, u32 *value) {
	const struct wfsDirectoryTreeHeader *tree;
	u32 nodeOff, pos = 0, steps = 0;

	if (blockSize < sizeof(struct wfsMetadataHeader) + 36)
		return -EIO;
	tree = (const struct wfsDirectoryTreeHeader *)(block + sizeof(struct wfsMetadataHeader) + 32);
	nodeOff = tree->root;
	if (!tree->recordsCount)
		return -ENOENT;
	while (++steps <= WFS_MAX_TREE_DEPTH) {
		const struct wfsDirectoryNodeHeader *node;
		const u8 *keys, *values;
		u32 nodeSize, i;
		bool hasLeaf;

		if (!wfsRange(nodeOff, sizeof(*node), blockSize))
			return -EIO;
		node = (const struct wfsDirectoryNodeHeader *)(block + nodeOff);
		nodeSize = wfsNodeSize(node, parent);
		if (!node->keysCount || !nodeSize || !wfsRange(nodeOff, nodeSize, blockSize))
			return -EIO;
		keys = (const u8 *)node + sizeof(*node) + node->prefixLength;
		hasLeaf = keys[0] == 0;
		for (i = 0; i < node->prefixLength; i++) {
			if (!key[pos] || (u8)key[pos] != *((const u8 *)node + sizeof(*node) + i))
				return -ENOENT;
			pos++;
		}
		values = (const u8 *)node + nodeSize - 2 - ((parent && hasLeaf) ? 2 : 0);
		if (!key[pos]) {
			if (!hasLeaf)
				return -ENOENT;
			if (parent)
				*value = *(const u32 *)values;
			else
				*value = *(const u16 *)values;
			return 0;
		}
		for (i = hasLeaf ? 1 : 0; i < node->keysCount; i++) {
			if (keys[i] == (u8)key[pos]) {
				nodeOff = *(const u16 *)(values - (i * 2));
				pos++;
				break;
			}
		}
		if (i == node->keysCount)
			return -ENOENT;
	}
	return -ELOOP;
}

/*
 * A directory-map parent tree maps split-point strings to leaf-block numbers.
 * Unlike a leaf tree, lookup selects the lexicographically greatest split
 * point not greater than the requested name.
 */
static int wfsParentWalk(const u8 *block, u32 blockSize, u32 nodeOff, const char *target,
			 char key[WFS_MAX_PATH], u32 keyLen, char best[WFS_MAX_PATH],
			 u32 *bestValue, bool *found, u32 depth) {
	const struct wfsDirectoryNodeHeader *node;
	const u8 *keys, *values;
	u32 nodeSize, i;
	bool hasLeaf;

	if (depth >= WFS_MAX_TREE_DEPTH || !wfsRange(nodeOff, sizeof(*node), blockSize))
		return -EIO;
	node = (const struct wfsDirectoryNodeHeader *)(block + nodeOff);
	nodeSize = wfsNodeSize(node, true);
	if (!nodeSize || !node->keysCount || keyLen + node->prefixLength >= WFS_MAX_PATH ||
		!wfsRange(nodeOff, nodeSize, blockSize))
		return -EIO;
	memcpy(key + keyLen, (const u8 *)node + sizeof(*node), node->prefixLength);
	keyLen += node->prefixLength;
	key[keyLen] = '\0';
	keys = (const u8 *)node + sizeof(*node) + node->prefixLength;
	hasLeaf = keys[0] == 0;
	values = (const u8 *)node + nodeSize - 4;
	if (hasLeaf && strcmp(key, target) <= 0 && (!*found || strcmp(key, best) > 0)) {
		strcpy(best, key);
		*bestValue = *(const u32 *)values;
		*found = true;
	}
	values = (const u8 *)node + nodeSize - 2 - (hasLeaf ? 2 : 0);
	for (i = hasLeaf ? 1 : 0; i < node->keysCount; i++) {
		u32 child = *(const u16 *)(values - i * 2);
		int ret;
		if (keyLen + 1 >= WFS_MAX_PATH)
			return -EIO;
		key[keyLen] = (char)keys[i];
		key[keyLen + 1] = '\0';
		ret = wfsParentWalk(block, blockSize, child, target, key, keyLen + 1,
			best, bestValue, found, depth + 1);
		if (ret)
			return ret;
	}
	return 0;
}

static int wfsParentFind(const u8 *block, u32 blockSize, const char *target, u32 *value) {
	const struct wfsDirectoryTreeHeader *tree;
	char key[WFS_MAX_PATH], best[WFS_MAX_PATH];
	bool found = false;
	int ret;

	if (blockSize < sizeof(struct wfsMetadataHeader) + 36)
		return -EIO;
	tree = (const struct wfsDirectoryTreeHeader *)(block + sizeof(struct wfsMetadataHeader) + 32);
	if (!tree->recordsCount)
		return -ENOENT;
	key[0] = '\0';
	best[0] = '\0';
	ret = wfsParentWalk(block, blockSize, tree->root, target, key, 0,
		best, value, &found, 0);
	if (ret)
		return ret;
	return found ? 0 : -ENOENT;
}

static int wfsFindEntry(const struct wfsArea *area, u32 directoryBlock, const char *name,
			 struct wfsEntry *entry, u32 *metadataBlock, u16 *metadataOffset) {
	u32 childBlock, offset;
	int ret;

	ret = wfsLoadMetadata(area, directoryBlock, area->blockLog2);
	if (ret)
		return ret;
	if (((struct wfsMetadataHeader *)state.buf)->flags & WFS_METADATA_DIRECTORY_LEAF) {
		ret = wfsTreeFind(state.buf, 1u << area->blockLog2, name, false, &offset);
		if (ret)
			return ret;
		childBlock = directoryBlock;
	}
	else {
		ret = wfsParentFind(state.buf, 1u << area->blockLog2, name, &childBlock);
		if (ret)
			return ret;
		ret = wfsLoadMetadata(area, childBlock, area->blockLog2);
		if (ret)
			return ret;
		if (!(((struct wfsMetadataHeader *)state.buf)->flags & WFS_METADATA_DIRECTORY_LEAF))
			return -EIO;
		ret = wfsTreeFind(state.buf, 1u << area->blockLog2, name, false, &offset);
		if (ret)
			return ret;
	}
	if (offset > (1u << area->blockLog2) - sizeof(*entry))
		return -EIO;
	memcpy(entry, state.buf + offset, sizeof(*entry));
	if (entry->metadataLog2Size < 6 || entry->metadataLog2Size > 10 ||
		!wfsRange(offset, 1u << entry->metadataLog2Size, 1u << area->blockLog2))
		return -EIO;
	*metadataBlock = childBlock;
	*metadataOffset = (u16)offset;
	return 0;
}

static int wfsGetEntryData(const struct wfsFd *fd, struct wfsEntry *entry, u8 metadata[WFS_MAX_ENTRY_SIZE]) {
	int ret = wfsLoadMetadata(&fd->area, fd->metadataBlock, fd->area.blockLog2);
	if (ret)
		return ret;
	memcpy(entry, state.buf + fd->metadataOffset, sizeof(*entry));
	if (entry->metadataLog2Size < 6 || entry->metadataLog2Size > 10 ||
		!wfsRange(fd->metadataOffset, 1u << entry->metadataLog2Size, 1u << fd->area.blockLog2))
		return -EIO;
	memcpy(metadata, state.buf + fd->metadataOffset, 1u << entry->metadataLog2Size);
	return 0;
}

static int wfsDataLocation(const struct wfsFd *fd, u32 blockIndex, struct wfsEntry *entry,
		u8 metadata[WFS_MAX_ENTRY_SIZE], u32 *areaBlock, u8 *blockLog2, u8 hash[WFS_HASH_SIZE]) {
	u32 count, index, clustersPer;
	const u8 *end;
	struct wfsDataBlockMetadata const *single;
	struct wfsClusterMetadata const *cluster;

	if (entry->sizeCategory == WFS_LAYOUT_BLOCKS || entry->sizeCategory == WFS_LAYOUT_LARGE_BLOCKS) {
		count = (entry->sizeOnDisk + ((1u << (fd->area.blockLog2 + (entry->sizeCategory == WFS_LAYOUT_LARGE_BLOCKS ? 3 : 0))) - 1)) >>
			(fd->area.blockLog2 + (entry->sizeCategory == WFS_LAYOUT_LARGE_BLOCKS ? 3 : 0));
		if (blockIndex >= count || count > 5)
			return -EIO;
		end = metadata + (1u << entry->metadataLog2Size);
		single = (const struct wfsDataBlockMetadata *)(end - count * sizeof(*single));
		single += count - 1 - blockIndex;
		*areaBlock = single->blockNumber;
		*blockLog2 = (u8)(fd->area.blockLog2 +
			(entry->sizeCategory == WFS_LAYOUT_LARGE_BLOCKS ? 3u : 0u));
		memcpy(hash, single->hash, WFS_HASH_SIZE);
		return 0;
	}

	if (entry->sizeCategory == WFS_LAYOUT_CLUSTERS || entry->sizeCategory == WFS_LAYOUT_CLUSTER_METADATA) {
		u32 clusters;
		clustersPer = ((1u << fd->area.blockLog2) - sizeof(struct wfsMetadataHeader)) / sizeof(*cluster);
		if (clustersPer > 48)
			clustersPer = 48;
		if (!clustersPer)
			return -EIO;
		clusters = (entry->sizeOnDisk + ((1u << (fd->area.blockLog2 + 6)) - 1)) >>
			(fd->area.blockLog2 + 6);
		if (entry->sizeCategory == WFS_LAYOUT_CLUSTER_METADATA) {
			u32 metaIndex = blockIndex / (8 * clustersPer);
			u32 metaCount = (clusters + clustersPer - 1) / clustersPer;
			u32 metaBlock;
			end = metadata + (1u << entry->metadataLog2Size);
			if (metaIndex >= metaCount || metaCount > 237)
				return -EIO;
			metaBlock = ((const u32 *)(end - metaCount * sizeof(u32)))[metaCount - 1 - metaIndex];
			if (wfsLoadMetadata(&fd->area, metaBlock, fd->area.blockLog2))
				return -EIO;
			cluster = (const struct wfsClusterMetadata *)(state.buf + sizeof(struct wfsMetadataHeader));
			index = blockIndex % (8 * clustersPer);
			cluster += index / 8;
			index %= 8;
		}
		else {
			count = clusters;
			if (blockIndex / 8 >= count || count > 4)
				return -EIO;
			end = metadata + (1u << entry->metadataLog2Size);
			cluster = (const struct wfsClusterMetadata *)(end - count * sizeof(*cluster));
			cluster += count - 1 - (blockIndex / 8);
			index = blockIndex % 8;
		}
		*areaBlock = cluster->blockNumber + index * 8;
		*blockLog2 = fd->area.blockLog2 + 3;
		memcpy(hash, cluster->hash[index], WFS_HASH_SIZE);
		return 0;
	}
	return -EIO;
}

static int wfsOpen(struct filesystem *fs, const char *path) {
	struct wfsArea area;
	struct wfsEntry entry;
	char name[WFS_MAX_PATH];
	const char *p;
	u32 dirBlock, metadataBlock;
	u16 metadataOffset;
	int fd, ret;
	(void)fs;

	if (!path || !state.part)
		return -EINVAL;
	for (fd = 0; fd < WFS_MAX_FILES; fd++)
		if (!state.files[fd].open)
			break;
	if (fd == WFS_MAX_FILES)
		return -EMFILE;

	area = state.rootArea;
	dirBlock = 3;
	p = path;
	while (*p == '/')
		p++;
	if (!*p)
		return -EISDIR;
	while (*p) {
		u32 n = 0;
		bool last;
		while (*p && *p != '/') {
			if (n + 1 >= sizeof(name))
				return -ENAMETOOLONG;
			name[n] = (*p >= 'A' && *p <= 'Z') ? *p + ('a' - 'A') : *p;
			n++; p++;
		}
		name[n] = '\0';
		while (*p == '/')
			p++;
		last = !*p;
		if (!strcmp(name, ".") || !strcmp(name, ".."))
			return -EINVAL;
		ret = wfsFindEntry(&area, dirBlock, name, &entry, &metadataBlock, &metadataOffset);
		if (ret)
			return ret;
		if (!last) {
			if (!(entry.flags & WFS_ENTRY_DIRECTORY) || (entry.flags & WFS_ENTRY_LINK))
				return -ENOTDIR;
			if (entry.flags & WFS_ENTRY_QUOTA) {
				u8 log2 = (entry.flags & WFS_ENTRY_AREA_BASIC) ? WFS_PHYSICAL_LOG2 : WFS_LOGICAL_LOG2;
				ret = wfsLoadArea(&area, entry.directoryBlock, log2, &area);
				if (ret)
					return ret;
				dirBlock = 3;
			}
			else
				dirBlock = entry.directoryBlock;
			continue;
		}
		if (entry.flags & WFS_ENTRY_LINK)
			return -ELOOP;
		if (entry.flags & WFS_ENTRY_DIRECTORY)
			return -EISDIR;
		memset(&state.files[fd], 0, sizeof(state.files[fd]));
		state.files[fd].open = true;
		state.files[fd].area = area;
		state.files[fd].metadataBlock = metadataBlock;
		state.files[fd].metadataOffset = metadataOffset;
		state.files[fd].size = entry.fileSize;
		state.files[fd].sizeOnDisk = entry.sizeOnDisk;
		if (state.files[fd].size > state.files[fd].sizeOnDisk) {
			memset(&state.files[fd], 0, sizeof(state.files[fd]));
			return -EIO;
		}
		return fd;
	}
	return -ENOENT;
}

static ssize_t wfsRead(struct filesystem *fs, int fd, void *dest, size_t len) {
	struct wfsFd *file;
	struct wfsEntry entry;
	u8 metadata[WFS_MAX_ENTRY_SIZE], hash[WFS_HASH_SIZE];
	u32 total = 0;
	int ret;
	(void)fs;
	if (fd < 0 || fd >= WFS_MAX_FILES || !state.files[fd].open)
		return -EBADF;
	if (!dest)
		return -EINVAL;
	file = &state.files[fd];
	if (file->pos >= file->size)
		return 0;
	if (len > file->size - file->pos)
		len = file->size - file->pos;
	ret = wfsGetEntryData(file, &entry, metadata);
	if (ret)
		return ret;
	if (entry.sizeCategory > WFS_LAYOUT_CLUSTER_METADATA)
		return -EIO;

	while (total < len) {
		u32 dataBlock, blockOffset, blockIndex, areaBlock, capacity, chunk;
		u8 blockLog2;
		if (entry.sizeCategory == WFS_LAYOUT_INLINE) {
			u32 start = (1u << entry.metadataLog2Size) - file->sizeOnDisk;
			if (file->sizeOnDisk > (1u << entry.metadataLog2Size) ||
				!wfsRange(start + file->pos, file->size - file->pos,
					1u << entry.metadataLog2Size))
				return total ? (ssize_t)total : -EIO;
			chunk = len - total;
			if (chunk > file->size - file->pos)
				chunk = file->size - file->pos;
			memcpy((u8 *)dest + total, metadata + start + file->pos, chunk);
			file->pos += chunk;
			total += chunk;
			continue;
		}
		blockLog2 = (u8)(file->area.blockLog2 +
			(entry.sizeCategory == WFS_LAYOUT_BLOCKS ? 0u : 3u));
		capacity = 1u << blockLog2;
		blockIndex = file->pos >> blockLog2;
		blockOffset = file->pos & (capacity - 1);
		ret = wfsDataLocation(file, blockIndex, &entry, metadata, &areaBlock, &blockLog2, hash);
		if (ret)
			return total ? (ssize_t)total : ret;
		capacity = 1u << blockLog2;
		dataBlock = capacity;
		if (dataBlock > file->size - (blockIndex * capacity))
			dataBlock = file->size - (blockIndex * capacity);
		dataBlock = (dataBlock + 511) & ~511u;
		if (!dataBlock || dataBlock > capacity)
			return total ? (ssize_t)total : -EIO;
		ret = wfsReadBlock(&file->area, areaBlock, blockLog2, dataBlock,
			!(entry.flags & WFS_ENTRY_UNENCRYPTED), hash, false);
		if (ret)
			return total ? (ssize_t)total : ret;
		chunk = capacity - blockOffset;
		if (chunk > len - total)
			chunk = len - total;
		if (chunk > file->size - file->pos)
			chunk = file->size - file->pos;
		memcpy((u8 *)dest + total, state.buf + blockOffset, chunk);
		file->pos += chunk;
		total += chunk;
	}
	return (ssize_t)total;
}

static ssize_t wfsSeek(struct filesystem *fs, int fd, ssize_t off) {
	(void)fs;
	if (fd < 0 || fd >= WFS_MAX_FILES || !state.files[fd].open)
		return -EBADF;
	if (off < 0 || (u32)off > state.files[fd].size)
		return -1;
	state.files[fd].pos = (u32)off;
	return off;
}

static void wfsClose(struct filesystem *fs, int fd) {
	(void)fs;
	if (fd >= 0 && fd < WFS_MAX_FILES)
		state.files[fd].open = false;
}

static ssize_t wfsGetSize(struct filesystem *fs, int fd) {
	(void)fs;
	if (fd < 0 || fd >= WFS_MAX_FILES || !state.files[fd].open)
		return -EBADF;
	return (ssize_t)state.files[fd].size;
}

/*
 * eMMC full SEC_COUNT -> WFS MLC device sector count (the value IOSU baked into
 * the block IVs at format time). These are firmware constants per MLC size.
 */
static u32 wfsMLCDeviceSectors(u32 emmcSectors) {
	switch (emmcSectors) {
	case 0x03b34000: /* 32 GB Wii U */
		return 0x03a20000;
	/* TODO: 8 GB Wii U (eMMC SEC_COUNT and WFS device size not yet known). */
	default:
		log_printf("wfs: unknown eMMC size %08x; WFS IV may be wrong\r\n", emmcSectors);
		return emmcSectors;
	}
}

static bool wfsProbe(struct filesystem *fs, struct partition *part) {
	(void)fs;
	if (H_ConsoleType != CONSOLE_TYPE_WII_U || !part || part->bdev->blockSize != 512)
		return false;
	/* A later standard-device probe must not disturb a live WFS mount. */
	if (state.part)
		return state.part == part;
	memset(&state, 0, sizeof(state));
	state.part = part;
	/*
	 * WFS bakes the MLC "device sector count" into every block's AES IV
	 * (iv[2]). IOSU does not use the raw eMMC SEC_COUNT here; it uses the
	 * smaller, fixed usable-MLC size it carves out (the tail is reserved).
	 * Map the eMMC capacity we read to that constant.
	 */
	state.sectors = wfsMLCDeviceSectors((u32)(part->bdev->size / part->bdev->blockSize));
	state.buf = M_PoolAlloc(POOL_ANY, WFS_MAX_BLOCK_SIZE + 64, 64);
	memcpy(state.key, H_OTPContents.wiiu.bank3.mlcNANDKey, sizeof(state.key));
	/* MLC is encrypted; USB has an explicit future key path and is not mounted now. */
	if (!wfsValidateRoot(WFS_PHYSICAL_LOG2, true)) {
		log_puts("encrypted physical root probe failed");
		if (wfsValidateRoot(WFS_LOGICAL_LOG2, true))
			goto found;
		log_puts("encrypted logical root probe failed");
		free(state.buf);
		memset(&state, 0, sizeof(state));
		return false;
	}
found:
	return true;
}

static int wfsMount(struct filesystem *fs, struct partition *part) {
	(void)fs;
	if (state.part != part || !state.buf || !state.valid)
		return -ENODEV;
	memset(state.files, 0, sizeof(state.files));
	return 0;
}

static void wfsUnmount(struct filesystem *fs) {
	(void)fs;
	if (state.buf)
		free(state.buf);
	memset(&state, 0, sizeof(state));
}

struct filesystem FS_WFS = {
	.name = "WFS",
	.drvData = NULL,
	.probe = wfsProbe,
	.mount = wfsMount,
	.unmount = wfsUnmount,
	.open = wfsOpen,
	.close = wfsClose,
	.read = wfsRead,
	.seek = wfsSeek,
	.getSize = wfsGetSize,
	.flagMask = BLOCK_FLAG_STANDARD
};
