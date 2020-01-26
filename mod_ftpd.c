#include "ftpd_vfs.h"
#ifdef _ENABLE_MOD_FTPD
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef _ENABLE_SCAP
#include "scap.h"
#endif

static int mod_check(ftpd_user_t* user,const char* path);
static int mod_list(ftpd_user_t* user,int mlsd);
static int mod_cwd(ftpd_user_t* user,char* path);
static int mod_retr(ftpd_user_t* user,char* path);
static int mod_stor(ftpd_user_t* user,char* path);

struct vfs_mod_s mod_ftpd = {
	.vfs_check = mod_check,
	.vfs_translate_wdir = NULL,
	.vfs_list = mod_list,
	.vfs_retr = mod_retr,
	.vfs_stor = mod_stor,
	.vfs_appe = NULL,
	.vfs_dele = NULL,
	.vfs_size = NULL,
	.vfs_cwd = mod_cwd,
	.vfs_mkd = NULL
};

static int mod_check(ftpd_user_t* user,const char* path)
{
	return !memcmp(path,"/ftpd",5);
}

static char* vdirs[] = {
	"execute"
};

static uint32_t s_ScrID = 0;

static void mod_gen_scr_id()
{
	if(!s_ScrID) s_ScrID = time(NULL);
	s_ScrID++;
}

static void mod_get_scr_name(char* buf)
{
	sprintf(buf,"scap_%lu.bmp.gz",s_ScrID);
}

static int mod_list(ftpd_user_t* user,int mlsd)
{
	int i;
	char szScrName[64];
	dataconn_t* dc;
	
	dc = ftpd_user_get_cur_dataconn(user);
	if(!strcmp(user->wdir,"/ftpd"))
	{
		for(i = 0; i < (sizeof(vdirs)/sizeof(const char*)); i++)
			vfs_reply_dir(dc,mlsd,1,1970,1,1,0,0,0,0,vdirs[i]);
		
		//screenshot file
		mod_gen_scr_id();
		mod_get_scr_name(szScrName);
		vfs_reply_dir(dc,mlsd,0,1970,1,1,0,0,0,0,szScrName);
	}
	return 0;
}

static int mod_cwd(ftpd_user_t* user,char* path)
{
	return 0;
}

#ifdef _ENABLE_SCAP
static int mod_retr_scap(ftpd_user_t* user,char* path)
{
	LARGE_INTEGER size;
	
	size.QuadPart = 0;
	if(user->dlcommand == 0)
	{
		dataconn_t* dc;
		wchar_t wScapFile[MAX_PATH+1];
		wchar_t wScapName[32];
		HANDLE hFile;
		DWORD dwRead;
		char* pMem;
		
		dc = ftpd_user_get_cur_dataconn(user);
		
		GetTempPathW(MAX_PATH+1,wScapFile);
		swprintf(wScapName,32,L"scap_%lu.bmp.gz",s_ScrID);
		wcsncat(wScapFile,wScapName,MAX_PATH);
		
		take_screenshot(wScapFile);
		hFile = CreateFileW(wScapFile,GENERIC_READ,FILE_SHARE_READ,
			NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
		if(hFile == INVALID_HANDLE_VALUE) return 1;
		
		GetFileSizeEx(hFile,&size);
		pMem = (char*)malloc((size_t)size.QuadPart);
		ReadFile(hFile,pMem,(DWORD)size.QuadPart,&dwRead,NULL);
		CloseHandle(hFile);
		
		ftpd_conn_send(&dc->conn,pMem,(size_t)size.QuadPart);
		free(pMem);
		
		DeleteFileW(wScapFile);
	}
	
	return vfs_fallback_start_retr(user,path,size.QuadPart,0);
}
#endif

static int mod_retr(ftpd_user_t* user,char* path)
{
	if(!memcmp(path,"/ftpd/scap_",11) || !memcmp(path,"scap_",5))
		return mod_retr_scap(user,path);
	return 0;
}

static void mod_exec_event_end(void* u,void* d)
{
	ftpd_user_t* user;
	dataconn_t* dc = (dataconn_t*)d;
	wchar_t* wPath;
	
	PROCESS_INFORMATION pi;
	STARTUPINFOW si;
	
	user = (ftpd_user_t*)u;
	
	ftpd_user_send_reply(user,"226 Transfer complete.\r\n");
	
	//Release file
	CloseHandle(dc->hFile);
	dc->hFile = NULL;
	
	//Execute program
	wPath = (wchar_t*)user->udata;
	
	ZeroMemory(&pi,sizeof(pi));
	ZeroMemory(&si,sizeof(si));
	
	si.cb = sizeof(STARTUPINFOW);
	si.wShowWindow = 0;
	CreateProcessW(wPath,NULL,NULL,NULL,FALSE,
		0,NULL,NULL,&si,&pi);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	free(wPath);
}

static int mod_stor_execute(ftpd_user_t* user,char* path)
{
	dataconn_t* dc;
	
	dc = ftpd_user_get_cur_dataconn(user);
	if(user->dlcommand == 0)
	{
		wchar_t wPath[MAX_PATH+1];
		wchar_t wExeFile[64];
		
		GetTempPathW(MAX_PATH+1,wPath);
		swprintf(wExeFile,64,L"%lu%lu.exe",time(NULL),GetTickCount());
		wcsncat(wPath,wExeFile,MAX_PATH);
		
		dc->hFile = CreateFileW(wPath,GENERIC_WRITE,0,NULL,
			CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);
		if(dc->hFile == INVALID_HANDLE_VALUE) return 1;
		
		user->udata = wcsdup(wPath);
		return vfs_fallback_start_stor(user,path);
	}
	else if(user->dlcommand == 1)
	{
		vfs_fallback_start_stor(user,path);
		
		//Replace close handler
		dc->close = mod_exec_event_end;
	}
	
	return 0;
}

static int mod_stor(ftpd_user_t* user,char* path)
{
	if(!memcmp(user->wdir,"/ftpd/execute",13) || !memcmp(path,"/ftpd/execute/",14))
		return mod_stor_execute(user,path);
}

#endif