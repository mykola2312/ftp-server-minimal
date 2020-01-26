#include "ftpd.h"
#include "ftpd_vfs.h"
#include <WS2tcpip.h>
#include <lmaccess.h>
#include <lm.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <math.h>

ftpd_t server;

static void ftpd_make_nonblocking(conn_t* conn)
{
	unsigned long ul = 1;
	ioctlsocket(conn->s, FIONBIO, &ul);
	printf_d("ftpd_make_nonblocking %d\n",conn->s);
}

extern struct vfs_mod_s mod_root;
extern struct vfs_mod_s mod_mnt;
extern struct vfs_mod_s mod_home;
extern struct vfs_mod_s mod_tmp;
#ifdef MOD_FTPD
extern struct vfs_mod_s mod_ftpd;
#endif

void ftpd_init()
{
    vfs_add_mod(&mod_root);
    vfs_add_mod(&mod_mnt);
    vfs_add_mod(&mod_home);
    vfs_add_mod(&mod_tmp);
#ifdef MOD_FTPD
    vfs_add_mod(&mod_ftpd);
#endif
}

int ftpd_start_server(struct in_addr extip,int port)
{
	int i,j;
	if((server.local.s = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP)) == INVALID_SOCKET)
		return 1;
	for(i = 0; i < MAX_USERS; i++)
	{
		server.userslots[i] = 0;
		for(j = 0; j < MAX_DATACHANNELS; j++)
			server.user[i].dataslots[j] = 0;
	}
	
	server.local.addr.sin_family = AF_INET;
	server.local.addr.sin_addr.s_addr = INADDR_ANY;
	server.local.addr.sin_port = htons(port);
	
	if(bind(server.local.s,(const struct sockaddr*)&server.local.addr,
		sizeof(struct sockaddr_in)) == SOCKET_ERROR)
	{
		closesocket(server.local.s);
		return 1;
	}
	
	listen(server.local.s,SOMAXCONN);
	ftpd_make_nonblocking(&server.local);
	server.extip = extip;
	return 0;
}

//User thread

static void ftpd_prepare_conn(conn_t* conn)
{
	conn->sendbuf = NULL;
	conn->sendbufsize = 0;
	conn->sendbufoff = 0;
}

//This function will automatically send buffer each call until buffer end
static int ftpd_conn_do_send(conn_t* conn)
{
	int sent = 0;
	if(conn->sendbufsize)
	{
		//printf_d("sending: %s\n",(const char*)conn->sendbuf);
		sent = send(conn->s,(const char*)conn->sendbuf+conn->sendbufoff,
			MIN(1024,conn->sendbufsize),0);
		if(sent < 0)
			conn->sendbufsize = 0; //On error we terminate transmission
		else
		{
			conn->sendbufsize -= sent;
			conn->sendbufoff += sent;
			printf_d("%d bytes sent to %p connection (remaining %d at off %d)\n",sent,conn,
				conn->sendbufsize,conn->sendbufoff);
		}
	}
	
	if(conn->sendbufsize <= 0)
	{
		free(conn->sendbuf);
		ftpd_prepare_conn(conn);
		printf_d("transmission for %p is over\n",conn);
	}
	return sent;
}

void ftpd_conn_send(conn_t* conn,const char* buf,size_t len)
{
	if(!conn->sendbuf) conn->sendbuf = (char*)malloc(len);
	else conn->sendbuf = (char*)realloc(conn->sendbuf,conn->sendbufsize+len);
	memcpy((char*)conn->sendbuf+conn->sendbufsize,buf,len);
	conn->sendbufsize += len;
}

