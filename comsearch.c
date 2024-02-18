#include "comsearch.h"
#include <Windows.h>
#include <string.h>
#include <stdint.h>

static int comexists(int portnum, char* nameout) {
	wchar_t wstrbuf[1027];
	char devname[8];
	wchar_t wdevname[16];

	devname[0] = 'C';
	devname[1] = 'O';
	devname[2] = 'M';
	devname[3] = ' ';
	devname[4] = 0;
	devname[5] = 0;

	if (portnum < 1 || portnum > 99) { return 0; }

	_itoa(portnum, &devname[3], 10);
	mbstowcs(wdevname, devname, 6);
	if (QueryDosDeviceW(wdevname, wstrbuf, 512) > 0) {
		if (nameout) { memcpy(nameout, devname, 6); }
		return 1;
	}
	else { return 0; }
}

#define COM_START (1)
#define COM_END (99)

// com_found array remembers which COM ports existed when comsearch was called
static char com_found[COM_END + 1];

// comsearch() marks in com_found whether each COM port exists
void comsearch() {
	// Iterate through all COM ports between COM_START and COM_END
	for (int i = COM_START; i <= COM_END; i++) {
		// Check if COM port number i exists.
		int exists = comexists(i, NULL);
		// If it does, mark it as found by putting a 1 in 
		// the com_found array at index i.
		if (exists) { com_found[i] = 1; }
		// If it doesn't exist, mark it as not found by putting a 0.
		else { com_found[i] = 0; }
	}
}

// compick(...) checks for a new COM port added since comsearch() was called.
// If all COM ports found by the last comsearch() call are still present,
// and if one single new COM port has been added since the last comsearch(),
// then compick(...) returns that single new COM port's number.
// Otherwise compick(...) returns 0.
// The portname parameter is an optional pointer to a char[] of length 6.
// If portname is nonnull and the return value from compick(...) is nonzero
// (indicating a single new COM port was found), the name of the COM port
// is stored at portname as a null-terminated string.
int compick(char* portname) {
	int found = 0; // This variable will hold the number of the new COM port

	// Iterate through all the COM ports
	for (int i = COM_START; i <= COM_END; i++) {
		// Check if COM port number i exists.
		int exists = comexists(i, NULL);

		// If it exists now and wasn't found before, that means it's new...
		if (exists && !com_found[i]) {
			// If no new COM port was previously found,
			// remember that COM port i is new
			if (found == 0) { found = i; }
			// If another new COM port was found previously however,
			// return 0 since there are at least two new COM ports.
			else { return 0; }
		}
		// If it doesn't exist now but was found before, return 0
		// because the COM ports have changed too much since comsearch().
		else if (!exists && com_found[i]) { return 0; }
	}
	if (found == 0) { return 0; } // Fail if nothing found

	Sleep(1000); // Wait 1000 milliseconds.

	// If we found something, check to make sure it still exists.
	// Also pass portname to comexists so the portname is written back.
	// Then return the number of the COM port.
	if (comexists(found, portname)) { return found; }
	// Otherwise if nothing found or it disappeared, return 0.
	else { return 0; }
}
