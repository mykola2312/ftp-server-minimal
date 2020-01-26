#include "ftpd_vfs.h"

static int mod_check(ftpd_user_t* user,const char* path);
static int mod_list(ftpd_user_t* user,int mlsd);
static int mod_cwd(ftpd_user_t* user,char* path);

struct vfs_mod_s mod_root = {
	.vfs_check = mod_check,
	.vfs_translate_wdir = NULL,
	.vfs_list = mod_list,
	.vfs_retr = NULL,
	.vfs_stor = NULL,
	.vfs_appe = NULL,
	.vfs_dele = NULL,
	.vfs_size = NULL,
	.vfs_cwd = mod_cwd,
	.vfs_mkd = NULL
};

static int mod_check(ftpd_user_t* user,const char* path)
{
	return !strcmp(path,"/");
}

static char* dirs[] = {
	"mnt",
	"tmp",
#ifdef _ENABLE_MOD_FTPD
    "ftpd",
#endif
	"home"
};

static int mod_list(ftpd_user_t* user,int mlsd)
{
	int i;
	dataconn_t* dc;
	
	dc = ftpd_user_get_cur_dataconn(user);
	for(i = 0; i < (sizeof(dirs)/sizeof(const char*)); i++)
		vfs_reply_dir(dc,mlsd,1,1970,1,1,0,0,0,0,dirs[i]);
	return 0;
}

static int mod_cwd(ftpd_user_t* user,char* path)
{
	return 0;
}