#ifndef DAEMONIZE_H
#define DAEMONIZE_H

// #ifdef __linux__
// #include <syslog.h>
// #include <sys/socket.h>
// #include <netinet/in.h>
// #elif WINDOWS
// #include <winsock.h>
// #endif

#define BUFFER_SIZE 100

struct ClientConnection
{
    int sockfd;
    struct sockaddr_in addr;
    socklen_t addr_len;
};

struct RequestHandler
{
    struct ClientConnection *client;
    void (*handle_request)(struct ClientConnection *client);
};

struct Connection
{
    struct RequestHandler *request_handler;
    struct sockaddr_in addr;
    socklen_t addr_len;
    struct Connection *next;
    struct Connection *prev;
};
extern struct Connection *connection_head;
extern struct Connection *connection_current;

int daemonize(const char *dir,const char *pidfile, int logfd);

#endif