/*
 * NPLL - Wii U MLC SCFM (SLC-resident block write cache) overlay
 *
 * The live MLC contents = raw MLC blocks + an SCFM overlay stored (unencrypted)
 * on the SLC. Reading the MLC alone gives a stale view. This module holds the
 * whole SCFM image in RAM and substitutes cached sectors for stale MLC ones.
 *
 * Copyright (C) 2026 Techflash
 */

#ifndef _SCFM_H
#define _SCFM_H

#include <npll/block.h>
#include <npll/types.h>

/*
 * Read the SCFM metadata from the currently-mounted SLC SFFS and capture
 * scfm.img's on-disk layout, so the payload can be read on demand afterward.
 * The caller must have the SLC SFFS mounted and must call this before the MLC
 * WFS is used. Returns 0 on success, negative errno otherwise.
 */
int S_SCFMLoadFile(const char *path);

/* Drop the overlay and free the resident metadata + captured layout. */
void S_SCFMClear(void);

/* Whether an SCFM overlay is currently installed. */
bool S_SCFMActive(void);

/* Whether the region covering device byte offset `off` is SCFM-cached (live). */
bool S_SCFMCached(u64 off);

/*
 * Read `len` bytes at device byte offset `off` from `bdev`, transparently
 * substituting SCFM-cached sectors for stale MLC ones. `off` and `len` must be
 * multiples of 512. With no overlay installed this is a plain block read.
 * Returns bytes read, or negative errno.
 */
ssize_t S_SCFMReadMLC(struct blockDevice *bdev, void *buf, u64 off, size_t len);

#endif /* _SCFM_H */
