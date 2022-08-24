
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <syslog.h>
#include <signal.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ctype.h>
#include "daemonize.h"

#define BUFFERSIZE 201
#define NUMCONN 10
#define PORT 12345

#define DEFAULT_PORT "8080"
#define DEFAULT_MAX_CLIENTS 10
#define PIDFILE "/var/etc/casper.pid"

struct RequestHandler *request_handler_head = NULL;
struct RequestHandler *request_handler_current = NULL;

struct Connection *connection_head = NULL;
struct Connection *connection_current = NULL;

struct ClientConnection *head = NULL;
int listenfd;

int daemonize(const char *dir, const char *pidfile, int logfd){
    pid_t pid;
    int fd;

    /* Fork parent process */
    pid = fork();
    if (pid < 0)
        return -1;
    if (pid > 0)
        exit(0);

    /* Create new session */
    setsid();

    /* Ignore signals */
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);

    /* Fork off again to get daemon */
    pid = fork();
    if (pid < 0)
        return -1;
    if (pid > 0)
        exit(0);

    /* Change working directory */
    if (dir != NULL)
    {
        if (chdir(dir) != 0)
            return -1;
    }

    /* Redirect standard file descriptors */
    if (logfd == -1)
    {
        fd = open("/dev/null", O_RDWR);
    }
    else
    {
        fd = dup2(logfd, STDOUT_FILENO);
        if (fd == -1)
            return -1;
        fd = dup2(logfd, STDERR_FILENO);
        if (fd == -1)
            return -1;
        close(logfd);
    }

    if (fd == -1)
        return -1;
    fd = dup2(fd, STDIN_FILENO);
    if (fd == -1)
        return -1;

    /* Write process ID to file */
    if (pidfile != NULL)
    {
        fd = open(pidfile, O_WRONLY | O_CREAT, 0644);
        if (fd < 0)
            return -1;
        if (write(fd, &pid, sizeof(pid)) != sizeof(pid))
            return -1;
        close(fd);
    }

    return 0;
}

void sig_handler(int signo)
{
    struct Connection *c, *tmp;

    if (signo == SIGTERM)
    {
        syslog(LOG_INFO, " SIGNAL SIGTERM caught.\n");
        if (connection_head)
        {
            /* Close all client connections */
            c = connection_head;
            while (c)
            {
                if (close(c->request_handler->client->sockfd) < 0)
                {
                    syslog(LOG_ERR, "close failed: %s (%d)\n",
                           strerror(errno), errno);
                }
                c = c->next;
            }

            /* Remove all client connections from the list */
            c = connection_head;
            while (c)
            {
                tmp = c;
                c = c->next;
                free(tmp->request_handler->client);
                free(tmp->request_handler);
                free(tmp);
            }
            connection_head = NULL;
        }
        exit(0);
    }
}

struct Connection *add_client(struct sockaddr_in addr, socklen_t addr_len)
{
    struct Connection *client = malloc(sizeof(struct Connection));
    struct RequestHandler *request_handler = malloc(sizeof(struct RequestHandler));
    if (!request_handler)
    {
        perror("Failed to allocate memory");
        return NULL;
    }

    if (!client)
    {
        perror("Failed to allocate memory");
        return NULL;
    }
    memset(request_handler, 0, sizeof(struct RequestHandler));
    memset(client, 0, sizeof(struct Connection));
    client->request_handler = request_handler;
    client->addr = addr;
    client->next = connection_head;
    connection_head = client;
    return client;
}

int do_accept(int listen_sock)
{
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    int sockfd;

    sockfd = accept(listen_sock, (struct sockaddr *)&addr, &addr_len);
    if (sockfd < 0)
    {
        syslog(LOG_ERR, "accept failed: %s (%d)\n", strerror(errno), errno);
        return -1;
    }

    /* Add client connection to the list */
    if (!add_client(addr, addr_len))
    {
        syslog(LOG_ERR, "add_client failed\n");
        return -1;
    }

    return 0;
}

int handle_request(struct ClientConnection *client)
{
    char buffer[BUFFER_SIZE];
    int n;
    n = recv(client->sockfd, buffer, sizeof(buffer), 0);
    if (n <= 0)
    {
        if (n < 0)
        {
            perror("Error receiving data");
        }
        else
        {
            fprintf(stderr, "Client disconnected unexpectedly\n");
        }
        close(client->sockfd);
        free(client);
        return -1;
    }
    printf("%.*s", n, buffer);
    if (send(client->sockfd, buffer, n, 0) != n)
    {
        perror("Error sending response");
        close(client->sockfd);
        free(client);
        return -1;
    }
    return 0;
}

