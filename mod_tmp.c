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

struct vfs_mod_s mod_tmp = {
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

extern struct vfs_mod_s mod_mnt;

static int mod_check(ftpd_user_t* user,const char* path)
{
	return !memcmp(path,"/tmp",4);
}

static int mod_translate_path_(ftpd_user_t* user,const char* orig,char* buf,int maxLen)
{
	// /home/USER/dir1/dir2
	// C:\Users\asus\AppData\Local\Temp\
	// /tmp/dir1/dir2
	static char szTempDir[MAX_PATH+1];
	if(szTempDir[0] == '\0')
	{
		wchar_t wTempDir[MAX_PATH];
		char* pTempUtf;
		
		GetTempPathW(MAX_PATH+1,wTempDir);
		wTempDir[wcslen(wTempDir)-1] = L'\0'; //Remove last slash
		pTempUtf = wcs2utf(wTempDir);

		snprintf(szTempDir,MAX_PATH,"/mnt/%c%s",pTempUtf[0],pTempUtf+2);
		free(pTempUtf);
		
		vfs_fix_slashes(szTempDir);
	}
	
	strncpy(buf,szTempDir,maxLen);
	strncat(buf,orig+5,maxLen-1);
	
	return 0;
}

static int mod_list(ftpd_user_t* user,int mlsd)
{
	char szPath[MAX_PATH];
	char szWdirOld[MAX_PATH];
	int ret;
	
	mod_translate_path_(user,user->wdir,szPath,MAX_PATH);
	strncpy(szWdirOld,user->wdir,MAX_PATH);
	strncpy(user->wdir,szPath,MAX_PATH);
	
	ret = mod_mnt.vfs_list(user,mlsd);
	strncpy(user->wdir,szWdirOld,MAX_PATH);
	return ret;
}

static int mod_cwd(ftpd_user_t* user,char* path)
{
	char szPath[MAX_PATH];
	
	mod_translate_path_(user,user->wdir,szPath,MAX_PATH);
	return mod_mnt.vfs_cwd(user,szPath);
}

static int mod_retr(ftpd_user_t* user,char* path)
{
	char szPath[MAX_PATH];
	char szWdirOld[MAX_PATH];
	int ret;
	
	mod_translate_path_(user,user->wdir,szPath,MAX_PATH);
	strncpy(szWdirOld,user->wdir,MAX_PATH);
	strncpy(user->wdir,szPath,MAX_PATH);
	
	ret = mod_mnt.vfs_retr(user,path);
	strncpy(user->wdir,szWdirOld,MAX_PATH);
	return ret;
}

static int mod_stor(ftpd_user_t* user,char* path)
{
	char szPath[MAX_PATH];
	char szWdirOld[MAX_PATH];
	int ret;
	
	mod_translate_path_(user,user->wdir,szPath,MAX_PATH);
	strncpy(szWdirOld,user->wdir,MAX_PATH);
	strncpy(user->wdir,szPath,MAX_PATH);
	
	ret = mod_mnt.vfs_stor(user,path);
	strncpy(user->wdir,szWdirOld,MAX_PATH);
	return ret;
}

static int mod_appe(ftpd_user_t* user,char* path)
{
	char szPath[MAX_PATH];
	char szWdirOld[MAX_PATH];
	int ret;
	
	mod_translate_path_(user,user->wdir,szPath,MAX_PATH);
	strncpy(szWdirOld,user->wdir,MAX_PATH);
	strncpy(user->wdir,szPath,MAX_PATH);
	
	ret = mod_mnt.vfs_appe(user,path);
	strncpy(user->wdir,szWdirOld,MAX_PATH);
	return ret;
}

static int mod_dele(ftpd_user_t* user,char* path)
{
	char szPath[MAX_PATH];
	char szWdirOld[MAX_PATH];
	int ret;
	
	mod_translate_path_(user,user->wdir,szPath,MAX_PATH);
	strncpy(szWdirOld,user->wdir,MAX_PATH);
	strncpy(user->wdir,szPath,MAX_PATH);
	
	ret = mod_mnt.vfs_dele(user,path);
	strncpy(user->wdir,szWdirOld,MAX_PATH);
	return ret;
}

static int mod_size(ftpd_user_t* user,char* path)
{
	char szPath[MAX_PATH];
	char szWdirOld[MAX_PATH];
	int ret;
	
	mod_translate_path_(user,user->wdir,szPath,MAX_PATH);
	strncpy(szWdirOld,user->wdir,MAX_PATH);
	strncpy(user->wdir,szPath,MAX_PATH);
	
	ret = mod_mnt.vfs_size(user,path);
	strncpy(user->wdir,szWdirOld,MAX_PATH);
	return ret;
}

static int mod_mkd(ftpd_user_t* user,char* path)
{
	char szPath[MAX_PATH];
	char szWdirOld[MAX_PATH];
	int ret;
	
	mod_translate_path_(user,user->wdir,szPath,MAX_PATH);
	strncpy(szWdirOld,user->wdir,MAX_PATH);
	strncpy(user->wdir,szPath,MAX_PATH);
	
	ret = mod_mnt.vfs_mkd(user,path);
	strncpy(user->wdir,szWdirOld,MAX_PATH);
	return ret;
}