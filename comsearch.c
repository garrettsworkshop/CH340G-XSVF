#include "comsearch.h"
#include <string.h>
#include <stdint.h>

#define COM_START (1)
#define COM_END (100)

static int comsearch_exists(int portnum, char *devname, char *portname) {
	wchar_t wstrbuf[1027];
	wchar_t wdevname[16];

	if (portnum <= 0 || portnum > 127) { return 0; }

	_itoa(portnum, &devname[3], 10);
	mbstowcs(wdevname, devname, 6);
	if (QueryDosDeviceW(wdevname, wstrbuf, 512) > 0) {
		if (portname != NULL) { memcpy(portname, devname, 8); }
		return 1;
	}
	else { return 0; }
}

int comsearch(char *portname) {
	char devname[8];
	char com_found[COM_END] = { 0 };

	devname[0] = 'C';
	devname[1] = 'O';
	devname[2] = 'M';

	for (int i = COM_START; i < COM_END; i++) {
		if (comsearch_exists(i, devname, portname)) { com_found[i] = 1; }
	}

	getchar();

	for (int i = COM_START; i < COM_END; i++) {
		if (comsearch_exists(i, devname, portname) && !com_found[i]) {
			return i;
		}
	}

	return 0;
}