static void remove_client(struct Connection *connection)
{
    /* Remove client connection from the list */
    if (connection == connection_head)
    {
        connection_head = connection_head->next;
        if (connection_head)
            connection_head->prev = NULL;
    }
    else
    {
        if (connection->next)
            connection->next->prev = connection->prev;
        if (connection->prev)
            connection->prev->next = connection->next;
    }

    free(connection->request_handler->client);
    free(connection->request_handler);
    free(connection);
}
static void showHelp()
{
    printf("Usage: daemonize [options]\n");
    printf("Options:\n");
    printf("\t-c <file>\tConfiguration file\n");
    printf("\t-n <name>\tConfiguration name\n");
    printf("\t-h\t\tShow this help\n");
    printf("\t-V\t\tShow version\n");
}
int getVersion()
{
    printf("Version 0.1.0");
    return 0;
}

void logError(notice)
{
    printf("%s", notice);
}
int main(int argc, char *argv[])
{
    char *configFile = NULL;
    char *configName = NULL;
    int server_socket;
    fd_set clients;
    struct timeval timeout;
    struct ClientConnection *client;
    struct ClientConnection *client_last = NULL;
    int client_count = 0;
    int port = 0;
    int maxClients = 0;
    char *logFile = NULL;
    char *logFileBuffer = NULL;
    int logFileBufferSize = 0;
    int logFileBufferPos = 0;
    int logFd = -1;
    int c;
    const char *msg;
    int sent;
    int result;
    int error;

    /* Read options */
    opterr = 0;
    while ((c = getopt(argc, argv, "c:n:Vh")) != -1)
    {
        switch (c)
        {
        case 'c':
            configFile = optarg;
            break;
        case 'n':
            configName = optarg;
            break;
        case 'V':
            printf("casper %s\n", getVersion());
            exit(0);
        case 'h':
            showHelp();
            exit(0);
        case '?':
            if (optopt == 'c' || optopt == 'n')
                fprintf(stderr, "Option -%c requires an argument.\n", optopt);
            else if (isprint(optopt))
                fprintf(stderr, "Unknown option `-%c'.\n", optopt);
            else
                fprintf(stderr, "Unknown option character `\\x%x'.\n", optopt);
            exit(1);
        default:
            abort();
        }
    }

    if (configFile == NULL)
    {
        logError("Configuration file not specified");
        exit(1);
    }
    if (configName == NULL)
    {
        logError("Configuration name not specified");
        exit(1);
    }

    /* Read configuration */
    if (readConfig(configFile, configName, &port, &maxClients, &logLevel, &logFile) != 0)
        exit(1);
    if (logFile != NULL)
    {
        logFd = open(logFile, O_WRONLY | O_APPEND | O_CREAT, 0644);
        if (logFd == -1)
            exit(1);
    }

    /* Daemonize */
    if (daemonize("/var/run/casper", PIDFILE, logFd) != 0)
        exit(1);

    logNotice("Initializing...");

    /* Create server socket */
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket == -1)
    {
        logError("Failed to create server socket");
        exit(1);
    }

    /* Bind to port */
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind(server_socket, (struct sockaddr *)&addr, sizeof(addr)) == -1)
    {
        logError("Failed to bind to port");
        exit(1);
    }

    /* Listen */
    if (listen(server_socket, SOMAXCONN) == -1)
    {
        logError("Failed to listen");
        exit(1);
    }

    /* Main loop */
    logNotice("Listening for clients...");
    for (;;)
    {
        /* Set timeout */
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        /* Reset file descriptor set */
        FD_ZERO(&clients);
        FD_SET(server_socket, &clients);

        /* Add clients to file descriptor set */
        client = client_last;
        while (client != NULL)
        {
            FD_SET(client->sockfd, &clients);
            client = client->prev;
        }

        /* Wait for activity */
        result = select(FD_SETSIZE, &clients, NULL, NULL, &timeout);
        if (result == -1)
        {
            if (errno == EINTR)
                continue;
            logError("Failed to wait for activity");
            exit(1);
        }
        if (result == 0)
            continue;

        /* Add new clients */
        if (FD_ISSET(server_socket, &clients))
        {
            /* Check maximum clients */
            if (client_count >= maxClients)
            {
                logError("Maximum clients reached");
                continue;
            }

            error = sockaddr_len;
            client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &sockaddr_len);
            if (client_socket == -1)
            {
                logError("Failed to accept client connection");
                exit(1);
            }

            /* Create client connection */
            client = (struct ClientConnection *)malloc(sizeof(struct ClientConnection));
            if (client == NULL)
            {
                logError("Failed to allocate client connection");
                exit(1);
            }
            client->sockfd = client_socket;
            client->addr = client_addr;
            client->pos = 0;

            /* Add client connection to list */
            client->next = client_last;
            if (client_last != NULL)
                client_last->prev = client;
            client_last = client;
            client_count++;
        }

        /* Handle clients */
        client = client_last;
        while (client != NULL)
        {
            /* Check for client disconnect */
            if (!FD_ISSET(client->sockfd, &clients))
            {
                close(client->sockfd);
                logNotice("Client disconnected");
                if (client->prev != NULL)
                    client->prev->next = client->next;
                if (client->next != NULL)
                    client->next->prev = client->prev;
                if (client == client_last)
                    client_last = client->prev;
                client_count--;
                free(client);
                client = client->prev;
                continue;
            }

            /* Copy log file to client */
            if (logFd != -1)
            {
                /* Check for log file change */
                off_t fileSize = lseek(logFd, 0, SEEK_END);
                if (fileSize == -1)
                {
                    close(logFd);
                    logFd = -1;
                }
                else if (logFileBufferSize < fileSize)
                {
                    logFileBuffer = (char *)realloc(logFileBuffer, fileSize);
                    if (logFileBuffer == NULL)
                    {
                        close(logFd);
                        logFd = -1;
                    }
                    else
                    {
                        logFileBufferSize = fileSize;
                        logFileBufferPos = 0;
                    }
                }

                /* Read log file */
                if (logFd != -1)
                {
                    result = read(logFd, logFileBuffer + logFileBufferPos, logFileBufferSize - logFileBufferPos);
                    if (result > 0)
                    {
                        logFileBufferPos += result;
                        if (logFileBufferPos >= logFileBufferSize)
                            logFileBufferPos = 0;
                    }
                    else if (result < 0 && errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK)
                    {
                        close(logFd);
                        logFd = -1;
                    }
                }
            }

            /* Send log file to client */
            if (logFd != -1)
            {
                sent = 0;
                while (sent < logFileBufferPos)
                {
                    result = send(client->sockfd, logFileBuffer + sent, logFileBufferPos - sent, MSG_NOSIGNAL);
                    if (result < 0)
                    {
                        /* Ignore errors */
                        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                            continue;
                        goto client_error;
                    }
                }
                sent += result;
            }
            sent = 0;
            while (sent < logFileBufferSize - logFileBufferPos)
            {
                result = send(client->sockfd, logFileBuffer + logFileBufferSize - sent, logFileBufferSize - logFileBufferPos - sent, MSG_NOSIGNAL);
                if (result < 0)
                {
                    /* Ignore errors */
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                        continue;
                    goto client_error;
                }
                sent += result;
            }
        }

        /* Send log file to client */
        if (logFd != -1)
        {
            sent = 0;
            while (sent < logFileBufferPos)
            {
                result = send(client->sockfd, logFileBuffer + sent, logFileBufferPos - sent, MSG_NOSIGNAL);
                if (result < 0)
                {
                    /* Ignore errors */
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                        continue;
                    goto client_error;
                }
                sent += result;
            }
            sent = 0;
            while (sent < logFileBufferSize - logFileBufferPos)
            {
                result = send(client->sockfd, logFileBuffer + logFileBufferSize - sent, logFileBufferSize - logFileBufferPos - sent, MSG_NOSIGNAL);
                if (result < 0)
                {
                    /* Ignore errors */
                    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                        continue;
                    goto client_error;
                }
                sent += result;
            }
        }

        /* Check for client disconnect */
        error = 0;
        result = sendto(client_socket, NULL, 0, MSG_NOSIGNAL, (struct sockaddr *)&client_addr, sizeof(client_addr));
        if (result < 0)
        {
            if (errno != EPIPE && errno != ECONNRESET)
                logError("Failed to send to client");
            error = 1;
        }

        /* Remove client connection from list */
        if (error)
        {
        client_error:
            close(client->sockfd);
            logNotice("Client disconnected");
            if (client->prev != NULL)
                client->prev->next = client->next;
            if (client->next != NULL)
                client->next->prev = client->prev;
            if (client == client_last)
                client_last = client->prev;
            client_count--;
            free(client);
            client = client->prev;
            continue;
        }

        client = client->prev;
    }
}