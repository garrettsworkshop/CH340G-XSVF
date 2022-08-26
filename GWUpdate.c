/*
 *  Lib(X)SVF  -  A library for implementing SVF and XSVF JTAG players
 *
 *  Copyright (C) 2009  RIEGL Research ForschungsGmbH
 *  Copyright (C) 2009  Clifford Wolf <clifford@clifford.at>
 *
 *  Permission to use, copy, modify, and/or distribute this software for any
 *  purpose with or without fee is hereby granted, provided that the above
 *  copyright notice and this permission notice appear in all copies.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 */

#include "libxsvf.h"
#include "comsearch.h"

#include <Windows.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include "CH340G-HAL.h"
#include "CH340G-time.h"
#include "CH340G-quit.h"

uint8_t expected_devices;
uint32_t expected_idcode;
uint8_t found_devices;
uint32_t found_idcode;

struct udata_s {
	FILE* f;
	int clockcount;
	int bitcount_tdi;
	int bitcount_tdo;
};

unsigned char tck_queue = 0;

static void flush_tck() {
	io_tck(tck_queue);
	tck_queue = 0;
}

static int h_setup(struct libxsvf_host* h)
{
	struct udata_s* u = h->user_data;
	fprintf(stderr, "[SETUP]\n");
	fflush(stderr);
	tck_queue = 0;
	io_setup();
	return 0;
}

static int h_shutdown(struct libxsvf_host* h)
{
	struct udata_s* u = h->user_data;
	fprintf(stderr, "[SHUTDOWN]\n");
	fflush(stderr);
	flush_tck();
	io_shutdown();
	return 0;
}

static void h_udelay(struct libxsvf_host* h, long usecs, int tms, long num_tck)
{
	struct udata_s* u = h->user_data;
	fprintf(stderr, "[DELAY:%ld, TMS:%d, NUM_TCK:%ld]\n", usecs, tms, num_tck);
	fflush(stderr);
	if (num_tck > 0) {
		io_tms(tms);
		while (num_tck > 255) {
			io_tck(255);
			num_tck -= 255;
		}
		io_tck((unsigned char)num_tck);
		fprintf(stderr, "[DELAY_AFTER_TCK:%ld]\n", usecs > 0 ? usecs : 0);
		fflush(stderr);
	}
	if (usecs > 0) { Sleep((usecs + 999) / 1000); }
}

static int h_getbyte(struct libxsvf_host* h)
{
	struct udata_s* u = h->user_data;
	return fgetc(u->f);
}

static int h_set_frequency(struct libxsvf_host* h, int v) { return 0; }

static void h_report_tapstate(struct libxsvf_host* h)
{
	struct udata_s* u = h->user_data;
	fprintf(stderr, "[%s]\n", libxsvf_state2str(h->tap_state));
}

static void h_report_device(struct libxsvf_host* h, unsigned long idcode)
{
	printf("Found device on JTAG chain. IDCODE=0x%08lx, REV=0x%01lx, PART=0x%04lx, MFR=0x%03lx\n", 
		idcode, (idcode >> 28) & 0xf, (idcode >> 12) & 0xffff, (idcode >> 1) & 0x7ff);

	found_devices++;
	found_idcode = idcode;
}

static void h_report_status(struct libxsvf_host* h, const char* message)
{
	struct udata_s* u = h->user_data;
	fprintf(stderr, "[STATUS] %s\n", message);
}

static void h_report_error(struct libxsvf_host* h, const char* file, int line, const char* message)
{
	fprintf(stderr, "[%s:%d] %s\n", file, line, message);
}

static int realloc_maxsize[LIBXSVF_MEM_NUM];

static void* h_realloc(struct libxsvf_host* h, void* ptr, int size, enum libxsvf_mem which)
{
	struct udata_s* u = h->user_data;
	if (size > realloc_maxsize[which]) { realloc_maxsize[which] = size; }
	return realloc(ptr, size);
}

int tms_old = -1;
int tdi_old = -1;
static int h_pulse_tck(struct libxsvf_host* h, int tms, int tdi, int tdo, int rmask, int sync)
{
	struct udata_s* u = h->user_data;

	if (!sync && tdo < 0 && tms == tms_old && (tdi == tdi_old || tdi < 0) && tck_queue < 255) {
		if (tdi <= 0) { u->bitcount_tdi++; }
		u->clockcount++;
		tck_queue++;
		return 1;
	}
	else {
		if (tck_queue > 0) {
			Gate1ms();
			flush_tck();
			if (tms != tms_old || (tdi >= 0 && tdi != tdi_old)) { Wait1ms(); }
		}

		if (tms != tms_old) {
			io_tms(tms);
			tms_old = tms;
		}
		if (tdi >= 0) {
			u->bitcount_tdi++;
			if (tdi != tdi_old) {
				io_tdi(tdi);
				tdi_old = tdi;
			}
		}
		if (tms != tms_old || (tdi >= 0 && tdi != tdi_old)) { SetGateTime(); }

		u->clockcount++;

		if (!sync && tdo < 0) {
			tck_queue++;
			return 1;
		}
		else {
			u->bitcount_tdo++;
			Gate1ms();
			io_tck(1);
			Wait1ms();
			int line_tdo = io_tdo();
			return tdo < 0 || line_tdo == tdo ? line_tdo : -1;
		}
	}
}

static struct udata_s u;

