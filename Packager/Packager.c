/*
 *  GWUpdate Packager
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "../boardid.h"
#include "../streamtools.h"

char buf[256];

boardid_digit_t parse_boardid_digit(const char* digit, const char* fail_message) {
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

	const char* inst1;
	const char* inst2;
	const char* update_name;
	const char* gwupdate_name;
	const char* driver_name;
	const char* out_name;
	char is_xsvf = 0;

	FILE* gwupdate_file;
	FILE* driver_file = NULL;
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
		driver_name = "../Driver/CH341SER.exe";
		out_name = "GWUpdate_out.exe";
	}
	else if (argc == 12) {
		expected_bits = strtol(argv[1], NULL, 10);
		boardid_dsr = parse_boardid_digit(argv[2], "Error! Bad BOARDID_DSR.");
		boardid_ri = parse_boardid_digit(argv[3], "Error! Bad BOARDID_RI.\n");
		boardid_dcd = parse_boardid_digit(argv[4], "Error! Bad BOARDID_DCD.\n");
		idcode = strtol(argv[5], NULL, 16);
		inst1 = argv[6];
		inst2 = argv[7];
		update_name = argv[8];
		gwupdate_name = argv[9];
		driver_name = argv[10];
		out_name = argv[11];

		is_xsvf = (update_name[strlen(update_name) - 4] == 'X') ||
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
		fputs("Usage: Packager "
			"<EXPECTED_LENGTH> "
			"<BOARDID_DSR> "
			"<BOARDID_RI> "
			"<BOARDID_DCD> "
			"<IDCODE> "
			"<INSTRUCTIONS1> "
			"<INSTRUCTIONS2> "
			"<UPDATE> "
			"<GWUPDATE> "
			"<DRIVER> "
			"<OUT>\n", stderr);
		return -1;
	}

	gwupdate_file = fopen(gwupdate_name, "rb");
	if (!gwupdate_file) {
		fputs("Error! Could not open GWUpdate base executable.\n", stderr);
		return -1;
	}

	if (strcmp(driver_name, "nodriver")) {
		driver_file = fopen(driver_name, "rb");
		if (!driver_file) {
			fputs("Error! Could not open driver installer.\n", stderr);
			return -1;
		}
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

	if (file_writeall(out_file, gwupdate_file)) {
		fputs("Error! Could not copy GWUpdate executable to output file.\n", stderr);
		return -1;
	}

	if (file_pad128k(out_file)) {
		fputs("Error! Could not pad output file.\n", stderr);
		return -1;
	}

	// If driver present, copy it
	if (driver_file) {
		// Write driver signature "DRVR"
		buf[0] = 'D';
		buf[1] = 'R';
		buf[2] = 'V';
		buf[3] = 'R';
		fwrite(buf, 1, 4, out_file);

		// Get driver file length
		fseek(driver_file, 0L, SEEK_END);
		uint32_t driver_length = ftell(driver_file);
		rewind(driver_file);

		// Write driver file length
		fwrite(&driver_length, sizeof(uint32_t), 1, out_file);

		if (file_writeall(out_file, driver_file)) {
			fputs("Error! Could not copy driver installer to output file.\n", stderr);
			return -1;
		}

		if (file_pad128k(out_file)) {
			fputs("Error! Could not pad output file.\n", stderr);
			return -1;
		}
	}

	// Write update file signature "UPD8"
	buf[0] = 'U';
	buf[1] = 'P';
	buf[2] = 'D';
	buf[3] = '8';
	fwrite(buf, 1, 4, out_file);

	// Write number of updates (only 1 supported)
	uint32_t num_updates = 1;
	fwrite(&num_updates, sizeof(uint32_t), 1, out_file);

	// Write instructions 1
	if (inst1_file) { file_writeallstr(out_file, inst1_file); }
	else { fputs(inst1, out_file); fputc(0, out_file); }

	// Write instructions 2
	if (inst2_file) { file_writeallstr(out_file, inst2_file); }
	else { fputs(inst2, out_file); fputc(0, out_file); }

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

	// Compute and write update length
	uint32_t length = 0;
	// Get length of update_file
	fseek(update_file, 0L, SEEK_END);
	length += ftell(update_file);
	rewind(update_file);
	// Write update length
	fwrite(&length, sizeof(uint32_t), 1, out_file);

	// Write (X)SVF image
	file_writeall(out_file, update_file);

	// Close files
	fclose(out_file);
	fclose(update_file);
	fclose(gwupdate_file);
	if (inst2_file) { fclose(inst2_file); }
	if (inst1_file) { fclose(inst1_file); }

	return 0;
}
