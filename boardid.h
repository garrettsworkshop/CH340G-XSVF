#ifndef _BOARDID_H
#define _BOARDID_H

#include <stdint.h>

typedef int8_t boardid_digit_t;
#define BOARDID_DIGIT_0 (0b0000)
#define BOARDID_DIGIT_1 (0b1111)
#define BOARDID_DIGIT_TDI (0b1010)
#define BOARDID_DIGIT_nTDI (0b0101)
#define BOARDID_DIGIT_TMS (0b1100)
#define BOARDID_DIGIT_nTMS (0b0011)
#define BOARDID_DIGIT_DONTCARE (-1)

int boardid_from_char(char digit, boardid_digit_t* id) {
	switch (digit) {
	case '0': *id = BOARDID_DIGIT_0; return 0;
	case '1': *id = BOARDID_DIGIT_1; return 0;
	case 'D': *id = BOARDID_DIGIT_TDI; return 0;
	case 'd': *id = BOARDID_DIGIT_nTDI; return 0;
	case 'M': *id = BOARDID_DIGIT_TMS; return 0;
	case 'm': *id = BOARDID_DIGIT_nTMS; return 0;
	case 'X': *id = BOARDID_DIGIT_DONTCARE; return 0;
	default: return -1;
	}
}

int boardid_to_char(boardid_digit_t id, char* digit) {
	switch (id) {
	case BOARDID_DIGIT_0: *digit = '0'; return 0;
	case BOARDID_DIGIT_1: *digit = '1'; return 0;
	case BOARDID_DIGIT_TDI: *digit = 'D'; return 0;
	case BOARDID_DIGIT_nTDI: *digit = 'd'; return 0;
	case BOARDID_DIGIT_TMS: *digit = 'M'; return 0;
	case BOARDID_DIGIT_nTMS: *digit = 'm'; return 0;
	case BOARDID_DIGIT_DONTCARE: *digit = 'X'; return 0;
	default: return -1;
	}
}

int boardid_digit_invalid(boardid_digit_t digit) {
	if (digit == BOARDID_DIGIT_DONTCARE) { return 0; }
	else if (digit & 0xF0) { return -1; }
	else { return 0; }
}

#endif
