#ifndef __FTPD_UTIL_H
#define __FTPD_UTIL_H

#include <Windows.h>

const char* get_month(int i);
void ftpd_fix_slashes(char* str);
char* wcs2utf(wchar_t* str);
wchar_t* utf2wcs(char* str);

#endif