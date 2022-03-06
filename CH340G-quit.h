#include <Windows.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#ifndef _CH340G_QUIT_H
#define _CH340G_QUIT_H

static int quit(int code) {
	fprintf(stderr, "Press enter to quit.\n");
	getchar();
	exit(code);
	return code;
}

#endif
