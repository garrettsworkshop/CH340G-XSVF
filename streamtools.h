#ifndef _STREAMTOOLS_H
#define _STREAMTOOLS_H
#include <stdio.h>

int file_pad128k(FILE* to);
int file_copy128k(FILE* to, FILE* from, size_t count);

int file_writeall(FILE* to, FILE* from);
int file_writeallstr(FILE* to, FILE* from);

int file_search128k(FILE *f, char *sig);

#endif