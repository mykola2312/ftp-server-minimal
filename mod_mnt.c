#include "ftpd_vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int mod_check(ftpd_user_t* user,const char* path);
static int mod_translate_path_(ftpd_user_t* user,const char* orig,char* buf,int maxLen);
static int mod_list(ftpd_user_t* user,int mlsd);
static int mod_cwd(ftpd_user_t* user,char* path);
static int mod_retr(ftpd_user_t* user,char* path);
static int mod_stor(ftpd_user_t* user,char* path);
static int mod_appe(ftpd_user_t* user,char* path);
static int mod_dele(ftpd_user_t* user,char* path);
static int mod_size(ftpd_user_t* user,char* path);
static int mod_mkd(ftpd_user_t* user,char* path);

struct vfs_mod_s mod_mnt = {
	.vfs_check = mod_check,
	.vfs_translate_wdir = mod_translate_path_,
	.vfs_list = mod_list,
	.vfs_retr = mod_retr,
	.vfs_stor = mod_stor,
	.vfs_appe = mod_appe,
	.vfs_dele = mod_dele,
	.vfs_size = mod_size,
	.vfs_cwd = mod_cwd,
	.vfs_mkd = mod_mkd
};

static int mod_check(ftpd_user_t* user,const char* path)
{
	return !memcmp(path,"/mnt",4);
}

typedef enum {
	PATH_MNT = 0,
	PATH_DRIVE,
} dirtype_t;

static dirtype_t mod_get_dirtype(char* path)
{
	if(!strcmp(path,"/mnt")) return PATH_MNT;
	return PATH_DRIVE;
}

static int mod_translate_path_(ftpd_user_t* user,const char* orig,char* buf,int maxLen)
{
	// /mnt/D/dir1/dir2
	snprintf(buf,maxLen,"%c:\\%s",orig[5],orig+7);
	ftpd_fix_slashes(buf);
	return 0;
}

static void mod_translate_path(ftpd_user_t* user,char* buf,int maxLen)
{
	// /mnt/D/dir1/dir2
	mod_translate_path_(user,user->wdir,buf,maxLen);
}

static int mod_list_drives(ftpd_user_t* user,int mlsd)
{
	char szDrive[4];
	DWORD dwDrives;
	int i;
	dataconn_t* dc;
	
	dc = ftpd_user_get_cur_dataconn(user);
	szDrive[1] = '\0';
	dwDrives = GetLogicalDrives();
	for(i = 0; i < 26; i++)
	{
		if(!((dwDrives>>i)&1)) continue;
		szDrive[0] = 'A'+i;
		vfs_reply_dir(dc,mlsd,1,1970,1,1,0,0,0,0,szDrive);
	}
	
	return 0;
}

static int mod_list_real(ftpd_user_t* user,int mlsd)
{
	char szSearch[MAX_PATH];
	char* pFile;
	wchar_t* wSearch;
	WIN32_FIND_DATAW wfd;
	HANDLE hFind;
	SYSTEMTIME tm;
	uint64_t size;
	dataconn_t* dc;
	
	dc = ftpd_user_get_cur_dataconn(user);
	
	mod_translate_path(user,szSearch,MAX_PATH);
	strncat(szSearch,"\\*",MAX_PATH-1);
	wSearch = utf2wcs(szSearch);
	
	
	hFind = FindFirstFileW(wSearch,&wfd);
	if(hFind != INVALID_HANDLE_VALUE)
	{
		do {
			if(!wcscmp(wfd.cFileName,L".")) continue;
			if(!wcscmp(wfd.cFileName,L"..")) continue;
			
			size = (uint64_t)wfd.nFileSizeHigh<<32 | (uint64_t)wfd.nFileSizeLow;
			FileTimeToSystemTime(&wfd.ftCreationTime,&tm);
			pFile = wcs2utf(wfd.cFileName);
			vfs_reply_dir(dc,mlsd,(wfd.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY),
				tm.wYear,tm.wMonth,tm.wDay,tm.wHour,tm.wMinute,tm.wSecond,
				size,pFile);
			free(pFile);
			
			
		} while(FindNextFileW(hFind,&wfd));
		FindClose(hFind);
	}
	free(wSearch);
	return 0;
}

/*
void vfs_reply_dir(dataconn_t* dc,int mlsd,
	int dir,int year,int month,int day,
	int hour,int minute,int second,
	uint64_t size,const char* name);
*/

static int mod_list(ftpd_user_t* user,int mlsd)
{
	int i;
	
	if(!strcmp(user->wdir,"/mnt"))
		return mod_list_drives(user,mlsd);
	return mod_list_real(user,mlsd);
}

static int mod_cwd(ftpd_user_t* user,char* path)
{
	dirtype_t dt;
	DWORD dwAttrs;
	char szPath[MAX_PATH];
	wchar_t* wPath;
	
	dt = mod_get_dirtype(path);
	if(dt == PATH_MNT) return 0;
	
	mod_translate_path_(user,path,szPath,MAX_PATH);
	wPath = utf2wcs(szPath);
	
	dwAttrs = GetFileAttributesW(wPath);
	free(wPath);
	
	if(dwAttrs == INVALID_FILE_ATTRIBUTES)
		return 1;
	return !(dwAttrs&FILE_ATTRIBUTE_DIRECTORY);
}

