#include <Windows.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#ifndef _GWU_QUIT_H
#define _GWU_QUIT_H

static void get_enter() {
	while (getchar() != '\n');
}

static int quit(int code) {
	fprintf(stderr, "Press enter to quit.\n");
	fflush(stderr);
	get_enter();
	exit(code);
	return code;
}

#endif
