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

#include <Windows.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include "CH340G-HAL.h"
#include "CH340G-time.h"
#include "CH340G-quit.h"

struct udata_s {
	FILE* f;
	int clockcount;
	int bitcount_tdi;
	int bitcount_tdo;
	int sendcount;
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

	u->clockcount++;
	if (tdi >= 0) { u->bitcount_tdi++; }

	if (!sync && tdo < 0 && tms == tms_old && (tdi == tdi_old || tdi < 0) && tck_queue < 255) {
		tck_queue++;
		return 1;
	}
	else {
		if (tck_queue > 0) {
			Gate();
			SetGate();
			flush_tck();
			u->sendcount++;
			if (tms != tms_old || (tdi >= 0 && tdi != tdi_old)) { Gate(); }
		}

		if (tms != tms_old) {
			if (tdi < 0 || tdi == tdi_old) { SetGate(); }
			io_tms(tms);
			tms_old = tms;
		}
		if (tdi >= 0) {
			if (tdi != tdi_old) {
				SetGate();
				io_tdi(tdi);
				tdi_old = tdi;
			}
		}

		if (!sync && tdo < 0) {
			tck_queue++;
			return 1;
		}
		else {
			if (tdo >= 0) { u->bitcount_tdo++; }
			Gate();
			SetGate();
			io_tck(1);
			u->sendcount++;
			Gate();
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
	fprintf(stderr, "Garrett's Workshop (X)SVF Player\n");
	fprintf(stderr, "Copyright (C) 2022 Garrett's Workshop\n");
	fprintf(stderr, "Based on xsvftool-gpio, part of Lib(X)SVF (http://www.clifford.at/libxsvf/).\n");
	fprintf(stderr, "Copyright (C) 2009  RIEGL Research ForschungsGmbH\n");
	fprintf(stderr, "Copyright (C) 2009  Clifford Wolf <clifford@clifford.at>\n");
	fprintf(stderr, "Lib(X)SVF is free software licensed under the ISC license.\n");
	fprintf(stderr, "Garrett's Workshop XSVF Player is free software licensed under the ISC license.\n");
}

int main(int argc, char** argv)
{
	char* filename;
	size_t filename_len;
	enum libxsvf_mode mode;
	copyleft();

	if (argc != 3) {
		portname = "COM3";
		filename = "REU_impl1_XFLASH_CFG_VFY.xsvf";
		fprintf(stderr, "Bad arguments.\n");
		fprintf(stderr, "Usage: %s <COM_port> <(X)SVF_file>\n", argv[0]);
		fprintf(stderr, "Continuing with standard args: %s %s %s\n", argv[0], portname, filename);
		//quit(-1);
	}
	else {
		portname = argv[1];
		filename = argv[2];
	}
	filename_len = strlen(filename);

	if (!_stricmp(&filename[filename_len - 4], ".svf")) {
		mode = LIBXSVF_MODE_SVF;
	}
	else if (!_stricmp(&filename[filename_len - 5], ".xsvf")) {
		mode = LIBXSVF_MODE_XSVF;
	}
	else {
		fprintf(stderr, "Unknown extension on input file %s. \n", filename);
		fprintf(stderr, "Input file must end in .svf or .xsvf");
		return quit(-1);
	}

	SetupTicks();
	LONGLONG start = GetTicksNow();

	if (libxsvf_play(&h, LIBXSVF_MODE_SCAN) < 0) {
		fprintf(stderr, "Error while scanning JTAG chain.\n");
		return quit(-1);
	}

	u.f = fopen(filename, "rb");
	if (u.f == NULL) {
		fprintf(stderr, "Can't open file %s.\n", filename);
		return quit(-1);
	}

	if (libxsvf_play(&h, mode) < 0) {
		fprintf(stderr, "Error while playing XSVF file.\n");
	}

	fclose(u.f);

	LONGLONG end = GetTicksNow() - start;
	double elapsed = (double)end / ticks_per_ms / 1000.0f;

	fprintf(stderr, "Done.\n");
	fprintf(stderr, "Total number of clock cycles: %d\n", u.clockcount);
	fprintf(stderr, "Number of significant TDI bits: %d\n", u.bitcount_tdi);
	fprintf(stderr, "Number of significant TDO bits: %d\n", u.bitcount_tdo);
	fprintf(stderr, "Number of TCK pulsetrains: %d\n", u.sendcount);
	fprintf(stderr, "Time elapsed: %lf sec.\n", elapsed);
	fprintf(stderr, "Speed: %lf bits / sec.\n", (double)u.clockcount / elapsed);
	return quit(0);
}
