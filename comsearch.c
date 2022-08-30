#include "comsearch.h"
#include <Windows.h>
#include <string.h>
#include <stdint.h>

#define COM_START (1)
#define COM_END (100)

static char com_found[COM_END] = { 0 };

static int comexists(int portnum, char *devname) {
	wchar_t wstrbuf[1027];
	wchar_t wdevname[16];

	if (portnum <= 0 || portnum > 127) { return 0; }

	_itoa(portnum, &devname[3], 10);
	mbstowcs(wdevname, devname, 6);
	if (QueryDosDeviceW(wdevname, wstrbuf, 512) > 0) {
		return 1;
	}
	else { return 0; }
}

void comsearch() {
	char devname[8] = "COM";

	memset(com_found, 0, COM_END);

	for (int i = COM_START; i < COM_END; i++) {
		if (comexists(i, devname)) { com_found[i] = 1; }
	}
}

int compick(char* portname) {
	char devname[8] = "COM";
	int found = -1;

	for (int i = COM_START; i < COM_END; i++) {
		if (comexists(i, devname) && !com_found[i]) {
			if (portname != NULL) { memcpy(portname, devname, 8); }
		}
	}

	return 0;
}
