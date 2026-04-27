/*
 *	Copyright (C) 2008 dhewg
 *
 *	this file is part of geckoloader
 *	http://wiibrew.org/index.php?title=Geckoloader
 *
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2 of the License, or
 *	(at your option) any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <arpa/inet.h>

#include "gecko.h"

#define MAX_ARGS_LEN 1024
#define GECKO_LOAD_BLOCK_SIZE_DEFAULT (16 * 1024)
#define GECKO_LOAD_BLOCK_SIZE_MAX (64 * 1024)
#define GECKO_ACK_TIMEOUT_MS 5000
#define GECKO_PROGRESS_INTERVAL (256 * 1024)
#define GECKO_SEND_DELAY_US_DEFAULT 0

#ifndef O_BINARY
#define O_BINARY 0
#endif

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;
typedef signed char s8;
typedef signed short s16;
typedef signed int s32;
typedef signed long long s64;

#ifndef __WIN32__
static const char *desc_export = "export";
#ifndef __APPLE__
static const char *desc_gecko = "/dev/ttyUSB0";
#else
static const char *desc_gecko = "/dev/tty.usbserial-GECKUSB0";
#endif
#else
static const char *desc_export = "set";
static const char *desc_gecko = "COM4";
#endif

static const char *envvar = "GECKOLOAD";
static const char *delay_envvar = "GECKOLOAD_DELAY_US";
static const char *block_envvar = "GECKOLOAD_BLOCK_SIZE";

static bool wait_gecko_ack(void) {
	static const u8 ack = 0x06;
	u8 b;
	int ret;

	while (true) {
		ret = gecko_read_timeout(&b, 1, GECKO_ACK_TIMEOUT_MS);
		if (ret == 2)
			fprintf(stderr, "timed out waiting for target acknowledgement\n");
		if (ret)
			return false;

		if (b == ack)
			return true;
	}
}

static bool send_gecko (const char *dev, const u8 *buf, u32 len) {
	u8 b[12];
	u32 net_len;
	u32 left, block;
	const u8 *p;
	const char *delay_env;
	const char *block_env;
	u32 send_delay_us = GECKO_SEND_DELAY_US_DEFAULT;
	u32 block_size = GECKO_LOAD_BLOCK_SIZE_DEFAULT;
	bool ok = true;

	if (gecko_open (dev)) {
		fprintf (stderr, "unable to open the device '%s'\n", dev);
		return false;
	}

	printf ("sending upload request\n");

	memcpy(&b[0], "NPLLBIN\x7f", 8);

	if (gecko_write (b, 8)) {
		gecko_close ();
		fprintf (stderr, "error sending data\n");
		return false;
	}

	/* write length */
	printf ("sending file size (%u bytes)\n", len);
	net_len = htonl(len);
	memcpy(b, &net_len, sizeof(net_len));

	if (gecko_write (b, 4)) {
		gecko_close ();
		fprintf (stderr, "error sending data\n");
		return false;
	}

	printf ("waiting for NPLL to allocate the receive buffer\n");
	if (!wait_gecko_ack()) {
		gecko_close ();
		fprintf (stderr, "error waiting for target acknowledgement\n");
		return false;
	}

	delay_env = getenv(delay_envvar);
	if (delay_env)
		send_delay_us = strtoul(delay_env, NULL, 0);

	block_env = getenv(block_envvar);
	if (block_env) {
		block_size = strtoul(block_env, NULL, 0);
		if (!block_size)
			block_size = 1;
		if (block_size > GECKO_LOAD_BLOCK_SIZE_MAX)
			block_size = GECKO_LOAD_BLOCK_SIZE_MAX;
	}

	printf ("using block size %u bytes", block_size);
	if (send_delay_us)
		printf (", delay %u us", send_delay_us);
	printf ("\n");

	printf ("sending data");
	fflush (stdout);

	left = len;
	p = buf;
	while (left) {
		block = left;
		if (block > block_size)
			block = block_size;
		left -= block;

		if (gecko_write (p, block)) {
			fprintf (stderr, "error sending block\n");
			ok = false;
			break;
		}
		p += block;

		if (send_delay_us)
			usleep(send_delay_us);

		if (((u32)(p - buf) % GECKO_PROGRESS_INTERVAL) == 0) {
			printf (".");
			fflush (stdout);
		}

	}
	printf ("\n");

	if (ok) {
		if (gecko_drain()) {
			gecko_close ();
			return false;
		}

		printf ("waiting for NPLL to finish receiving\n");
		if (!wait_gecko_ack()) {
			fprintf (stderr, "error waiting for final target acknowledgement\n");
			ok = false;
		}
	}

	gecko_close ();

	return ok;
}

static void usage (const char *argv0) {
	fprintf (stderr, "set the environment variable %s to a valid "
				"destination.\n\n"
				"examples:\n"
				"\tusbgecko mode:\n"
				"\t\t%s %s=%s\n\n"
				"usage:\n"
				"\t%s <filename>\n\n",
				envvar,
				desc_export, envvar, desc_gecko,
				argv0);
	exit (EXIT_FAILURE);
}

int main (int argc, char **argv) {
	int fd;
	struct stat st;
	char *ev;
	u8 *buf;
	off_t fsize;
	u32 len;
	bool res;

	puts("geckoload for NPLL by Techflash, based on wiiload v0.5.1 coded by dhewg");

	if (argc < 2)
		usage (*argv);

	ev = getenv (envvar);
	if (!ev)
		usage (*argv);

	fd = open (argv[1], O_RDONLY | O_BINARY);
	if (fd < 0) {
		perror ("error opening the file");
		exit (EXIT_FAILURE);
	}

	if (fstat (fd, &st)) {
		close (fd);
		perror ("error stat'ing the file");
		exit (EXIT_FAILURE);
	}

	fsize = st.st_size;

	buf = malloc (fsize);
	if (!buf) {
		close (fd);
		fprintf (stderr, "out of memory\n");
		exit (EXIT_FAILURE);
	}

	if (read (fd, buf, fsize) != fsize) {
		close (fd);
		free (buf);
		perror ("error reading the file");
		exit (EXIT_FAILURE);
	}
	close (fd);

	len = fsize;
#ifndef __WIN32__	// stat call fails on some windows installations for com ports
	if (stat (ev, &st))
		usage (*argv);
#endif
	res = send_gecko (ev, buf, len);

	if (res)
		printf ("done.\n");
	else
		printf ("transfer failed.\n");

	free (buf);

	return res ? 0 : 1;
}