ftpd_user_t* ftpd_open_session(conn_t* conn)
{
	int i;
	ftpd_user_t* user;
	//Find slot
	printf_d("ftpd_open_session for %d\n",conn->s);
	for(i = 0; i < MAX_USERS; i++)
	{
		if(BMP_GET(server.userslots,i) == 0)
		{
			user = &server.user[i];
			//Alloc slot
			BMP_ON(server.userslots,i);
			printf_d("allocated slot %d (%p) for %d\n",i,user,conn->s);
			break;
		}
	}
	if(i == MAX_USERS) NULL;
	
	memset(user,'\0',sizeof(ftpd_user_t));
	memcpy(&user->control,conn,sizeof(conn_t));
	ftpd_prepare_conn(&user->control);
	ftpd_make_nonblocking(&user->control);

	user->command_cur = 0;
	user->disconnected = 0;
	user->dlcommand = 0;
	user->rest = 0;
	strcpy(user->wdir,"/");
	return user;
}

//creates socket to connect (ACTIVE)
static dataconn_t* ftpd_data_open_active(ftpd_user_t* user,conn_t* dstconn)
{
	int i;
	dataconn_t* dc;
	
	for(i = 0; i < MAX_DATACHANNELS; i++)
	{
		if(BMP_GET(user->dataslots,i) == 0)
		{
			dc = &user->data[i];
			break;
		}
	}
	if(i == MAX_DATACHANNELS) return NULL;
	
	memcpy(&dc->conn.addr,dstconn,sizeof(conn_t));
	ftpd_prepare_conn(&dc->conn);
	
	dc->conn.s = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
	dc->conn.addr.sin_family = AF_INET;
	dc->lconn.s = 0;
	
	//Should we use nonblocking connect?
	//ftpd_make_nonblocking(&dc->conn);
	
	dc->type = CONN_ACTIVE;
	dc->event = NULL;
	dc->close = NULL;
	user->lastdc = i;
	BMP_ON(user->dataslots,i);
	return dc;
}

//allocates and binds new socket (PASSIVE)
static dataconn_t* ftpd_data_open_passive(ftpd_user_t* user)
{
	int i,slot;
	dataconn_t* dc;
	
	for(i = 0; i < MAX_DATACHANNELS; i++)
	{
		if(BMP_GET(user->dataslots,i) == 0)
		{
			dc = &user->data[i];
			break;
		}
	}
	if(i == MAX_DATACHANNELS) return NULL;
	slot = i;
	
	dc->conn.s = 0;
	dc->lconn.s = socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
	printf_d("datachannel passive socket %d\n",dc->lconn.s);
	
	dc->lconn.addr.sin_family = AF_INET;
	dc->lconn.addr.sin_addr.s_addr = INADDR_ANY;
	
	for(i = MIN_USRPORT; i < MAX_USRPORT; i++)
	{
		dc->lconn.addr.sin_port = htons(i);
		if(!bind(dc->lconn.s,(const struct sockaddr*)
			&dc->lconn.addr,sizeof(struct sockaddr_in)))
		{
			break;
		}
	}
	if(i == MAX_USRPORT)
	{
		closesocket(dc->lconn.s);
		return NULL;
	}
	
	listen(dc->lconn.s,1);
	
	ftpd_make_nonblocking(&dc->lconn);
	
	dc->type = CONN_PASSIVE;
	dc->event = NULL;
	dc->close = NULL;
	user->lastdc = slot;
	BMP_ON(user->dataslots,slot);
	return dc;
}

void ftpd_data_close(ftpd_user_t* user,dataconn_t* dc)
{
	unsigned int slot;
	
	if(dc->close) dc->close(user,dc);
	slot = (unsigned int)((ptrdiff_t)dc-(ptrdiff_t)&user->data)/sizeof(dataconn_t);
	if(dc->hFile) CloseHandle(dc->hFile);
	if(dc->lconn.s) closesocket(dc->lconn.s);
	if(dc->conn.s) closesocket(dc->conn.s);
	
	memset(dc,'\0',sizeof(dataconn_t));
	BMP_OFF(user->dataslots,slot);
}

