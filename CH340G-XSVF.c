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

char* com_port_name = NULL;
char* file_name = NULL;
HANDLE serialport = NULL;

static LONGLONG GetTicksNow() {
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return now.QuadPart;
}

LONGLONG ticks_per_ms;
char freq_set = 0;
static LONGLONG Get1msTicks() {
	if (!freq_set) {
		LARGE_INTEGER ticks_per_sec;
		QueryPerformanceFrequency(&ticks_per_sec);
		ticks_per_ms = (ticks_per_sec.QuadPart + 999) / 1000;
		freq_set = 1;
	}
	return ticks_per_ms;
}

LONGLONG last;
char last_set = 0;
static void Gate1ms() {
	if (!last_set) {
		last = GetTicksNow();
		last_set = 1;
	}
	LONGLONG now;
	LONGLONG end = last + Get1msTicks() + 10;
	do {
		now = GetTicksNow();
	} while (now < end);

	last = GetTicksNow();
}

int oldtms = -1;
static void io_tms(int val)
{
	if (val != oldtms) {
		if (!EscapeCommFunction(serialport, val ? CLRRTS : SETRTS)) {
			fprintf(stderr, "Error writing to %s!\n", com_port_name);
			exit(-1);
		}
		oldtms = val;
	}
}

int oldtdi = -1;
static void io_tdi(int val)
{
	if (val != oldtdi) {
		if (!EscapeCommFunction(serialport, val ? CLRDTR : SETDTR)) {
			fprintf(stderr, "Error writing to %s!\n", com_port_name);
			exit(-1);
		}
		oldtdi = val;
	}
}

static void io_tck_negpulse()
{
	char c = 0x00;
	int written;
	if (!WriteFile(serialport, &c, 1, &written, NULL)) {
		fprintf(stderr, "Error writing to %s!\n", com_port_name);
		exit(-1);
	}
	Gate1ms();
}

static void io_setup(void)
{
	char name[100] = { 0 };
	char root[] = "\\\\.\\\0";
	memcpy(name, root, strlen(root));
	memcpy(name + strlen(root), com_port_name, strlen(com_port_name));

	serialport = CreateFileA(
		name,							// port name
		GENERIC_READ | GENERIC_WRITE,	// Read/Write
		0,								// No Sharing
		NULL,							// No Security
		OPEN_EXISTING,					// Open existing port only
		0,								// Non Overlapped I/O
		NULL);							// Null for Comm Devices

	if (serialport == INVALID_HANDLE_VALUE) { goto error; }

	DCB dcb;
	SecureZeroMemory(&dcb, sizeof(DCB));
	dcb.DCBlength = sizeof(DCB);

	if (!GetCommState(serialport, &dcb)) { goto error; }
	dcb.BaudRate = CBR_38400;
	dcb.fBinary = TRUE;
	dcb.fParity = FALSE;
	dcb.fOutxCtsFlow = FALSE;
	dcb.fOutxDsrFlow = FALSE;
	dcb.fDtrControl = DTR_CONTROL_DISABLE;
	dcb.fDsrSensitivity = FALSE;
	dcb.fTXContinueOnXoff = TRUE;
	dcb.fOutX = FALSE;
	dcb.fInX = FALSE;
	dcb.fNull = FALSE;
	dcb.fRtsControl = RTS_CONTROL_DISABLE;
	dcb.fAbortOnError = TRUE;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;
	if (!SetCommState(serialport, &dcb)) { goto error; }
	return;

	error:
	fprintf(stderr, "Error opening %s!\n", com_port_name);
	if (serialport != INVALID_HANDLE_VALUE) { CloseHandle(serialport); }
	exit(-1);
	return;
}

static void io_shutdown(void)
{
	CloseHandle(serialport);
}

static int io_tdo()
{
	DWORD status;
	int success = GetCommModemStatus(serialport, &status);
	int tdo = (status & MS_CTS_ON) ? 0 : 1;
	return tdo;
}

struct udata_s {
	FILE* f;
	int verbose;
	int clockcount;
	int bitcount_tdi;
	int bitcount_tdo;
	int retval_i;
	int retval[256];
};

static int h_setup(struct libxsvf_host* h)
{
	struct udata_s* u = h->user_data;
	if (u->verbose >= 2) {
		fprintf(stderr, "[SETUP]\n");
		fflush(stderr);
	}
	io_setup();
	return 0;
}

static int h_shutdown(struct libxsvf_host* h)
{
	struct udata_s* u = h->user_data;
	if (u->verbose >= 2) {
		fprintf(stderr, "[SHUTDOWN]\n");
		fflush(stderr);
	}
	io_shutdown();
	return 0;
}

static void h_udelay(struct libxsvf_host* h, long usecs, int tms, long num_tck)
{
	struct udata_s* u = h->user_data;
	if (u->verbose >= 3) {
		fprintf(stderr, "[DELAY:%ld, TMS:%d, NUM_TCK:%ld]\n", usecs, tms, num_tck);
		fflush(stderr);
	}
	if (num_tck > 0) {
		io_tms(tms);
		while (num_tck > 0) {
			io_tck_negpulse();
			num_tck--;
		}
		if (u->verbose >= 3) {
			fprintf(stderr, "[DELAY_AFTER_TCK:%ld]\n", usecs > 0 ? usecs : 0);
			fflush(stderr);
		}
	}
	if (usecs > 0) {
		Sleep((usecs + 999) / 1000);
	}
}

