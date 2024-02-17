/*
 *  GWUpdate Combiner
 */

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define LEN128K (128 * 1024)
char buf[LEN128K];

static int writebin(FILE* to, FILE* from) {
	size_t count;
	do {
		count = fread(buf, 1, LEN128K, from);
		if (count != 0) {
			if (fwrite(buf, 1, count, to) != count) { return -1; }
		}
	} while (count != 0);
	return 0;
}

static int copy128k(FILE* to, FILE* from) {
	size_t readcount = fread(buf, 1, LEN128K, from);
	if (readcount != LEN128K) { return -1; }
	size_t writecount = fwrite(buf, 1, LEN128K, to);
	if (readcount != writecount) { return -1; }
	else { return 0; }
}

int main(int argc, char** argv)
{
	int defaults = argc == 1;
	if (argc == 0 || argc == 2) {
		fputs("Usage: Combiner <OUT_FILE> <UPDATE1> <UPDATE2>...\n", stderr);
		return -1;
	}

	// Open output file
	FILE* out_file;
	if (defaults) { 
		out_file = fopen("GWUpdate_combined.exe", "wb");
		argc = 4;
	}
	else { out_file = fopen(argv[1], "wb"); }
	if (!out_file) {
		fputs("Error! Couldn't open output file.\n", stderr);
		return -1;
	}

	// Open each input file
	for (int i = 2; i < argc; i++) {
		int c; // Character buffer

		// Open input file
		FILE* in_file;
		if (defaults) { in_file = fopen("../Packager/GWUpdate_out.exe", "rb"); }
		else { in_file = fopen(argv[2], "rb"); }
		if (!in_file) {
			fputs("Error! Couldn't open input file.\n", stderr);
			return -1;
		}

		// Find embedded update file
		int update_index;
		for (update_index = 1; ; update_index++) { // Search at each 128k offset for UPD8 signature
			if (update_index > 255) { // Looked too many times fail
				fprintf(stderr, "Error! Update file signature not found.\n");
				return -1;
			}
			if (fseek(in_file, update_index * 128 * 1024, SEEK_SET)) { // Seek past end of file fail
				fprintf(stderr, "Error! Seeked past end of file looking for update file signature.\n");
				return -1;
			}

			char c = fgetc(in_file);
			if (c != 'U') { continue; }
			c = fgetc(in_file);
			if (c != 'P') { continue; }
			c = fgetc(in_file);
			if (c != 'D') { continue; }
			c = fgetc(in_file);
			if (c != '8') { continue; }
			break;
		}

		// Copy GWUpdate base executable from first update file
		if (i == 2) {
			// Copy everything before embedded update file
			rewind(in_file);
			for (int j = 0; j < update_index; j++) {
				if (copy128k(out_file, in_file)) {
					fprintf(stderr, "Error! Failed copying GWUpdate executable to output file.\n");
					return -1;
				}
			}

			// Write update file signature "UPD8"
			buf[0] = 'U';
			buf[1] = 'P';
			buf[2] = 'D';
			buf[3] = '8';
			fwrite(buf, 1, 4, out_file);
			fseek(in_file, 4, SEEK_CUR); // Skip "UPD8" in source file
		}

		// Get number of update images from input file
		uint32_t num_updates;
		if (!fread(&num_updates, sizeof(uint32_t), 1, in_file)) {
			fprintf(stderr, "Error! Couldn't read number of update images from file.\n");
			return -1;
		}

		// Check input file has just one update image
		if (num_updates != 1) { 
			fprintf(stderr, "Error! Input file has multiple update images.\n");
			return -1;
		}

		if (i == 2) {
			// Write number of updates into output file
			num_updates = argc - 2;
			fwrite(&num_updates, sizeof(uint32_t), 1, out_file);
		} else {
			// Skip instructions 1 and 2
			for (int j = 0; j < 2; j++) {
				while (1) {
					c = fgetc(in_file);
					if (c == EOF) {
						fprintf(stderr, "Error! EOF during instructions %i.\n", j);
					} else if (c == 0) { break; }
				}
			}
		}

		// Write remainder of update to output file
		if (writebin(out_file, in_file)) {
			fprintf(stderr, "Error! Failed to write input file to output file.\n");
			return -1;
		}

		fclose(in_file);
	}
}
