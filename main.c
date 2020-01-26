#include "ftpd.h"
#include <WinSock2.h>

struct in_addr get_local_ip()
{
    struct hostent* host;
    char hostname[256];

    gethostname(hostname,sizeof(hostname));
    host = gethostbyname(hostname);

    return *((struct in_addr*)host->h_addr_list[0]);
}

int main()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2,2),&wsa);

    ftpd_init();

    ftpd_start_server(get_local_ip(),23);
    ftpd_main_loop();

    return 0;
}