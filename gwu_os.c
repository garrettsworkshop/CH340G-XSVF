#include "gwu_os.h"
#include <stdint.h>
#include <stdio.h>
#include <Windows.h>

#define BUF_LEN (16 * 1024 * 1024)
char buf[BUF_LEN + 1];

int driver_check() {
	char cmdLine[] = "pnputil /enum-drivers";

	SECURITY_ATTRIBUTES sa = { 0 };
	sa.nLength = sizeof(sa);
	sa.lpSecurityDescriptor = NULL;
	sa.bInheritHandle = TRUE;

	HANDLE out_rd, out_wr, err_rd, err_wr;

	if (!CreatePipe(&out_rd, &out_wr, &sa, 0) ||
		!CreatePipe(&err_rd, &err_wr, &sa, 0)) {
		return 0;
	}

	SetHandleInformation(out_rd, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(err_rd, HANDLE_FLAG_INHERIT, 0);

	PROCESS_INFORMATION pi = { 0 };
	STARTUPINFOA si = { 0 };
	//si.cbSize = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = out_wr;
	si.hStdError = err_wr;

	// Start pnputil enumerate command
	if (!CreateProcessA(
		NULL, // application name
		cmdLine, // command line
		NULL, // process attrs
		NULL, // thread attrs
		TRUE, //inherit handles 
		0, // creation flags
		NULL, // environment variables
		NULL, // current directory
		&si, &pi)) { return 0; }

	// Wait for process to exit
	WaitForSingleObject(pi.hProcess, INFINITE);

	// Read stdout into buffer
	DWORD bytes_read;
	if (!ReadFile(out_rd, buf, BUF_LEN, &bytes_read, NULL)) { return 0; }
	buf[bytes_read] = 0; // Put null terminator at end

	// Close all handles
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	CloseHandle(out_rd);
	CloseHandle(out_wr);
	CloseHandle(err_rd);
	CloseHandle(err_wr);

	for (unsigned int i = 0; i < bytes_read; i++) {
		buf[i] = toupper(buf[i]);
	}

	// Search buffer for driver name
	if (strstr(buf, "CH341SER.INF") ||
		strstr(buf, "CH340SER.INF")) {
		return 1;
	}
	else { return 0; }
}

int driver_install(FILE* driver_src) {
	// Get driver exe length
	uint32_t driver_len;
	if (!fread(&driver_len, sizeof(uint32_t), 1, driver_src)) { return -1; }

	// Fail if driver exe too big
	if (driver_len > BUF_LEN) { return -1; }

	// Read driver exe into buffer
	if (fread(buf, 1, driver_len, driver_src) != driver_len) { return -1; }

	// Get temp directory
	char temp_dir_name[MAX_PATH + 1];
	if (!GetTempPathA(MAX_PATH, &temp_dir_name)) { return -1; }

	// Get temp file name
	char temp_file_name[MAX_PATH + 1];
	GetTempFileNameA(temp_dir_name, "GWU", 0, &temp_file_name);
	size_t temp_file_name_length = strlen(temp_file_name);

	// Get temp exe name
	char temp_exe_name[MAX_PATH + 1];
	strcpy(temp_exe_name, temp_file_name);
	temp_exe_name[temp_file_name_length - 3] = 'e';
	temp_exe_name[temp_file_name_length - 2] = 'x';
	temp_exe_name[temp_file_name_length - 1] = 'e';

	// Rename file to exe
	MoveFileA(temp_file_name, temp_exe_name);

	// Open temporary file
	FILE* temp_file = fopen(temp_exe_name, "wb");
	if (!temp_file) { return -1; }

	// Write driver exe to temp file
	if (fwrite(buf, 1, driver_len, temp_file) != driver_len) { return -1; }

	// Close temp file
	fclose(temp_file);

	// Run program
	SHELLEXECUTEINFOA ei = { 0 };
	ei.cbSize = sizeof(SHELLEXECUTEINFOA);
	ei.fMask = SEE_MASK_NOCLOSEPROCESS;
	ei.hwnd = NULL;
	ei.lpVerb = NULL;
	ei.lpFile = temp_exe_name;
	ei.lpParameters = "-sp/S";
	ei.lpDirectory = NULL;
	ei.nShow = SW_SHOW;
	ei.hInstApp = NULL;
	if (!ShellExecuteExA(&ei)) { return -1; }

	// Wait for it to finish
	WaitForSingleObject(ei.hProcess, INFINITE);

	for (int i = 0; i < 10; i++) {
		fputc('.', stderr);
#ifndef _DEBUG
		Sleep(800);
#else
		Sleep(200);
#endif
	}
	fprintf(stderr, "\n\n");

	// Delete temp file
	DeleteFileA(temp_exe_name);

	return 0;
}

int os_is_wine() {
	static const char* (CDECL * pwine_get_version)(void);
	HMODULE h_ntdll = GetModuleHandleA("ntdll.dll");
	if (!h_ntdll) { return 0; }
	pwine_get_version = (void*)GetProcAddress(h_ntdll, "wine_get_version");
	if (pwine_get_version) { return 1; }
	else { return 0; }
}
