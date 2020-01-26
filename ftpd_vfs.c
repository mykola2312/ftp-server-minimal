#include "ftpd_vfs.h"
#include "ftpd_util.h"

static struct vfs_mod_s* first = NULL;

void vfs_add_mod(struct vfs_mod_s* mod)
{
	struct vfs_mod_s* elem;
	
	if(first)
	{
		elem = first;
		while(elem->next) elem = elem->next;
		mod->next = NULL;
		elem->next = mod;
	} else first = mod;
}

vfs_mod_t* vfs_get_mod(ftpd_user_t* user,const char* wdir)
{
	vfs_mod_t* mod;
	
	mod = first;
	while(mod)
	{
		if(mod->vfs_check(user,wdir)) break;
		mod = mod->next;
	}
	
	return mod;
}

int vfs_list(ftpd_user_t* user,int mlsd)
{
	vfs_mod_t* mod;
	
	mod = vfs_get_mod(user,user->wdir);
	if(!mod) return 1;
	if(mod->vfs_list)
		return mod->vfs_list(user,mlsd);
	return 1;
}

int vfs_fallback_start_retr(ftpd_user_t* user,char* path,uint64_t size,int is_file)
{
	dataconn_t* dc;
	
	if(user->dlcommand == 0)
	{
		ftpd_user_send_replyf(user,"150 Opening BINARY mode data connection for %s (%llu).\r\n",
			path,size);
		user->dlcommand++;
	}
	else if(user->dlcommand == 1)
	{
		if(ftpd_user_make_dataconn(user))
		{
			ftpd_data_close(user,dc);
			ftpd_user_send_reply(user,"425 Can't open data connection.\r\n");
			user->dlcommand = 0;
			return 0;
		}
		
		dc = ftpd_user_get_cur_dataconn(user);
		if(is_file)
		{
			DWORD dwRestHigh,dwRestLow;
			
			dwRestLow = user->rest & 0xFFFFFFFF;
			dwRestHigh = user->rest >> 32;
			
			SetFilePointer(dc->hFile,dwRestLow,&dwRestHigh,FILE_CURRENT);
			dc->mode = DATACONN_SEND_FP;
		} else dc->mode = DATACONN_SEND;
		
		dc->event = ftpd_list_event_end;
		user->dlcommand = 0;
	}
	return 0;
}

int vfs_fallback_start_stor(ftpd_user_t* user,char* path)
{
	dataconn_t* dc;
	
	if(user->dlcommand == 0)
	{
		ftpd_user_send_replyf(user,"150 Opening BINARY mode data connection for %s\r\n",
			path);
		user->dlcommand++;
	}
	else if(user->dlcommand == 1)
	{
		DWORD dwRestHigh,dwRestLow;
		
		if(ftpd_user_make_dataconn(user))
		{
			ftpd_data_close(user,dc);
			ftpd_user_send_reply(user,"425 Can't open data connection.\r\n");
			user->dlcommand = 0;
			return 0;
		}
		
		dc = ftpd_user_get_cur_dataconn(user);
		
		dwRestLow = user->rest & 0xFFFFFFFF;
		dwRestHigh = user->rest >> 32;
			
		SetFilePointer(dc->hFile,dwRestLow,&dwRestHigh,FILE_CURRENT);
		dc->mode = DATACONN_RECV_FP;
		
		dc->close = ftpd_list_event_end;
		user->dlcommand = 0;
	}
	return 0;
}

int vfs_retr(ftpd_user_t* user,char* path)
{
	vfs_mod_t* mod;
	
	if(path[0] == '/')
		mod = vfs_get_mod(user,path);
	else mod = vfs_get_mod(user,user->wdir);
	
	if(!mod) return 1;
	if(mod->vfs_retr)
		return mod->vfs_retr(user,path);
	return 1;
}

int vfs_stor(ftpd_user_t* user,char* path)
{
	vfs_mod_t* mod;
	
	if(path[0] == '/')
		mod = vfs_get_mod(user,path);
	else mod = vfs_get_mod(user,user->wdir);
	
	if(!mod) return 1;
	if(mod->vfs_stor)
		return mod->vfs_stor(user,path);
	return 1;
}

int vfs_appe(ftpd_user_t* user,char* path)
{
	vfs_mod_t* mod;
	
	if(path[0] == '/')
		mod = vfs_get_mod(user,path);
	else mod = vfs_get_mod(user,user->wdir);
	
	if(!mod) return 1;
	if(mod->vfs_appe)
		return mod->vfs_appe(user,path);
	return 1;
}

