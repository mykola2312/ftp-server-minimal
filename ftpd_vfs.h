#ifndef __FTPD_VFS_H
#define __FTPD_VFS_H

#include "ftpd.h"

/*
	FTPd VIRTUAL FILESYSTEM
	/ - root
		/mnt - drives
			/mnt/c - windows drive D:
			/mnt/d - windows drive D:
		/tmp - %TEMP% dir for current user
		/home - home dir for current user
			/home/user1/
			/home/user2/
		/ftpd - FTPd Virtual Dir
			screenshot.jpg - download to take screenshot
			execute - upload here exe to execute
*/

/*
typedef enum {
	PATH_NORMAL = 0,
	//Anything virtual goes here
	PATH_ROOT, //	/
	PATH_MNT,	//	/mnt
	PATH_TMP,	//	/tmp
	PATH_HOME,	//	/home
	PATH_FTPD,	//	/ftpd
	PATH_HOMEUSER,
} dirtype_t;
*/

/*
	Our server will be divided in different VFS modules,
	which can take control of FTP commands when it's their
	working directory.
*/

typedef struct vfs_mod_s {
	int (*vfs_check)(ftpd_user_t*,const char*); //Check PWD; user,wdir
	int (*vfs_translate_wdir)(ftpd_user_t*,const char*,char*,int); //user,wdir,buf,maxLen
	int (*vfs_list)(ftpd_user_t*,int); //user,mlsd
	int (*vfs_retr)(ftpd_user_t*,char*); //user,path
	int (*vfs_stor)(ftpd_user_t*,char*); //user,path
	int (*vfs_appe)(ftpd_user_t*,char*); //user,path
	int (*vfs_size)(ftpd_user_t*,char*); //user,path
	int (*vfs_dele)(ftpd_user_t*,char*); //user,path
	int (*vfs_cwd)(ftpd_user_t*,char*);
	int (*vfs_mkd)(ftpd_user_t*,char*);
	
	struct vfs_mod_s* next;
} vfs_mod_t;

void vfs_add_mod(struct vfs_mod_s* mod);
vfs_mod_t* vfs_get_mod(ftpd_user_t* user,const char* wdir);
int vfs_list(ftpd_user_t* user,int mlsd);
int vfs_retr(ftpd_user_t* user,char* path);
int vfs_stor(ftpd_user_t* user,char* path);
int vfs_appe(ftpd_user_t* user,char* path);
int vfs_dele(ftpd_user_t* user,char* path);
int vfs_size(ftpd_user_t* user,char* path);
int vfs_cwd(ftpd_user_t* user,char* path);
int vfs_mkd(ftpd_user_t* user,char* path);

//Replaces '\\' to '/'
int vfs_fix_slashes(char* buf);

int vfs_changepath(ftpd_user_t* user,char* newpath);

void vfs_reply_dir(dataconn_t* dc,int mlsd,
	int dir,int year,int month,int day,
	int hour,int minute,int second,
	uint64_t size,const char* name);

int vfs_fallback_start_retr(ftpd_user_t* user,char* path,uint64_t size,int is_file);
int vfs_fallback_start_stor(ftpd_user_t* user,char* path);
	
#endif