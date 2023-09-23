/*
 *  GWUpdate Packager
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "boardid.h"

#define BUF_SIZE (65536)

char buf[BUF_SIZE];

static void pad128k(FILE* to) {
#define MASK (128 * 1024 - 1)
	long baselength = ftell(to);
	long insertpos = (baselength + MASK) & (~MASK);
	long padding = insertpos - baselength;
	memset(buf, 0, BUF_SIZE);
	while (padding > 0) {
		if (padding > BUF_SIZE) {
			fwrite(buf, 1, BUF_SIZE, to);
			padding -= BUF_SIZE;
		}
		else {
			fwrite(buf, 1, padding, to);
			padding = 0;
		}
	}
}

static void writestr(FILE* to, FILE* from) {
	size_t count = fread(buf, 1, BUF_SIZE, from);
	if (count == 0) {
		buf[0] = 0;
		fwrite(buf, 1, 1, to);
		return;
	}
	while (1) {
		for (int i = 0; i < count; i++) {
			if (buf[i] == 0) {
				fwrite(buf, 1, i+1, to);
				return;
			}
		}
		fwrite(buf, 1, count, to);
		size_t newcount = fread(buf, 1, BUF_SIZE, from);

		if (newcount == 0) {
			if (buf[count - 1] != 0) {
				buf[0] = 0;
				fwrite(buf, 1, 1, to);
			}
			return;
		}
		else { count = newcount; }
	}
}

static void writebin(FILE* to, FILE* from) {
	size_t count;
	do {
		count = fread(buf, 1, BUF_SIZE, from);
		if (count != 0) { fwrite(buf, 1, count, to); }
	} while (count != 0);
}

boardid_digit_t parse_boardid_digit(char *digit, char *fail_message) {
	boardid_digit_t ret;
	if (boardid_from_char(digit[0], &ret)) {
		fputs(fail_message, stderr);
		exit(-1);
		return BOARDID_DIGIT_DONTCARE;
	}
	return ret;
}

int main(int argc, char** argv)
{
	uint32_t expected_bits;
	boardid_digit_t boardid_dsr;
	boardid_digit_t boardid_ri;
	boardid_digit_t boardid_dcd;
	uint32_t idcode;

	char* inst1;
	char* inst2;
	char* update_name;
	char* gwupdate_name;
	char* out_name;
	char is_xsvf = 0;

	FILE* gwupdate_file;
	FILE* update_file;
	FILE* inst1_file = NULL;
	FILE* inst2_file = NULL;
	FILE* out_file;

	if (argc == 1) { // Default arguments
		expected_bits = 800000;
		boardid_dsr = BOARDID_DIGIT_1;
		boardid_ri = BOARDID_DIGIT_1;
		boardid_dcd = BOARDID_DIGIT_1;
		idcode = 0x071280dd; // Altera EPM7128S "Altera97"
		inst1 = "Unplug device.\n";
		inst2 = "Plug in device.\n";
		update_name = "../update.svf";
		is_xsvf = 0;
		gwupdate_name = "../x64/Release/GWUpdate.exe";
		out_name = "GWUpdate_out.exe";
	}
	else if (argc == 11) {
		expected_bits = strtol(argv[1], NULL, 10);
		boardid_dsr = parse_boardid_digit(argv[2], "Error! Bad BOARDID_DSR.");
		boardid_ri = parse_boardid_digit(argv[3], "Error! Bad BOARDID_RI.\n");
		boardid_dcd = parse_boardid_digit(argv[4], "Error! Bad BOARDID_DCD.\n");
		idcode = strtol(argv[5], NULL, 16);
		inst1 = argv[6];
		inst2 = argv[7];
		update_name = argv[8];
		gwupdate_name = argv[9];
		out_name = argv[10];

		is_xsvf =(update_name[strlen(update_name) - 4] == 'X') || 
			(update_name[strlen(update_name) - 4] == 'x');

		inst1_file = fopen(inst1, "r");
		if (!inst1_file) {
			fputs("Error! Could not open instructions1 file.\n", stderr);
			return -1;
		}

		inst2_file = fopen(inst2, "r");
		if (!inst2_file) {
			fputs("Error! Could not open instructions2 file.\n", stderr);
			return -1;
		}
	}
	else {
		fputs("Usage: gwupkg "
			"<EXPECTED_LENGTH> "
			"<BOARDID_DSR> "
			"<BOARDID_RI> "
			"<BOARDID_DCD> "
			"<IDCODE> "
			"<INSTRUCTIONS1> "
			"<INSTRUCTIONS2> "
			"<UPDATE> "
			"<GWUPDATE> "
			"<OUT>\n", stderr);
		return -1;
	}

	gwupdate_file = fopen(gwupdate_name, "rb");
	if (!gwupdate_file) {
		fputs("Error! Could not open GWUpdate base executable.\n", stderr);
		return -1;
	}

	update_file = fopen(update_name, "rb");
	if (!update_file) {
		fputs("Error! Could not open update file.\n", stderr);
		return -1;
	}

	out_file = fopen(out_name, "wb");
	if (!out_file) {
		fputs("Error! Could not open output file.\n", stderr);
		return -1;
	}

	writebin(out_file, gwupdate_file);

	pad128k(out_file);

	// Write update file signature
	buf[0] = 'U';
	buf[1] = 'P';
	buf[2] = 'D';
	buf[3] = '8';
	fwrite(buf, 1, 4, out_file);

	// Write update file type
	buf[0] = is_xsvf ? 'X' : ' ';
	buf[1] = 'S';
	buf[2] = 'V';
	buf[3] = 'F';
	fwrite(buf, 1, 4, out_file);

	// Write boardid digits
	boardid_digit_t boardid_reserved = 0;
	fwrite(&boardid_dsr, sizeof(boardid_digit_t), 1, out_file);
	fwrite(&boardid_ri, sizeof(boardid_digit_t), 1, out_file);
	fwrite(&boardid_dcd, sizeof(boardid_digit_t), 1, out_file);
	fwrite(&boardid_reserved, sizeof(boardid_digit_t), 1, out_file);

	// Write expected bit count
	fwrite(&expected_bits, sizeof(uint32_t), 1, out_file);

	// Write number of devices (only 1 supported)
	uint32_t num_devices = 1;
	fwrite(&num_devices, sizeof(uint32_t), 1, out_file);

	// Write first (and only) device IDCODE
	fwrite(&idcode, sizeof(uint32_t), 1, out_file);

	// Write instructions 1
	if (inst1_file) { writestr(out_file, inst1_file); }
	else { fputs(inst1, out_file); fputc(0, out_file); }

	// Write instructions 2
	if (inst2_file) { writestr(out_file, inst2_file); }
	else { fputs(inst2, out_file); fputc(0, out_file); }

	// Write (X)SVF
	if (is_xsvf) { writebin(out_file, update_file); }
	else { writestr(out_file, update_file); }

	return 0;
}
