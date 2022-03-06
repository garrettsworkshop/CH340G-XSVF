#include <Windows.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#ifndef _CH340G_TIME_H
#define _CH340G_TIME_H

LONGLONG ticks_per_ms;
static void Setup1msTicks() {
	LARGE_INTEGER ticks_per_sec;
	QueryPerformanceFrequency(&ticks_per_sec);
	ticks_per_ms = (ticks_per_sec.QuadPart + 999) / 1000;
}

static LONGLONG GetTicksNow() {
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return now.QuadPart;
}

LONGLONG last;
char last_set = 0;
static void SetGateTime() {
	last = GetTicksNow();
}
static void Gate1ms() {
	if (!last_set) { SetGateTime(); }
	LONGLONG end = last + ticks_per_ms + 1;
	do { last = GetTicksNow(); } while (last < end);
}

static void Wait1ms() {
	LONGLONG end = GetTicksNow() + ticks_per_ms + 1;
	while (GetTicksNow() < end);
}

#endif
