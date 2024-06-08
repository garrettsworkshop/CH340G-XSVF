/*
 *  GWUpdate
 *  based on...
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

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include "CH340G-HAL.h"
#include "gwu_time.h"
#include "gwu_console.h"
#include "gwu_os.h"
#include "boardid.h"

#define LEN128K (128 * 1024)

uint32_t expected_devices;
uint32_t expected_idcode;
uint32_t found_devices;
uint32_t found_idcode;
uint32_t expected_bits;
enum libxsvf_mode cur_mode;
char enable_vt;

struct udata_s {
	FILE* f;
	int clockcount;
	int bitcount_tdi;
	int bitcount_tdo;
	int sendcount;
};
static struct udata_s u;

LONGLONG start = 0;
void printinfo() {
	LONGLONG end = GetTicksNow() - start;
	double elapsed = (double)end / ticks_per_ms / 1000.0f;
	fprintf(stderr, "\n");
	fprintf(stderr, "Total number of clock cycles: %d\n", u.clockcount);
	fprintf(stderr, "Number of significant TDI bits: %d\n", u.bitcount_tdi);
	fprintf(stderr, "Number of significant TDO bits: %d\n", u.bitcount_tdo);
	fprintf(stderr, "Number of TCK pulsetrains: %d\n", u.sendcount);
	fprintf(stderr, "Time elapsed: %lf sec.\n", elapsed);
	fprintf(stderr, "Speed: %lf bits / sec.\n", (double)u.clockcount / elapsed);
	fprintf(stderr, "\n");
}
void printshortinfo() {
	if (cur_mode != LIBXSVF_MODE_SCAN) {
		LONGLONG end = GetTicksNow() - start;
		double elapsed = (double)end / ticks_per_ms / 1000.0f;
		if (enable_vt) { fprintf(stderr, "\033[1A\033[K"); }
		float percent = 100.0f * (float)u.clockcount / expected_bits;
		if (percent > 100.0f) { percent = 100.0f; }
		fprintf(stderr, "Update in progress... %-4.1f%%      Bits: %d       Time: %.1f sec.      Speed: %.1f b/sec.\n",
			percent, u.clockcount, elapsed, (double)u.clockcount / elapsed);
	}
}

unsigned char tck_queue = 0;

static void flush_tck() {
	io_tck(tck_queue);
	tck_queue = 0;
}

static int h_setup(struct libxsvf_host* h)
{
	static char printed = 0;
	struct udata_s* u = h->user_data;
	if (!printed) {
		fprintf(stderr, "Opening JTAG connection...\n\n");
		fflush(stderr);
		printed = 1;
	}
	tck_queue = 0;
	io_setup();
	return 0;
}

static int h_shutdown(struct libxsvf_host* h)
{
	struct udata_s* u = h->user_data;
	if (cur_mode != LIBXSVF_MODE_SCAN) {
		fprintf(stderr, "Closing JTAG connection...\n\n");
		fflush(stderr);
	}
	flush_tck();
	io_shutdown();
	return 0;
}

static void h_udelay(struct libxsvf_host* h, long usecs, int tms, long num_tck)
{
	struct udata_s* u = h->user_data;

	if (tck_queue > 0) { flush_tck(); }

	if (num_tck > 0) {
		io_tms(tms);
		while (num_tck > 255) {
			io_tck(255);
			num_tck -= 255;
		}
		io_tck((unsigned char)num_tck);
	}
	if (usecs > 0) { Sleep((usecs + 999) / 1000); }
}

int getbyte_limit = 0;
int getbyte_cur = 0;
static int h_getbyte(struct libxsvf_host* h)
{
	if (getbyte_cur >= getbyte_limit) { return EOF; }
	struct udata_s* u = h->user_data;
	int c = fgetc(u->f);
	getbyte_cur++;
	return c;
}

static int h_set_frequency(struct libxsvf_host* h, int v) { return 0; }

static void h_report_tapstate(struct libxsvf_host* h)
{
	struct udata_s* u = h->user_data;
	const char* message = libxsvf_state2str(h->tap_state);
	char newmessage[40];
	memset(newmessage, ' ', sizeof(newmessage) - 1);
	newmessage[sizeof(newmessage) - 1] = 0;
	memcpy(newmessage, message, strlen(message));
	newmessage[strlen(message)] = ']';
	//fprintf(stderr, "[%s  ", newmessage);
	printshortinfo();
}

static unsigned long idcode_match = 0;
static void h_report_device(struct libxsvf_host* h, unsigned long idcode)
{
	if (idcode_match == 0 || idcode_match == -1 || idcode == idcode_match) {
		printf("Found device on JTAG chain.      IDCODE=0x%08lx, REV=0x%01lx, PART=0x%04lx, MFR=0x%03lx\n",
			idcode, (idcode >> 28) & 0xf, (idcode >> 12) & 0xffff, (idcode >> 1) & 0x7ff);
	}

	found_devices++;
	found_idcode = idcode;
	printshortinfo();
}

static void h_report_status(struct libxsvf_host* h, const char* message)
{
	struct udata_s* u = h->user_data;
	char newmessage[33];
	memset(newmessage, ' ', sizeof(newmessage) - 1);
	newmessage[sizeof(newmessage) - 1] = 0;
	if (strlen(message) < 33) {
		memcpy(newmessage, message, strlen(message));
		//fprintf(stderr, "[STATUS] %s ", newmessage);
	}
	else {
		//fprintf(stderr, "[STATUS] %s ", message);
	}
	printshortinfo();
}

static void h_report_error(struct libxsvf_host* h, const char* file, int line, const char* message)
{
	fprintf(stderr, "[%s:%d] %s\n\n", file, line, message);
}

static int realloc_maxsize[LIBXSVF_MEM_NUM];

static void* h_realloc(struct libxsvf_host* h, void* ptr, int size, enum libxsvf_mem which)
{
	struct udata_s* u = h->user_data;
	if (size > realloc_maxsize[which]) { realloc_maxsize[which] = size; }
	return realloc(ptr, size);
}

static int tms_old = -1;
static int tdi_old = -1;
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
			SetGate();
			Gate();
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
	fprintf(stderr,
		"   ____                          _    _    _        __        __            _          _                   \n"
		"  / ___|  __ _  _ __  _ __  ___ | |_ | |_ ( )___    \\ \\      / /___   _ __ | | __ ___ | |__    ___   _ __   \n"
		" | |  _  / _` || '__|| '__|/ _ \\| __|| __||// __|    \\ \\ /\\ / // _ \\ | '__|| |/ // __|| '_ \\  / _ \\ | '_ \\  \n"
		" | |_| || (_| || |   | |  |  __/| |_ | |_   \\__ \\     \\ V  V /| (_) || |   |   < \\__ \\| | | || (_) || |_) | \n"
		"  \\____| \\__,_||_|   |_|   \\___| \\__| \\__|  |___/      \\_/\\_/  \\___/ |_|   |_|\\_\\|___/|_| |_| \\___/ |  __/  \n"
		//"                                                                                                    |_|    \n");
		"                    _   _           _       _         ____            _                             |_|    \n"
		"                   | | | |_ __   __| | __ _| |_ ___  / ___| _   _ ___| |_ ___ _ __ ___  \n"
		"                   | | | | '_ \\ / _` |/ _` | __/ _ \\ \\___ \\| | | / __| __/ _ \\ '_ ` _ \\ \n"
		"                   | |_| | |_) | (_| | (_| | ||  __/  ___) | |_| \\__ \\ ||  __/ | | | | | \n"
		"                    \\___/|  __/ \\__,_|\\__,_|\\__\\___| |____/ \\__, |___/\\__\\___|_| |_| |_|\n"
		"                         |_|                                |___/                       \n");

	fprintf(stderr, "Copyright (C) 2024 Garrett's Workshop\n");
	fprintf(stderr, "Based on xsvftool-gpio, part of Lib(X)SVF (http://www.clifford.at/libxsvf/).\n");
	fprintf(stderr, "Copyright (C) 2009  RIEGL Research ForschungsGmbH\n");
	fprintf(stderr, "Copyright (C) 2009  Clifford Wolf <clifford@clifford.at>\n");
	fprintf(stderr, "Lib(X)SVF is free software licensed under the ISC license.\n");
	fprintf(stderr, "GWUpdate is free software licensed under the ISC license.\n\n");
	fprintf(stderr, "Loading...");
	for (int i = 0; i < 10; i++) {
		fputc('.', stderr);
		Sleep(250);
	}
	if (enable_vt) {
		fputc('\n', stderr);
		for (int i = 0; i < 9; i++) { fprintf(stderr, "\033[0K\033[1A"); }
	}
	fputc('\n', stderr);
	fputc('\n', stderr);
}

#define STRBUF_SIZE (64 * 1024)
char strbuf[STRBUF_SIZE];

int check_boardid_digit(int(*get)(), boardid_digit_t expected) {
	if (expected == BOARDID_DIGIT_DONTCARE) { return 0; }

	int id;
	io_tms(0);
	io_tdi(0);
	id = get();
	io_tms(0);
	io_tdi(1);
	id = (id << 1) | get();
	io_tms(1);
	io_tdi(0);
	id = (id << 1) | get();
	io_tms(1);
	io_tdi(1);
	id = (id << 1) | get();

	if (id == expected) { return 0; }
	else { return -1; }
}

int read_boardid_digit(FILE* f, boardid_digit_t* digit, int index) {
	if (!fread(digit, sizeof(boardid_digit_t), 1, f)) {
		switch (index) {
		case 0: fprintf(stderr, 
			"Error! Could not read boardid digit DSR from update image.\n");
			break;
		case 1: fprintf(stderr, 
			"Error! Could not read boardid digit RI from update image.\n");
			break;
		case 2: fprintf(stderr, 
			"Error! Could not read boardid digit DCD from update image.\n");
			break;
		case 3: fprintf(stderr, 
			"Error! Could not read boardid digit from update image.\n");
			break;
		}
		return -1;
	}
	else if (boardid_digit_invalid(*digit)) {
		switch (index) {
		case 0: fprintf(stderr, 
			"Error! Invalid boardid digit DSR in update image.\n");
			break;
		case 1: fprintf(stderr, 
			"Error! Invalid boardid digit RI in update image.\n");
			break;
		case 2: fprintf(stderr, 
			"Error! Invalid boardid digit DCD in update image.\n");
			break;
		case 3: fprintf(stderr, 
			"Error! Invalid boardid digit in update image.\n");
			break;
		}
		return -1;
	}
	return 0;
}

int main(int argc, char** argv)
{
	enum libxsvf_mode mode;
	int portnum;
	int driver_installed = 0;

	// Start driver check
	driver_start_check();

	// Set up console
	console_disable_echo();
	if (!console_enable_vt()) { enable_vt = 1; }
	else { enable_vt = 0; }

	// Display copyright message
	copyleft();

	// Check for correct number of arguments
	if (argc != 1) {
		fprintf(stderr, "Error! Bad arguments.\n");
		return quit(-1);
	}

	// Open data file
#ifndef _DEBUG
	u.f = fopen(argv[0], "rb");
#else
	u.f = fopen("Packager/GWUpdate_out.exe", "rb");
#endif

	if (!u.f) {
		fprintf(stderr,
#ifndef _DEBUG
			"Error! Failed to open GWUpdate executable as data file."
#else
			"Error! Failed to open update data file."
#endif
			"\n");
		return quit(-1);
	}

	// Find embedded driver file
	char sig[4];
	sig[0] = 'D';
	sig[1] = 'R';
	sig[2] = 'V';
	sig[3] = 'R';
	if (file_search128k(u.f, sig)) {
		// Check for driver and install it not currently present
		if (!driver_finish_check()) {
			fprintf(stderr, "Installing driver...");
			if (driver_install(u.f)) {
				fprintf(stderr, "Error! Failed to install driver.\n");
				return quit(-1);
			}
		}
	}

	// Find embedded update file and fail if not found
	sig[0] = 'U';
	sig[1] = 'P';
	sig[2] = 'D';
	sig[3] = '8';
	if (!file_search128k(u.f, sig)) {
		fprintf(stderr, "Error! Update file signature not found.\n");
		return quit(-1);
	}

	// Read number of update images from update file
	uint32_t num_updates;
	if (!fread(&num_updates, sizeof(uint32_t), 1, u.f)) { // Couldn't read idcode
		fprintf(stderr, "Error! Couldn't read number of firmware images in update file.\n");
		return quit(-1);
	}

	// Fail if number of updates is 0
	if (num_updates == 0) {
		fprintf(stderr, "Error! No firmware images found in update file.\n");
		return quit(-1);
	}

	// Print first instructions text from update file
	while (1) {
		int c = fgetc(u.f);
		if (c == EOF || c == 0) { break; }
		else { fputc(c, stderr); }
	}
	get_enter(); // Wait for enter key

	// Enumerate COM ports
	comsearch(portname);

	// Print second instructions text from update file
	while (1) {
		int c = fgetc(u.f);
		if (c == EOF || c == 0) { break; }
		else { fputc(c, stderr); }
	}
	get_enter(); // Wait for enter key

	// Pick COM port
	if ((portnum = compick(portname)) <= 0) {
		fprintf(stderr, "Error! Could not find USB device.\n");
		return quit(-1);
	}

	// Check each update until one with matching boardid and IDCODE
	uint32_t fwsize = 0;
	int matched_board = 0;
	for (uint32_t update_index = 0; update_index < num_updates; update_index++) {
		// Get (X)SVF file type flag
		int c[4];
		for (int i = 0; i < 4; i++) { c[i] = fgetc(u.f); }

		// Check update file type - SVF or XSVF
		if (c[0] == 'X') { mode = LIBXSVF_MODE_XSVF; } // First 'X' for XSVF
		else if (c[0] == ' ') { mode = LIBXSVF_MODE_SVF; } // First ' ' for SVF
		else { c[1] = EOF; }
		if ((c[1] != 'S') || (c[2] != 'V') || (c[3] != 'F')) {
			fprintf(stderr, "Error! Unsupported firmware image format: \"");

			return quit(-1);
		}

		// Get boardid digits
		boardid_digit_t boardid_dsr;
		boardid_digit_t boardid_ri;
		boardid_digit_t boardid_dcd;
		boardid_digit_t boardid_reserved;
		if (read_boardid_digit(u.f, &boardid_dsr, 0) ||
			read_boardid_digit(u.f, &boardid_ri, 1) ||
			read_boardid_digit(u.f, &boardid_dcd, 2) ||
			read_boardid_digit(u.f, &boardid_reserved, 3)) {
			fprintf(stderr, "Error! Could not read boardid digits from update image.\n");
			return quit(-1);
		}

		// Get expected bit count from update file
		if (!fread(&expected_bits, sizeof(uint32_t), 1, u.f)) {
			fprintf(stderr, "Error! Could not read expected bit count from update image.\n");
			return quit(-1);
		}

		// Read number of devices on JTAG chain
		if (!fread(&expected_devices, sizeof(uint32_t), 1, u.f)) {
			fprintf(stderr, "Error! Could not read JTAG device count from update image.\n");
			return quit(-1);
		}

		// Fail if number of devices isn't 1
		if (expected_devices > 1) {
			fprintf(stderr, "Error! Update image has multiple devices on JTAG chain but GWUpdate only supports one device.\n");
			return quit(-1);
		}
		else if (expected_devices == 0) {
			fprintf(stderr, "Error! Update image has no devices on JTAG chain.\n");
			return quit(-1);
		}
		found_devices = 0; // Reset found devices

		// Read single expected IDCODE from update file
		if (!fread(&expected_idcode, sizeof(uint32_t), 1, u.f)) { // Couldn't read idcode
			fprintf(stderr, "Error! Couldn't read JTAG idcode from file.\n");
			return quit(-1);
		}

		// Read update image length from update file
		if (!fread(&fwsize, sizeof(uint32_t), 1, u.f)) { // Couldn't read length
			fprintf(stderr, "Error! Couldn't read firmware image length from file.\n");
			return quit(-1);
		}

		// Check for expected board ID
		io_setup();
		if (check_boardid_digit(io_dsr, boardid_dsr) ||
			check_boardid_digit(io_ri, boardid_ri) ||
			check_boardid_digit(io_dcd, boardid_dcd)) {
			goto wrong_type;
		}
		io_shutdown();

		// Scan JTAG chain
		idcode_match = expected_idcode;
		cur_mode = LIBXSVF_MODE_SCAN;
		if (libxsvf_play(&h, LIBXSVF_MODE_SCAN) < 0) {
			fprintf(stderr, "Error! Failed to scan JTAG chain.\n");
			return quit(-1);
		}

		// Check for expected IDCODE
		if (expected_idcode != 0 &&
			expected_idcode != -1 &&
			expected_idcode != found_idcode) {
			goto wrong_type;
		}

		// If everything is good, break out of the loop
		matched_board = 1;
		break;

	wrong_type:
		// Fast-forward through update (X)SVF if not last update
		if (update_index != num_updates - 1) {
			fseek(u.f, fwsize, SEEK_CUR);
		}
		continue; // Try again
	}

	// Fail if no boards matched
	if (!matched_board) {
		fprintf(stderr, "Error! Firmware update is not compatible with this board.\n");
		return quit(-1);
	}

	// Set firmware size limit
	getbyte_cur = 0;
	getbyte_limit = fwsize;

	// Reset bit count
	u.bitcount_tdi = 0;
	u.bitcount_tdo = 0;
	u.clockcount = 0;
	u.sendcount = 0;

	// Start elapsed time timer
	SetupTicks();
	start = GetTicksNow();

	// Play update (X)SVF
	fputc('\n', stderr);
	cur_mode = mode;
	if (libxsvf_play(&h, mode) < 0) {
		fprintf(stderr, "Error! Failed to play (X)SVF.\n");
		printinfo();
		fprintf(stderr, "-----------------\n");
		fprintf(stderr, "| Update FAILED |\n");
		fprintf(stderr, "-----------------\n");
		return quit(-1);
	}
	else {
		printinfo();
		fprintf(stderr, "---------------------\n");
		fprintf(stderr, "| Update SUCCESSFUL |\n");
		fprintf(stderr, "---------------------\n");
	}

	LONGLONG end = GetTicksNow() - start;
	double elapsed = (double)end / ticks_per_ms / 1000.0f;

	// Close file
	fclose(u.f);

	return quit(0);
}
