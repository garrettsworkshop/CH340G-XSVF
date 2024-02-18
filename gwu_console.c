#include <Windows.h>
#include "gwu_console.h"

int console_disable_echo() {
	// Disable console echo
	HANDLE h_stdin = GetStdHandle(STD_INPUT_HANDLE);
	DWORD mode = 0;
	if (!GetConsoleMode(h_stdin, &mode)) { return -1; }
	mode &= ~ENABLE_ECHO_INPUT;
	if (!SetConsoleMode(h_stdin, mode)) { return -1; }
	return 0;
}

int console_enable_vt() {
	// Enable ASCII terminal control codes
	HANDLE h_stderr = GetStdHandle(STD_ERROR_HANDLE);
	DWORD mode;
	if (!GetConsoleMode(h_stderr, &mode)) { return -1; }
	mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	if (!SetConsoleMode(h_stderr, mode)) { return -1; }
	return 0;
}

void get_enter() { while (getchar() != '\n'); }

int quit(int code) {
	fprintf(stderr, "Press enter to quit.\n");
	fflush(stderr);
	get_enter();
	exit(code);
	return code;
}