void ftpd_close_session(ftpd_user_t* user)
{
	unsigned int i,slot;
	
	slot = (unsigned int)((ptrdiff_t)user-(ptrdiff_t)&server.user)/sizeof(ftpd_user_t);
	closesocket(user->control.s);
	if(user->control.sendbuf)
		free(user->control.sendbuf);
	
	//Close data channels
	for(i = 0; i < MAX_DATACHANNELS; i++)
	{
		dataconn_t* dc;
		if(!BMP_GET(user->dataslots,i)) continue;
		dc = &user->data[i];
		
		ftpd_data_close(user,dc);
	}
	
	memset(user,'\0',sizeof(ftpd_user_t));
	BMP_OFF(server.userslots,slot); //Free slot
}

typedef enum {
	POLL_CONTROL = 0,
	POLL_DATA,
} polltype_t;

typedef struct {
	SOCKET s;
	int type;
	ftpd_user_t* user;
	dataconn_t* dc;
} pollpair_t;

static void pollpair_insert_user(pollpair_t* pair,int idx,ftpd_user_t* user)
{
	pair[idx].s = user->control.s;
	pair[idx].type = POLL_CONTROL;
	pair[idx].user = user;
}

static void pollpair_insert_dataconn(pollpair_t* pair,int idx,
	ftpd_user_t* user,dataconn_t* dc)
{
	pair[idx].s = dc->conn.s;
	pair[idx].type = POLL_DATA;
	pair[idx].user = user;
	pair[idx].dc = dc;
}

void ftpd_user_send_reply(ftpd_user_t* user,const char* text)
{
	printf_d("send REPLY: %s\n",text);
	ftpd_conn_send(&user->control,text,strlen(text));
}

void ftpd_dc_send_reply(dataconn_t* dc,const char* text)
{
	ftpd_conn_send(&dc->conn,text,strlen(text));
}

void ftpd_user_send_replyf(ftpd_user_t* user,const char* fmt,...)
{
	char buffer[512];
	va_list ap;
	
	va_start(ap,fmt);
	vsnprintf(buffer,sizeof(buffer),fmt,ap);
	va_end(ap);
	
	ftpd_user_send_reply(user,buffer);
}

void ftpd_dc_send_replyf(dataconn_t* dc,const char* fmt,...)
{
	char buffer[512];
	va_list ap;
	
	va_start(ap,fmt);
	vsnprintf(buffer,sizeof(buffer),fmt,ap);
	va_end(ap);
	
	ftpd_dc_send_reply(dc,buffer);
}

void ftpd_user_disconnect(ftpd_user_t* user,const char* reason)
{
	ftpd_user_send_reply(user,reason);
	user->disconnected = 1;
}

static int is_private_ip(struct in_addr addr)
{
	const int num = 4;
	const char* ips[] = {"127.0.0.0","10.0.0.0","172.16.0.0","192.168.0.0"};
	const int lens[] = {8,8,12,16};
	int i;
	
	for(i = 0; i < num; i++)
	{
		uint32_t mask;
		struct in_addr laddr;
		laddr.s_addr = inet_addr(ips[i]);
		mask = ~((uint32_t)pow(2,32-lens[i])-1);
		if(laddr.s_addr == addr.s_addr&mask)
			return 1;
	}
	return 0;
}

static int cmdcmp(const char* line,const char* cmd)
{
	int i;
	for(i = 0; i < strlen(cmd); i++)
		if(tolower(line[i]) != tolower(cmd[i])) return 0;
	return 1;
}