static wchar_t* mod_get_path(ftpd_user_t* user,char* path)
{
	char szFile[MAX_PATH];
	
	if(path[0] == '/')
	{
		mod_translate_path_(user,path,szFile,MAX_PATH-1);
	}
	else
	{
		mod_translate_path(user,szFile,MAX_PATH-1);
		strncat(szFile,"/",MAX_PATH-1);
		strncat(szFile,path,MAX_PATH-1);
	}
	ftpd_fix_slashes(szFile);
	return utf2wcs(szFile);
}

static int mod_retr(ftpd_user_t* user,char* path)
{
	LARGE_INTEGER size;
	
	if(mod_get_dirtype(path) == PATH_MNT) return 1;
	
	//Size required only in first dlcommand stage
	size.QuadPart = 0;
	if(user->dlcommand == 0)
	{
		char szFile[MAX_PATH];
		wchar_t* wFile;
		BY_HANDLE_FILE_INFORMATION info;
		dataconn_t* dc;
		
		dc = ftpd_user_get_cur_dataconn(user);
		wFile = mod_get_path(user,path);
		
		dc->hFile = CreateFileW(wFile,GENERIC_READ,FILE_SHARE_READ,
			NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
		free(wFile);
		
		if(dc->hFile == INVALID_HANDLE_VALUE) return 1;
		GetFileInformationByHandle(dc->hFile,&info);
		if(info.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)
		{
			CloseHandle(dc->hFile);
			return 1;
		}
		
		size.QuadPart = (uint64_t)info.nFileSizeHigh<<32 | (uint64_t)info.nFileSizeLow;
	}
	
	return vfs_fallback_start_retr(user,path,size.QuadPart,1);
}

static int mod_stor(ftpd_user_t* user,char* path)
{
	if(mod_get_dirtype(path) == PATH_MNT) return 1;
	
	if(user->dlcommand == 0)
	{
		char szFile[MAX_PATH];
		wchar_t* wFile;
		BY_HANDLE_FILE_INFORMATION info;
		dataconn_t* dc;
		
		dc = ftpd_user_get_cur_dataconn(user);
		wFile = mod_get_path(user,path);
		
		dc->hFile = CreateFileW(wFile,GENERIC_WRITE,0,NULL,
			CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
			
		free(wFile);
		if(dc->hFile == INVALID_HANDLE_VALUE) return 1;
	}
	
	return vfs_fallback_start_stor(user,path);
}

static int mod_appe(ftpd_user_t* user,char* path)
{
	if(mod_get_dirtype(path) == PATH_MNT) return 1;
	
	if(user->dlcommand == 0)
	{
		char szFile[MAX_PATH];
		wchar_t* wFile;
		BY_HANDLE_FILE_INFORMATION info;
		dataconn_t* dc;
		
		dc = ftpd_user_get_cur_dataconn(user);
		wFile = mod_get_path(user,path);
		
		dc->hFile = CreateFileW(wFile,GENERIC_ALL,0,NULL,
			TRUNCATE_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
		free(wFile);
		if(dc->hFile == INVALID_HANDLE_VALUE) return 1;
	}
	
	return vfs_fallback_start_stor(user,path);
}

static int mod_dele(ftpd_user_t* user,char* path)
{
	wchar_t* wFile;
	DWORD dwAttrs;
	BOOL bVal;
	
	wFile = mod_get_path(user,path);
	dwAttrs = GetFileAttributesW(wFile);
	
	if(dwAttrs == INVALID_FILE_ATTRIBUTES) return 1;
	else if(dwAttrs&FILE_ATTRIBUTE_DIRECTORY)
		bVal = RemoveDirectoryW(wFile);
	else bVal = DeleteFileW(wFile);
	
	
	free(wFile);
	if(bVal)
	{
		ftpd_user_send_reply(user,"250 DELE command successful\r\n");
		return 0;
	}
	return 1;
}

static int mod_size(ftpd_user_t* user,char* path)
{
	wchar_t* wFile;
	HANDLE hFile;
	LARGE_INTEGER size;
	
	wFile = mod_get_path(user,path);
	hFile = CreateFileW(wFile,GENERIC_READ,FILE_SHARE_READ,
		NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
	free(wFile);
	
	if(hFile == INVALID_HANDLE_VALUE) return 1;
	
	size.QuadPart = 0;
	GetFileSizeEx(hFile,&size);
	ftpd_user_send_replyf(user,"213 %llu\r\n",size.QuadPart);
	return 0;
}

static int mod_mkd(ftpd_user_t* user,char* path)
{
	wchar_t* wFile;
	BOOL bVal;
	
	wFile = mod_get_path(user,path);
	bVal = CreateDirectoryW(wFile,NULL);
	free(wFile);
	
	if(bVal)
		ftpd_user_send_replyf(user,"257 \"%s\" created successfully\r\n",path);
	return !bVal;
}