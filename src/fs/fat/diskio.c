/*
 * NPLL - Filesystems - FAT - Disk I/O Hooks
 * Copyright (C) 2026 Techflash
 */

/*-----------------------------------------------------------------------*/
/* Low level disk I/O module SKELETON for FatFs     (C)ChaN, 2025        */
/*-----------------------------------------------------------------------*/
/* If a working storage control module is available, it should be        */
/* attached to the FatFs via a glue function rather than modifying it.   */
/* This is an example of glue functions to attach various exsisting      */
/* storage control modules to the FatFs module with a defined API.       */
/*-----------------------------------------------------------------------*/

#define MODULE "fat"
#include "ff.h"			/* Basic definitions of FatFs */
#include "diskio.h"		/* Declarations FatFs MAI */

#include <npll/block.h>
#include <npll/log.h>
#include <npll/partition.h>

struct partition *partitions[FF_VOLUMES] = { NULL };
const char *VolumeStr[FF_VOLUMES] = { NULL };

/*-----------------------------------------------------------------------*/
/* Get Drive Status                                                      */
/*-----------------------------------------------------------------------*/

DSTATUS disk_status (
	BYTE pdrv		/* Physical drive nmuber to identify the drive */
)
{
	if (!partitions[pdrv])
		return STA_NOINIT;

	if (!partitions[pdrv]->bdev->write)
		return STA_PROTECT;

	return 0;
}



/*-----------------------------------------------------------------------*/
/* Inidialize a Drive                                                    */
/*-----------------------------------------------------------------------*/

DSTATUS disk_initialize (
	BYTE pdrv				/* Physical drive nmuber to identify the drive */
)
{
	if (!partitions[pdrv])
		return STA_NOINIT;

	return 0;
}



/*-----------------------------------------------------------------------*/
/* Read Sector(s)                                                        */
/*-----------------------------------------------------------------------*/

DRESULT disk_read (
	BYTE pdrv,		/* Physical drive nmuber to identify the drive */
	BYTE *buff,		/* Data buffer to store read data */
	LBA_t sector,	/* Start sector in LBA */
	UINT count		/* Number of sectors to read */
)
{
	ssize_t result;
	u64 blocksz;

	if (!partitions[pdrv])
		return RES_PARERR;

	blocksz = partitions[pdrv]->bdev->blockSize;
	result = B_Read(partitions[pdrv], buff, count * blocksz, sector * blocksz);
	if (result != (ssize_t)(count * blocksz)) {
		log_printf("B_Read ret %d\r\n", result);
		return RES_ERROR;
	}

	return RES_OK;
}



/*-----------------------------------------------------------------------*/
/* Write Sector(s)                                                       */
/*-----------------------------------------------------------------------*/

#if FF_FS_READONLY == 0

DRESULT disk_write (
	BYTE pdrv,			/* Physical drive nmuber to identify the drive */
	const BYTE *buff,	/* Data to be written */
	LBA_t sector,		/* Start sector in LBA */
	UINT count			/* Number of sectors to write */
)
{
	ssize_t result;
	u64 blocksz;

	if (!partitions[pdrv])
		return RES_PARERR;

	blocksz = partitions[pdrv]->bdev->blockSize;
	result = B_Write(partitions[pdrv], buff, count * blocksz, sector * blocksz);
	if (result != (ssize_t)(count * blocksz))
		return RES_ERROR;

	return RES_OK;
}

#endif


/*-----------------------------------------------------------------------*/
/* Miscellaneous Functions                                               */
/*-----------------------------------------------------------------------*/

DRESULT disk_ioctl (
	BYTE pdrv,		/* Physical drive nmuber (0..) */
	BYTE cmd,		/* Control code */
	void *buff		/* Buffer to send/receive control data */
)
{
	if (!partitions[pdrv])
		return RES_PARERR;

	switch (cmd) {
	case CTRL_SYNC:
		return RES_OK; /* all operations are synchronous anyways */
	case GET_SECTOR_COUNT: {
		*(UINT *)buff = partitions[pdrv]->bdev->size / partitions[pdrv]->bdev->blockSize;
		return RES_OK;
	}
	case GET_SECTOR_SIZE:
	case GET_BLOCK_SIZE: {
		*(UINT *)buff = partitions[pdrv]->bdev->blockSize;
		return RES_OK;
	}
	case CTRL_TRIM:
	default:
		return RES_PARERR; /* not supported */
	}
}