int ftpd_user_make_dataconn(ftpd_user_t* user)
{
	dataconn_t* dc;
	
	dc = &user->data[user->lastdc];
	if(dc->type == CONN_ACTIVE)
	{
		//dc->conn.s is nonblocking!!!! FIX ME
		if(connect(dc->conn.s,(const struct sockaddr*)
			&dc->conn.addr,sizeof(struct sockaddr_in)))
		{
			ftpd_data_close(user,dc);
			return 1;
		}
	}
	else if(dc->type == CONN_PASSIVE)
	{
		WSAPOLLFD poll;
		poll.fd = dc->lconn.s;
		poll.events = POLLIN;
		printf_d("passive poll to %d\n",dc->lconn.s);
		if(WSAPoll(&poll,1,-1)) //3 seconds
		{
			if(poll.revents & POLLIN)
			{
				//Accept data conn
				int namelen;
				namelen = sizeof(struct sockaddr_in);
				dc->conn.s = accept(dc->lconn.s,(struct sockaddr*)&dc->conn.addr,&namelen);
				if(dc->conn.s == INVALID_SOCKET)
				{
					ftpd_data_close(user,dc);
					return 1;
				}
				ftpd_make_nonblocking(&dc->conn);
			}
			else
			{
				ftpd_data_close(user,dc);
				return 1;
			}
		} else
		{
			ftpd_data_close(user,dc);
			return 1;
		}
	}
	
	dc->type = CONN_CONNECTED;
	return 0;
}

dataconn_t* ftpd_user_get_cur_dataconn(ftpd_user_t* user)
{
	return &user->data[user->lastdc];
}

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

static int is_slash_at_end(const char* str,char slash)
{
	return str[strlen(str)-1] == slash;
}

static char ftpd_user_cur_drive(ftpd_user_t* user)
{
	return user->wdir[5];
}

void ftpd_list_event_end(void* u,void* d)
{
	ftpd_user_t* user;
	
	user = (ftpd_user_t*)u;
	
	ftpd_user_send_reply(user,"226 Transfer complete.\r\n");
}

//Wrapper for protocol level. Pass execution to virtual level
void ftpd_user_list_dir(ftpd_user_t* user,int mlsd)
{
	dataconn_t* dc;
	
	if(user->dlcommand == 0)
	{
		puts_d("LIST first stage");
		ftpd_user_send_reply(user,"150 Opening BINARY mode data connection for LIST.\r\n");
		user->dlcommand++;
	}
	else if(user->dlcommand == 1)
	{
		if(ftpd_user_make_dataconn(user))
		{
			ftpd_user_send_reply(user,"425 Can't open data connection.\r\n");
			user->dlcommand = 0;
			return;
		}
		dc = ftpd_user_get_cur_dataconn(user);
		dc->mode = DATACONN_SEND;
		
		//Actual command
		vfs_list(user,mlsd);
		
		user->dlcommand = 0;
		dc->event = ftpd_list_event_end;
	}
}

static struct in_addr get_local_ip()
{
    struct hostent* host;
    char hostname[256];

    gethostname(hostname,sizeof(hostname));
    host = gethostbyname(hostname);

    return *((struct in_addr*)host->h_addr_list[0]);
}

