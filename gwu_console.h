#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#ifndef _GWU_CONSOLE_H
#define _GWU_CONSOLE_H

int console_disable_echo();
int console_enable_vt();

void get_enter();
int quit(int code);

#endif