int vfs_dele(ftpd_user_t* user,char* path)
{
	vfs_mod_t* mod;
	
	if(path[0] == '/')
		mod = vfs_get_mod(user,path);
	else mod = vfs_get_mod(user,user->wdir);
	
	if(!mod) return 1;
	if(mod->vfs_dele)
		return mod->vfs_dele(user,path);
	return 1;
}

int vfs_size(ftpd_user_t* user,char* path)
{
	vfs_mod_t* mod;
	
	if(path[0] == '/')
		mod = vfs_get_mod(user,path);
	else mod = vfs_get_mod(user,user->wdir);
	
	if(!mod) return 1;
	if(mod->vfs_size)
		return mod->vfs_size(user,path);
	return 1;
}

int vfs_cwd(ftpd_user_t* user,char* path)
{
	vfs_mod_t* mod;
	
	if(path[0] == '/')
		mod = vfs_get_mod(user,path);
	else mod = vfs_get_mod(user,user->wdir);
	
	if(!mod) return 1;
	if(mod->vfs_cwd)
		return mod->vfs_cwd(user,path);
	return 1;
}

int vfs_mkd(ftpd_user_t* user,char* path)
{
	vfs_mod_t* mod;
	
	if(path[0] == '/')
		mod = vfs_get_mod(user,path);
	else mod = vfs_get_mod(user,user->wdir);
	
	if(!mod) return 1;
	if(mod->vfs_mkd)
		return mod->vfs_mkd(user,path);
	return 1;
}

//Replaces '\\' to '/'
int vfs_fix_slashes(char* buf)
{
	int i;
	for(i = 0; buf[i]; i++)
		if(buf[i] == '\\') buf[i] = '/';
}

int vfs_changepath(ftpd_user_t* user,char* path)
{
	char buf[MAX_PATH];
	char newpath[MAX_PATH];
	int last,cwd_root;
	//First, we need to determine is it absolute or relative path
	
	strncpy(newpath,path,MAX_PATH-1);
	vfs_fix_slashes(newpath);
	
	cwd_root = 0;
	
	// ../dir1
	if(!memcmp(newpath,"..",2) && strcmp(user->wdir,"/"))
	{
		char* c;
		
		strncpy(buf,user->wdir,MAX_PATH-1);
		//wdir /mnt/D/dir1/dir2
		c = strrchr(buf,'/');
		if(c == buf)
		{
			//We got root dir
			strncpy(buf,"/",MAX_PATH-1);
			cwd_root = 1;
			
		}
		else
		{
			*c = '\0';
			if(newpath[2] != '\0')
				strncat(buf,newpath+2,MAX_PATH-1);
		}
	}
	else
	{
		//new path /mnt/D/dir1
		if(newpath[0] == '/') //Absolute path
		{
			strncpy(buf,newpath,MAX_PATH);
		}
		else
		{
			//FIXME BUG! CWD home => "home" is current directory (without wdir slash)
			buf[0] = '\0';
			strncpy(buf,user->wdir,MAX_PATH-1);
			if(strcmp(user->wdir,"/")) //If not "/" root dir
				strncat(buf,"/",MAX_PATH-1);
			strncat(buf,newpath,MAX_PATH-1);
		}
	}
	
	vfs_fix_slashes(buf);
	
	if(strcmp(buf,"/") && cwd_root == 0)
	{
		//Remove last slash
		last = strlen(buf)-1;
		if(buf[last] == '/') buf[last] = '\0';
	}
	
	//Now call vfs_cwd
	if(vfs_cwd(user,buf))
		return 1;
	
	strncpy(user->wdir,buf,MAX_PATH-1);
	return 0;
}

void vfs_reply_dir(dataconn_t* dc,int mlsd,
	int dir,int year,int month,int day,
	int hour,int minute,int second,
	uint64_t size,const char* name)
{
	if(mlsd)
	{
		ftpd_dc_send_replyf(dc,"type=%s;modify=%04d%02d%02d%02d%02d%02d;size=%llu; %s\r\n",
			dir ? "dir" : "file",year,month,day,hour,minute,second,size,name);
	}
	else
	{
		ftpd_dc_send_replyf(dc,"%crwxr-xr-x 1 ftp ftp %llu %s %02d %04d %s\r\n",
			dir ? 'd' : '-',size,get_month(month),day,year,name);
	}
}