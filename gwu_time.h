#include <Windows.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#ifndef _GWU_TIME_H
#define _GWU_TIME_H

LONGLONG ticks_per_ms;
static void SetupTicks() {
	LARGE_INTEGER ticks_per_sec;
	QueryPerformanceFrequency(&ticks_per_sec);
	ticks_per_ms = ticks_per_sec.QuadPart / 1000;
}

static LONGLONG GetTicksNow() {
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return now.QuadPart;
}

LONGLONG last;
static void SetGate() {
	last = GetTicksNow();
}
static void Gate() {
	LONGLONG end = last + ticks_per_ms;
	while (GetTicksNow() < end);
}
static void Gate2() {
	LONGLONG end = last + 2 * ticks_per_ms;
	while (GetTicksNow() < end);
}

#endif
