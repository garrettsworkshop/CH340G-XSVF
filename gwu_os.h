#ifndef _GWU_OS_H
#define _GWU_OS_H

#include <stdio.h>

int driver_check();
int driver_install(FILE* driver_src);
int os_is_wine();

#endif
