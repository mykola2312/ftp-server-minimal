#include "ftpd_util.h"

const char* months[] = {
	"Jan","Feb","Mar","Apr","May","Jun",
	"Jul","Aug","Sep","Oct","Nov","Dec"
};

const char* get_month(int i)
{
	return months[i-1];
}

void ftpd_fix_slashes(char* str)
{
	while(*str)
	{
		if(*str == '/') *str = '\\';
		str++;
	}
}

char* wcs2utf(wchar_t* str)
{
	char* newstr;
	int newlen;
	
	newlen = WideCharToMultiByte(CP_UTF8,0,str,-1,NULL,0,NULL,NULL);
	newstr = (char*)malloc(newlen);
	WideCharToMultiByte(CP_UTF8,0,str,-1,newstr,newlen,NULL,NULL);
	
	return newstr;
}

wchar_t* utf2wcs(char* str)
{
	wchar_t* newstr;
	int newlen;
	
	newlen = MultiByteToWideChar(CP_UTF8,0,str,-1,NULL,0);
	newstr = (wchar_t*)malloc(newlen*sizeof(wchar_t));
	MultiByteToWideChar(CP_UTF8,0,str,-1,newstr,newlen);
	
	return newstr;
}