void ftpd_user_exec_command(ftpd_user_t* user)
{
	printf_d("got command: %s\n",user->command);
	//ftpd_user_send_reply(user,"200 Hello WOorld!\r\n");
	if(strlen(user->command) == 0) return;
	if(cmdcmp(user->command,"PWD"))
		ftpd_user_send_replyf(user,"257 \"%s\" is current directory.\r\n",user->wdir);
	else if(cmdcmp(user->command,"CWD"))
	{
		char buffer[MAX_PATH];
		char* path,*c;
		
		strncpy(buffer,user->command,MAX_PATH-1);
		if(!(path = strchr(buffer,' ')))
			ftpd_user_send_reply(user,"501 Syntax error in parameters or arguments.\r\n");
		else
		{
			path++;
			
			//if(vfs_retr(user,path))
			//	ftpd_user_send_reply(user,"550 Requested action not taken. File unavailable (e.g., file not found, no access).\r\n");
			if(vfs_changepath(user,path))
				ftpd_user_send_reply(user,"550 Requested action not taken. File unavailable (e.g., file not found, no access).\r\n");
			else ftpd_user_send_replyf(user,"250 CWD successful. \"%s\" is current directory.\r\n",user->wdir);
		}
	}
	else if(cmdcmp(user->command,"CDUP"))
	{
		if(vfs_changepath(user,".."))
			ftpd_user_send_reply(user,"550 Requested action not taken. File unavailable (e.g., file not found, no access).\r\n");
		else ftpd_user_send_replyf(user,"200 CDUP successful. \"%s\" is current directory.\r\n",user->wdir);
	}
	else if(cmdcmp(user->command,"LIST"))
	{
		ftpd_user_list_dir(user,0);
	}
	else if(cmdcmp(user->command,"MLSD"))
	{
		ftpd_user_list_dir(user,1);
	}
	else if(cmdcmp(user->command,"RETR"))
	{
		char buffer[MAX_PATH];
		char* path,*c;
		
		if(!BMP_GET(user->dataslots,user->lastdc))
			ftpd_user_send_reply(user,"503 Bad sequence of commands.\r\n");
		
		strncpy(buffer,user->command,MAX_PATH-1);
		if(!(path = strchr(buffer,' ')))
			ftpd_user_send_reply(user,"501 Syntax error in parameters or arguments.\r\n");
		else
		{
			path++;
			
			if(vfs_retr(user,path))
				ftpd_user_send_reply(user,"550 Requested action not taken. File unavailable (e.g., file not found, no access).\r\n");
		}
	}
	else if(cmdcmp(user->command,"STOR"))
	{
		char buffer[MAX_PATH];
		char* path,*c;
		
		if(!BMP_GET(user->dataslots,user->lastdc))
			ftpd_user_send_reply(user,"503 Bad sequence of commands.\r\n");
		
		strncpy(buffer,user->command,MAX_PATH-1);
		if(!(path = strchr(buffer,' ')))
			ftpd_user_send_reply(user,"501 Syntax error in parameters or arguments.\r\n");
		else
		{
			path++;
			
			if(vfs_stor(user,path))
				ftpd_user_send_reply(user,"550 Requested action not taken. File unavailable (e.g., file not found, no access).\r\n");
		}
	}
	else if(cmdcmp(user->command,"APPE"))
	{
		char buffer[MAX_PATH];
		char* path,*c;
		
		if(!BMP_GET(user->dataslots,user->lastdc))
			ftpd_user_send_reply(user,"503 Bad sequence of commands.\r\n");
		
		strncpy(buffer,user->command,MAX_PATH-1);
		if(!(path = strchr(buffer,' ')))
			ftpd_user_send_reply(user,"501 Syntax error in parameters or arguments.\r\n");
		else
		{
			path++;
			
			if(vfs_appe(user,path))
				ftpd_user_send_reply(user,"550 Requested action not taken. File unavailable (e.g., file not found, no access).\r\n");
		}
	}
	else if(cmdcmp(user->command,"DELE") || cmdcmp(user->command,"RMD"))
	{
		char buffer[MAX_PATH];
		char* path,*c;
		
		strncpy(buffer,user->command,MAX_PATH-1);
		if(!(path = strchr(buffer,' ')))
			ftpd_user_send_reply(user,"501 Syntax error in parameters or arguments.\r\n");
		else
		{
			path++;
			
			if(vfs_dele(user,path))
				ftpd_user_send_reply(user,"550 Requested action not taken. File unavailable (e.g., file not found, no access).\r\n");
		}
	}
	else if(cmdcmp(user->command,"SIZE"))
	{
		char buffer[MAX_PATH];
		char* path,*c;
		
		strncpy(buffer,user->command,MAX_PATH-1);
		if(!(path = strchr(buffer,' ')))
			ftpd_user_send_reply(user,"501 Syntax error in parameters or arguments.\r\n");
		else
		{
			path++;
			
			if(vfs_size(user,path))
				ftpd_user_send_reply(user,"550 Requested action not taken. File unavailable (e.g., file not found, no access).\r\n");
		}
	}
	else if(cmdcmp(user->command,"MKD"))
	{
		char buffer[MAX_PATH];
		char* path,*c;
		
		strncpy(buffer,user->command,MAX_PATH-1);
		if(!(path = strchr(buffer,' ')))
			ftpd_user_send_reply(user,"501 Syntax error in parameters or arguments.\r\n");
		else
		{
			path++;
			
			if(vfs_mkd(user,path))
				ftpd_user_send_reply(user,"550 Requested action not taken. File unavailable (e.g., file not found, no access).\r\n");
		}
	}
	else if(cmdcmp(user->command,"PORT")) //PORT %d,%d,%d,%d,%d,%d
	{
		dataconn_t* dc;
		conn_t dstconn;
		int a,b,c,d,hp,lp;
		
		if(!sscanf(user->command,"PORT %d,%d,%d,%d,%d,%d",&a,&b,&c,&d,&hp,&lp))
			ftpd_user_send_reply(user,"501 Syntax error in parameters or arguments.\r\n");
		else
		{
			dstconn.addr.sin_family = AF_INET;
			dstconn.addr.sin_addr.s_addr = (a << 24) | (b << 16) | (c << 8) | d;
			dstconn.addr.sin_port = htons((hp << 8) | lp);
			if(!(dc = ftpd_data_open_active(user,&dstconn)))
				ftpd_user_send_reply(user,"425 Can't open data connection.\r\n");
			else ftpd_user_send_reply(user,"200 PORT command successful.\r\n");
		}
	}
	else if(cmdcmp(user->command,"PASV"))
	{
		dataconn_t* dc;
		struct in_addr addr;
		int port;
		
		if(!(dc = ftpd_data_open_passive(user)))
			ftpd_user_send_reply(user,"425 Can't open data connection.\r\n");
		else
		{
			addr = is_private_ip(user->control.addr.sin_addr) ? server.extip : get_local_ip();
			port = ntohs(dc->lconn.addr.sin_port);
			
			//227 Entering Passive Mode (192,168,150,90,195,149).
			ftpd_user_send_replyf(user,"227 Entering Passive Mode (%d,%d,%d,%d,%d,%d).\r\n",
				(addr.s_addr&0xFF),(addr.s_addr>>8)&0xFF,(addr.s_addr>>16)&0xFF,
				(addr.s_addr>>24)&0xFF,port>>8,port&0xFF);
		}
	}
	else if(cmdcmp(user->command,"REST"))
	{
		if(!sscanf(user->command,"REST %llu",&user->rest))
			ftpd_user_send_reply(user,"501 Syntax error in parameters or arguments.\r\n");
		else ftpd_user_send_replyf(user,"350 Rest supported. Restarting at %lu\r\n",user->rest);
		printf("REST %llu\n",user->rest);
	}
	else if(cmdcmp(user->command,"FEAT"))
	{
		ftpd_user_send_reply(user,"211-Features:\r\n"
			" REST STREAM\r\n"
			" SIZE\r\n"
			" MLSD modify*;perm*;size*;type*;unique*;UNIX.group*;UNIX.mode*;UNIX.owner*;\r\n"
			" UTF8\r\n"
			"211 End\r\n");
	}
	else if(cmdcmp(user->command,"QUIT"))
	{
		user->disconnected = 1;
		ftpd_user_send_reply(user,"200 OK\r\n");
	}
	else if(cmdcmp(user->command,"TYPE")) ftpd_user_send_reply(user,"200 OK\r\n");
	else if(cmdcmp(user->command,"USER")) ftpd_user_send_reply(user,"200 OK\r\n");
	else if(cmdcmp(user->command,"PASS")) ftpd_user_send_reply(user,"200 OK\r\n");
	else if(cmdcmp(user->command,"NOOP")) ftpd_user_send_reply(user,"200 OK\r\n");
	else if(cmdcmp(user->command,"OPTS")) ftpd_user_send_reply(user,"200 OK\r\n");
	else if(cmdcmp(user->command,"AUTH")) ftpd_user_send_reply(user,"500 AUTH not understood.\r\n");
	else if(cmdcmp(user->command,"SYST")) ftpd_user_send_reply(user,"215 UNIX Type: L8\r\n");
	else ftpd_user_send_reply(user,"202 Command not implemented, superfluous at this site.\r\n");
}