static struct libxsvf_host h = {
	.udelay = h_udelay,
	.setup = h_setup,
	.shutdown = h_shutdown,
	.getbyte = h_getbyte,
	.pulse_tck = h_pulse_tck,
	.pulse_sck = NULL,
	.set_trst = NULL,
	.set_frequency = h_set_frequency,
	.report_tapstate = h_report_tapstate,
	.report_device = h_report_device,
	.report_status = h_report_status,
	.report_error = h_report_error,
	.realloc = h_realloc,
	.user_data = &u
};

static void copyleft()
{
	fprintf(stderr, "GWUpdate\n");
	fprintf(stderr, "Copyright (C) 2022 Garrett's Workshop\n");
	fprintf(stderr, "Based on xsvftool-gpio, part of Lib(X)SVF (http://www.clifford.at/libxsvf/).\n");
	fprintf(stderr, "Copyright (C) 2009  RIEGL Research ForschungsGmbH\n");
	fprintf(stderr, "Copyright (C) 2009  Clifford Wolf <clifford@clifford.at>\n");
	fprintf(stderr, "Lib(X)SVF is free software licensed under the ISC license.\n");
	fprintf(stderr, "GWUpdate is free software licensed under the ISC license.\n");
}

#define STRBUF_SIZE (68 * 1024)
char strbuf[STRBUF_SIZE + 1];

int main(int argc, char** argv)
{
	enum libxsvf_mode mode;
	int portnum;

	copyleft();

	if (argc != 1) {
		fprintf(stderr, "Error! Bad arguments.\n");
		return quit(-1);
	}

	u.f = fopen(argv[0], "rb");
	if (!u.f) {
		fprintf(stderr, "Error! Failed to open GWUpdate executable as data file.\n");
		return quit(-1);
	}

	// Find XSVF data
	mode = LIBXSVF_MODE_SVF; // Default to SVF mode
	for (int i = 1; ; i++) { // Search at each 128k offset for SVF/XSVF flag
		char c;
		if (i > 20) { // Looked too many times fail
			fprintf(stderr, "Error! (X)SVF flag not found.\n");
			return quit(-1);
		}
		if (fseek(u.f, i * 128 * 1024, SEEK_SET)) { // Seek past end of file fail
			fprintf(stderr, "Error! Seeked past end of file looking for (X)SVF flag.\n");
			return quit(-1);
		}
		
		c = getchar(u.f);
		if (c == 'X') { // First 'X'
			mode = LIBXSVF_MODE_XSVF;
			// Then 'S', else try next 128k
			if (getchar(u.f) != 'S') { break; }
		}
		else if (c != 'S') { break; } // First 'S'

		// Then 'V', then 'F', else try next 128k
		if (getchar(u.f) != 'V') { break; }
		if (getchar(u.f) != 'F') { break; }
	}

	// Ensure update file indicates only one device on JTAG chain
	if (expected_devices = fgetc(u.f) != 1) { // So check next char == 1
		fprintf(stderr, "Error! Update file has multiple devices on JTAG chain but GWUpdate only supports one device.\n");
		return quit(-1);
	}
	found_devices = 0;

	// Read single expected IDCODE
	if (fread(&expected_idcode, sizeof(uint32_t), 1, u.f) != 1) { // Couldn't read idcode
		fprintf(stderr, "Error! Couldn't read JTAG idcode from file.\n");
		return quit(-1);
	}

	// Print instructions following (X)SVF flag.
	if (!strbuf) { // Malloc fail
		fprintf(stderr, "Error! Failed to allocate string buffer.\n");
		return quit(-1);
	}
	if (!fgets(strbuf, STRBUF_SIZE, u.f)) { // Read instructions string into buffer
		fprintf(stderr, "Error! Failed to read update instructions from file.\n");
		return quit(-1);
	}
	if (!puts(strbuf)) { // Display instructions
		fprintf(stderr, "Error! Failed to display update instructions.\n");
		return quit(-1);
	}

	// Find COM port
	if ((portnum = comsearch(portname)) > 0) {
		fprintf(stderr, "Error! Could not find USB device.\n");
		return quit(-1);
	}

	// Start elapsed time timer
	Setup1msTicks();
	LONGLONG start = GetTicksNow();

	// Scan JTAG chain
	if (libxsvf_play(&h, LIBXSVF_MODE_SCAN) < 0) {
		fprintf(stderr, "Error! Failed to scan JTAG chain.\n");
		return quit(-1);
	}

	// Check for expected IDCODE
	if (expected_idcode != found_idcode) {
		fprintf(stderr, "Error! Incorrect device found on JTAG chain.\n");
		return quit(-1);
	}

	// Play update (X)SVF
	if (libxsvf_play(&h, mode) < 0) {
		fprintf(stderr, "Error! Failed to play (X)SVF.\n");
	}

	LONGLONG end = GetTicksNow() - start;
	double elapsed = (double)end / ticks_per_ms / 1000.0f;

	// Close file
	fclose(u.f);

	fprintf(stderr, "Done.\n");
	fprintf(stderr, "Total number of clock cycles: %d\n", u.clockcount);
	fprintf(stderr, "Number of significant TDI bits: %d\n", u.bitcount_tdi);
	fprintf(stderr, "Number of significant TDO bits: %d\n", u.bitcount_tdo);
	fprintf(stderr, "Time elapsed: %lf sec.\n", elapsed);
	fprintf(stderr, "Speed: %lf bits / sec.\n", (double)u.clockcount / elapsed);
	return quit(0);
}