static int h_getbyte(struct libxsvf_host* h)
{
	struct udata_s* u = h->user_data;
	return fgetc(u->f);
}

static int h_pulse_tck(struct libxsvf_host* h, int tms, int tdi, int tdo, int rmask, int sync)
{
	struct udata_s* u = h->user_data;

	if (tdi >= 0) {
		u->bitcount_tdi++;
		io_tdi(tdi);
	}
	io_tms(tms);

	io_tck_negpulse();

	int line_tdo = io_tdo();
	int rc = line_tdo >= 0 ? line_tdo : 0;

	if (rmask == 1 && u->retval_i < 256)
		u->retval[u->retval_i++] = line_tdo;

	if (tdo >= 0 && line_tdo >= 0) {
		u->bitcount_tdo++;
		if (tdo != line_tdo)
			rc = -1;
	}

	if (u->verbose >= 4) {
		fprintf(stderr, "[TMS:%d, TDI:%d, TDO_ARG:%d, TDO_LINE:%d, RMASK:%d, RC:%d]\n", tms, tdi, tdo, line_tdo, rmask, rc);
	}

	u->clockcount++;
	return rc;
}

static int h_set_frequency(struct libxsvf_host* h, int v)
{
	fprintf(stderr, "WARNING: Setting JTAG clock frequency to %d ignored!\n", v);
	return 0;
}

static void h_report_tapstate(struct libxsvf_host* h)
{
	struct udata_s* u = h->user_data;
	if (u->verbose >= 3) {
		fprintf(stderr, "[%s]\n", libxsvf_state2str(h->tap_state));
	}
}

static void h_report_device(struct libxsvf_host* h, unsigned long idcode)
{
	// struct udata_s *u = h->user_data;
	printf("Found device on JTAG chain. IDCODE=0x%08lx, REV=0x%01lx, PART=0x%04lx, MFR=0x%03lx\n", idcode,
		(idcode >> 28) & 0xf, (idcode >> 12) & 0xffff, (idcode >> 1) & 0x7ff);
}

static void h_report_status(struct libxsvf_host* h, const char* message)
{
	struct udata_s* u = h->user_data;
	if (u->verbose >= 2) {
		fprintf(stderr, "[STATUS] %s\n", message);
	}
}

static void h_report_error(struct libxsvf_host* h, const char* file, int line, const char* message)
{
	fprintf(stderr, "[%s:%d] %s\n", file, line, message);
}

static int realloc_maxsize[LIBXSVF_MEM_NUM];

static void* h_realloc(struct libxsvf_host* h, void* ptr, int size, enum libxsvf_mem which)
{
	struct udata_s* u = h->user_data;
	if (size > realloc_maxsize[which])
		realloc_maxsize[which] = size;
	if (u->verbose >= 3) {
		fprintf(stderr, "[REALLOC:%s:%d]\n", libxsvf_mem2str(which), size);
	}
	return realloc(ptr, size);
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
	fprintf(stderr, "Garrett's Workshop XSVF Player\n");
	fprintf(stderr, "Copyright (C) 2022 Garrett's Workshop\n");
	fprintf(stderr, "Based on xsvftool-gpio, part of Lib(X)SVF (http://www.clifford.at/libxsvf/).\n");
	fprintf(stderr, "Copyright (C) 2009  RIEGL Research ForschungsGmbH\n");
	fprintf(stderr, "Copyright (C) 2009  Clifford Wolf <clifford@clifford.at>\n");
	fprintf(stderr, "Lib(X)SVF is free software licensed under the ISC license.\n");
}

int main(int argc, char** argv)
{
	if (argc != 3) {
		fprintf(stderr, "Bad arguments.\n");
		fprintf(stderr, "Usage: %s <COM_port> <XSVF_file>\n", argv[0]);
		//return -1;
	}

	//com_port_name = argv[1];
	//file_name = argv[2];
	com_port_name = "COM3";
	file_name = "C:\\Users\\zanek\\Documents\\GitHub\\GW4302\\cpld\\impl1\\REU_impl1.svf";

	copyleft();

	LONGLONG start = GetTicksNow();

	if (libxsvf_play(&h, LIBXSVF_MODE_SCAN) < 0) {
		fprintf(stderr, "Error while scanning JTAG chain.\n");
	}

	u.f = fopen(file_name, "rb");
	if (u.f == NULL) {
		fprintf(stderr, "Can't open file.\n");
		return -1;
	}

	if (libxsvf_play(&h, LIBXSVF_MODE_SVF) < 0) {
		fprintf(stderr, "Error while playing XSVF file.\n");
	}

	fclose(u.f);

	LONGLONG end = GetTicksNow() - start;
	double elapsed = (double)end / Get1msTicks() / 1000.0f;

	fprintf(stderr, "Done.\n");
	fprintf(stderr, "Total number of clock cycles: %d\n", u.clockcount);
	fprintf(stderr, "Number of significant TDI bits: %d\n", u.bitcount_tdi);
	fprintf(stderr, "Number of significant TDO bits: %d\n", u.bitcount_tdo);
	fprintf(stderr, "Time elapsed: %lf sec.\n", elapsed);
	fprintf(stderr, "Speed: %lf bits / sec.\n", (double)u.clockcount / elapsed);

	return 0;
}
