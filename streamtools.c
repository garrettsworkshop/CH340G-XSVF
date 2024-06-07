#include "streamtools.h"
#include <string.h>

#define LEN128K (128 * 1024)
#define LEN128K_MASK (LEN128K - 1)

int file_pad128k(FILE* to) {
	long baselength = ftell(to);
	long insertpos = (baselength + LEN128K_MASK) & (~LEN128K_MASK);
	long padding = insertpos - baselength;

	for (int i = 0; i < padding; i++) {
		fputc(0, to);
		if (ferror(to)) { return -1; }
	}
}

int file_copy128k(FILE* to, FILE* from, size_t count) {
	for (int i = 0; i < count; i++) {
		for (int j = 0; j < LEN128K; j++) {
			int c = fgetc(from);
			if (ferror(from) || c == EOF) { return -1; }
			fputc(c, to);
			if (ferror(to)) { return -1; }
		}
	}
	return 0;
}

int file_writeall(FILE* to, FILE* from) {
	while (1) {
		int c = fgetc(from);
		if (ferror(from)) { return -1; }
		if (c == EOF) { break; }
		fputc(c, to);
		if (ferror(to)) { return -1; }
	}
	return 0;
}

int file_writeallstr(FILE* to, FILE* from) {
	while (1) {
		int c = fgetc(from);
		if (ferror(from)) { return -1; }
		if (c == EOF || c == 0) {
			fputc(0, to);
			break;
		}
		fputc(c, to);
		if (ferror(to)) { return -1; }
	}
	return 0;
};

int file_search128k(FILE* f, char *sig) {
	// Find embedded update file
	for (int i = 1; ; i++) { // Search at each 128k offset for signature
		// Fail if looked too many times
		if (i > 255) { return -1; }

		// Seek to offset and fail if can't
		if (fseek(f, i * LEN128K, SEEK_SET)) { return 0;}

		char c = fgetc(f);
		if (c != sig[0]) { continue; }
		c = fgetc(f);
		if (c != sig[1]) { continue; }
		c = fgetc(f);
		if (c != sig[2]) { continue; }
		c = fgetc(f);
		if (c != sig[3]) { continue; }
		break;
	}
	return 1;
}