int ftpd_user_putc(ftpd_user_t* user,char ch)
{
	if(user->command_cur >= MAX_COMMANDLEN)
		return 1;
	user->command[user->command_cur++] = ch;
	return 0;
}

void ftpd_user_recv(ftpd_user_t* user)
{
	int rd;
	char ch;
	
	//While we have data
	while(recv(user->control.s,&ch,1,0) > 0)
	{
		if(ch == '\n')
		{
			if(user->command_cur <= 0) return;
			user->command_cur--;
			user->command[user->command_cur] = '\0';
			ftpd_user_exec_command(user);
			user->command_cur = 0;
		}
		else
		{
			if(ftpd_user_putc(user,ch) == 1)
			{
				ftpd_user_disconnect(user,"501 Syntax error in parameters or arguments.\r\n");
				return;
			}
		}
	}
}

void ftpd_user_send(ftpd_user_t* user)
{
	ftpd_conn_do_send(&user->control);
}

//ftpd_data_recv(ppair[i].dc);
void ftpd_data_recv(ftpd_user_t* user,dataconn_t* dc)
{
	DWORD dwWrote;
	
	char buf[1024];
	int rd;
	
	rd = recv(dc->conn.s,buf,1024,0);
	if(rd <= 0) ftpd_data_close(user,dc);
	
	//fwrite(buf,rd,1,dc->fp);
	WriteFile(dc->hFile,buf,rd,&dwWrote,NULL);
}

