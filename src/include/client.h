
#ifndef __CLIENT_CONNECTION__
#define __CLIENT_CONNECTION__

struct in_addr
{
    unsigned long s_addr;
};

struct sockaddr_in
{
    short sin_family;
    unsigned short sin_port;
    struct in_addr sin_addr;
    char sin_zero[8];
};

struct ClientConnection
{
    int fd;
    struct sockaddr_in clientaddr;
    struct ClientConnection *next;
    struct ClientConnection *prev;
};

#endif