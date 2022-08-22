#ifndef __CONNECTION_H__
#define __CONNECTION_H__

#include "request_handler.h"

struct Connection
{
    /* Connection information */
    struct sockaddr_in addr;
    socklen_t addr_len;
    struct Connection *next;
    struct Connection *prev;

    /* Request handler */
    struct RequestHandler *request_handler;
};

#endif