#include <Windows.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#ifndef _CH340G_HAL_H
#define _CH340G_HAL_H

#include "CH340G-time.h"
#include "CH340G-quit.h"

#define CLKCHAR_1 0x00
#define CLKCHAR_2 0x40
#define CLKCHAR_3 0x50
#define CLKCHAR_4 0x54
#define CLKCHAR_5 0x55

char portname[16] = { 0 };
HANDLE serialport = NULL;

static void io_tms(int val)
{
	if (!EscapeCommFunction(serialport, val ? CLRRTS : SETRTS)) {
		fprintf(stderr, "Error setting TMS on %s!\n", portname);
		quit(-1);
	}
}

static void io_tdi(int val)
{
	if (!EscapeCommFunction(serialport, val ? CLRDTR : SETDTR)) {
		fprintf(stderr, "Error setting TDI on %s!\n", portname);
		quit(-1);
	}
}

static void io_sendtck(char *buf, int len) {
	int written;
	if (!WriteFile(serialport, buf, len, &written, NULL)) {
		fprintf(stderr, "Error pulsing TCK on %s!\n", portname);
		quit(-1);
	}
	if (written < len) {
		io_sendtck(&buf[written], len - written);
	}
}

static void io_tck(unsigned char count) {
	char send[64];
	unsigned char fivecount = count / 5;
	unsigned char remainder = count % 5;
	memset(send, CLKCHAR_5, fivecount);
	switch (remainder) {
	case 1: send[fivecount] = CLKCHAR_1; break;
	case 2: send[fivecount] = CLKCHAR_2; break;
	case 3: send[fivecount] = CLKCHAR_3; break;
	case 4: send[fivecount] = CLKCHAR_4; break;
	default: break;
	}
	io_sendtck(send, fivecount + (remainder == 0 ? 0 : 1));
}

static int io_tdo()
{
	DWORD status;
	if (!GetCommModemStatus(serialport, &status)) {
		fprintf(stderr, "Error reading TDO from %s!\n", portname);
		quit(-1);
	}
	return (status & MS_CTS_ON) ? 0 : 1;
}

static void io_setup(void)
{
	char name[100] = { 0 };
	char root[] = "\\\\.\\\0";
	memcpy(name, root, strlen(root));
	memcpy(name + strlen(root), portname, strlen(portname));

	SetupTicks();

	serialport = CreateFileA(
		name,							// Port name
		GENERIC_READ | GENERIC_WRITE,	// Read & Write
		0,								// No sharing
		NULL,							// No security
		OPEN_EXISTING,					// Open existing port
		0,								// Non-overlapped I/O
		NULL);							// Null for comm devices

	if (serialport == INVALID_HANDLE_VALUE) { goto error; }

	DCB dcb;
	SecureZeroMemory(&dcb, sizeof(DCB));
	dcb.DCBlength = sizeof(DCB);

	if (!GetCommState(serialport, &dcb)) { goto error; }
	dcb.BaudRate = 2000000;
	dcb.fBinary = TRUE;
	dcb.fParity = FALSE;
	dcb.fOutxCtsFlow = FALSE;
	dcb.fOutxDsrFlow = FALSE;
	dcb.fDtrControl = DTR_CONTROL_DISABLE;
	dcb.fDsrSensitivity = FALSE;
	dcb.fTXContinueOnXoff = TRUE;
	dcb.fOutX = FALSE;
	dcb.fInX = FALSE;
	dcb.fNull = FALSE;
	dcb.fRtsControl = RTS_CONTROL_DISABLE;
	dcb.fAbortOnError = TRUE;
	dcb.ByteSize = 8;
	dcb.Parity = NOPARITY;
	dcb.StopBits = ONESTOPBIT;
	if (!SetCommState(serialport, &dcb)) { goto error; }

	if (!EscapeCommFunction(serialport, CLRBREAK)) { goto error; }
	Sleep(100);
	if (!EscapeCommFunction(serialport, SETBREAK)) { goto error; }
	Sleep(100);
	if (!EscapeCommFunction(serialport, CLRBREAK)) { goto error; }
	Sleep(100);
	if (!EscapeCommFunction(serialport, SETBREAK)) { goto error; }
	Sleep(100);
	if (!EscapeCommFunction(serialport, CLRBREAK)) { goto error; }
	Sleep(100);

	return;

error:
	fprintf(stderr, "Error opening %s!\n", portname);
	if (serialport != INVALID_HANDLE_VALUE) { CloseHandle(serialport); }
	quit(-1);
}

static void io_shutdown(void)
{
	Wait();
	Wait();
	CloseHandle(serialport);
	Wait();
	Wait();
}

#endif