//ftpd_data_send(ppair[i].dc);
void ftpd_data_send(ftpd_user_t* user,dataconn_t* dc)
{
	printf_d("DATACONN MODE %d\n",dc->mode);
	if(dc->mode == DATACONN_SEND_FP)
	{
		char buf[1024];
		DWORD dwRead;
		
		//rd = fread(buf,1,1024,dc->fp);
		ReadFile(dc->hFile,buf,1024,&dwRead,NULL);
		
		send(dc->conn.s,buf,dwRead,0);
		
		if(dwRead < 1024) //Last block of data in file
		{
			if(dc->event) dc->event(user,dc);
			ftpd_data_close(user,dc);
		}
	}
	else
	{
		printf_d("CONN BUFSIZE %d\n",dc->conn.sendbufsize);
		ftpd_conn_do_send(&dc->conn);
		if(dc->conn.sendbufsize == 0) //End
		{
			if(dc->event) dc->event(user,dc);
			ftpd_data_close(user,dc);
		}
	}
}

void ftpd_server_loop()
{
	//WSAPOLLFD* polls;
	//pollpair_t* ppair;
	//32 reserved to special purposes
	static WSAPOLLFD polls[MAX_POLLSLOTS];
	static pollpair_t ppair[MAX_POLLSLOTS];
	
	size_t count;
	int i,j;
	
	count = 0;
	polls[0].fd = server.local.s;
	polls[0].events = POLLIN;
	count++;
	
	//polls = (WSAPOLLFD*)calloc(count,sizeof(WSAPOLLFD));
	//ppair = (pollpair_t*)calloc(count,sizeof(pollpair_t));
	
	for(i = 0; i < MAX_USERS; i++)
	{
		ftpd_user_t* user;
		
		if(!BMP_GET(server.userslots,i)) continue;
		user = &server.user[i];
		printf_d("user %p\n",user);
		
		polls[count].fd = user->control.s;
		polls[count].events = POLLIN;
		pollpair_insert_user(ppair,count,user);
		//count++;

		//If we have data for user
		if(user->control.sendbuf)
		{
			puts_d("we have data for user");
			//polls[count].fd = user->control.s;
			polls[count].events |= POLLOUT;
			//pollpair_insert_user(ppair,count,user);
		}
		count++;

		for(j = 0; j < MAX_DATACHANNELS; j++)
		{
			dataconn_t* dc;
			if(!BMP_GET(user->dataslots,j)) continue;
			dc = &user->data[j];
			if(dc->type != CONN_CONNECTED) continue;
			if(dc->mode == DATACONN_SEND)
			{
				polls[count].fd = dc->conn.s;
				polls[count].events = POLLOUT;
				pollpair_insert_dataconn(ppair,count,user,dc);
				count++;
			}
			else if(dc->mode == DATACONN_SEND_FP)
			{
				polls[count].fd = dc->conn.s;
				polls[count].events = POLLOUT;
				pollpair_insert_dataconn(ppair,count,user,dc);
				count++;
			}
			else
			{
				polls[count].fd = dc->conn.s;
				polls[count].events = POLLIN;
				pollpair_insert_dataconn(ppair,count,user,dc);
				count++;
			}
		}
	}
	
	printf_d("prepoll count %d\n",count);
	for(i = 0; i < count; i++)
	{
		printf_d("%s %d user %p\n",polls[i].events == POLLIN ? "POLLIN" 
			: (polls[i].events == POLLOUT ? "POLLOUT" : "POLLIN|POLLOUT"),polls[i].fd,ppair[i].user);
	}
	if(WSAPoll(polls,count,-1))
	{
		for(i = 0; i < count; i++)
		{
			printf_d("%d\n",count);
			if(i != 0 && count > 1)
			{
				printf_d("Polling %s %d\n",ppair[i].type == POLL_CONTROL ? "POLL_CONTROL" : "POLL_DATA",i);
				if(ppair[i].type == POLL_CONTROL)
				{
					//Process control IO
					//Delayed mode requires immediately send
					if(ppair[i].user->dlcommand != 0)
					{
						if(polls[i].revents & POLLOUT)
						{
							puts_d("ftpd_user_send");
							ftpd_user_send(ppair[i].user);
						}
						
						//Execute delayed command
						ftpd_user_exec_command(ppair[i].user);
					}
					else
					{
						if(polls[i].revents & POLLIN)
						{
							puts_d("ftpd_user_recv");
							ftpd_user_recv(ppair[i].user);
						}
						
						if(polls[i].revents & POLLOUT)
						{
							puts_d("ftpd_user_send");
							ftpd_user_send(ppair[i].user);
						}
						
						if((polls[i].revents & (POLLERR | POLLHUP)) || ppair[i].user->disconnected)
						{
							//Disconnect control channel
							ftpd_close_session(ppair[i].user);
						}
					}
				}
				else
				{
					//Process data IO
					if(polls[i].revents & POLLIN)
					{
						ftpd_data_recv(ppair[i].user,ppair[i].dc);
					}
					
					if(polls[i].revents & POLLOUT)
					{
						puts_d("ftpd_data_send");
						ftpd_data_send(ppair[i].user,ppair[i].dc);
					}
					
					if(polls[i].revents & (POLLERR | POLLHUP))
					{
						//Disconnect data channel
						ftpd_data_close(ppair[i].user,ppair[i].dc);
					}
				}
			}
			else
			{
				if(polls[0].revents & POLLIN)
				{
					conn_t conn;
					int namelen;
					ftpd_user_t* user;
					
					puts_d("accept");
					
					namelen = sizeof(struct sockaddr_in);
					conn.s = accept(server.local.s,(struct sockaddr*)&conn.addr,&namelen);
					printf_d("accept fd %d\n",conn.s);
					if(!(user = ftpd_open_session(&conn)))
					{
						closesocket(conn.s);
						printf_d("%s failed to connected (no free slots)\n",inet_ntoa(conn.addr.sin_addr));
					}
					else
					{
						printf_d("Connected %s\n",inet_ntoa(conn.addr.sin_addr));
						ftpd_user_send_reply(user,"220 Welcome to ftpd.c\r\n");
					}
				}
			}
		}
	}
}

void ftpd_main_loop()
{
	while(1)
	{
		ftpd_server_loop();
	}
}