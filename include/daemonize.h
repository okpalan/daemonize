#ifndef DAEMONIZE_H
#define DAEMONIZE_H

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

int daemonize(char *dir, char *pidfile, int logfd);

#endif