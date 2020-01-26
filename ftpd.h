#ifndef __FTPD_H
#define __FTPD_H

#ifdef __MINGW32__
#undef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <WinSock2.h>
#include <stdint.h>
#include "ftpd_util.h"
//#include "ftpd_vfs.h"

#define MAX_USERS 64
#define MAX_DATACHANNELS 128
#define MAX_USERNAMELEN 32
#define MAX_COMMANDLEN 256
#define MAX_POLLSLOTS MAX_USERS*MAX_DATACHANNELS+32

#define MIN_USRPORT 5000
#define MAX_USRPORT 6480

//Bitmap functions for static allocations
#define BMP_DEF(name,size) uintptr_t name[size/sizeof(uintptr_t)]
#define BMP_GET(bmp,bit) ((bmp[bit/sizeof(uintptr_t)] >> (bit%sizeof(uintptr_t))) & 1)
#define BMP_ON(bmp,bit) (bmp[bit/sizeof(uintptr_t)] |= 1<<(bit%sizeof(uintptr_t)))
#define BMP_OFF(bmp,bit) (bmp[bit/sizeof(uintptr_t)] &= ~(1<<(bit%sizeof(uintptr_t))))

#define MIN(a,b) (((a)<(b))?(a):(b))

//#define DEBUG_PRINT

#ifdef DEBUG_PRINT
#define printf_d printf
#define puts_d puts
#else
#define printf_d
#define puts_d
#endif


typedef enum {
	CONN_ACTIVE = 0,
	CONN_PASSIVE,
	CONN_CONNECTED, //No operations needed
} conntype_t;

typedef struct {
	SOCKET s;
	struct sockaddr_in addr;
	
	char* sendbuf;
	size_t sendbufsize;
	size_t sendbufoff;
} conn_t;

typedef enum {
	DATACONN_SEND = 0,
	DATACONN_SEND_FP,
	DATACONN_RECV_FP
} datamodes_t;

typedef void (*dataconn_event_t)(void* user,void* dc);

typedef struct {
	conn_t conn;
	conn_t lconn; //For server sockets
	
	HANDLE hFile;
	int mode;
	int type;
	
	dataconn_event_t event;
	dataconn_event_t close;
} dataconn_t;

typedef struct {
	conn_t control;
	dataconn_t data[MAX_DATACHANNELS];
	BMP_DEF(dataslots,MAX_DATACHANNELS);
	char command[MAX_COMMANDLEN];
	size_t command_cur;
	int disconnected;
	char wdir[MAX_PATH];
	int lastdc;
	int dlcommand; //Delayed command after send
	uint64_t rest; //Offset in file
	void* udata; //For special purposes
} ftpd_user_t;

typedef struct {
	conn_t local;
	ftpd_user_t user[MAX_USERS];
	BMP_DEF(userslots,MAX_USERS);
	struct in_addr extip;
} ftpd_t;

void ftpd_init();
int ftpd_start_server(struct in_addr extip,int port);
void ftpd_main_loop();
ftpd_user_t* ftpd_open_session(conn_t* conn);
void ftpd_data_close(ftpd_user_t* user,dataconn_t* dc);
void ftpd_close_session(ftpd_user_t* user);
void ftpd_conn_send(conn_t* conn,const char* buf,size_t len);
void ftpd_user_send_reply(ftpd_user_t* user,const char* text);
void ftpd_dc_send_reply(dataconn_t* dc,const char* text);
void ftpd_user_send_replyf(ftpd_user_t* user,const char* fmt,...);
void ftpd_dc_send_replyf(dataconn_t* dc,const char* fmt,...);
void ftpd_user_disconnect(ftpd_user_t* user,const char* reason);
int ftpd_user_make_dataconn(ftpd_user_t* user);
dataconn_t* ftpd_user_get_cur_dataconn(ftpd_user_t* user);
void ftpd_fix_slashes(char* str);

void ftpd_list_event_end(void* u,void* d);

extern ftpd_t server;

#endif