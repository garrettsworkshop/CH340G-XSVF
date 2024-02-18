#include "streamtools.h"
#include <string.h>

#define LEN128K (128 * 1024)
#define LEN128K_MASK (LEN128K - 1)

#define BUF_SIZE (16 * 1024 * 1024)
char buf[BUF_SIZE + 1];

int file_pad128k(FILE* to) {
	long baselength = ftell(to);
	long insertpos = (baselength + LEN128K_MASK) & (~LEN128K_MASK);
	long padding = insertpos - baselength;
	if (padding > BUF_SIZE) { return -1; }
	memset(buf, 0, padding);
	return (fwrite(buf, 1, padding, to) == padding) ? 0 : -1;
}

int file_copy128k(FILE* to, FILE* from, size_t count) {
	for (int i = 0; i < count; i++) {
		if (fread(buf, 1, LEN128K, from) != LEN128K) { return -1; }
		if (fwrite(buf, 1, LEN128K, to) != LEN128K) { return -1; }
	}
	return 0;
}

int file_writeall(FILE* to, FILE* from) {
	size_t count = fread(buf, 1, BUF_SIZE, from);
	return (fwrite(buf, 1, count, to) == count) ? 0 : -1;
}

int file_writeallstr(FILE* to, FILE* from) {
	buf[0] = 0;
	buf[BUF_SIZE] = 0;
	size_t count = fread(buf, 1, BUF_SIZE, from);
	size_t strlength = strnlen(buf, BUF_SIZE) + 1;
	return (fwrite(buf, 1, strlength, to) == strlength) ? 0 : -1;
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
