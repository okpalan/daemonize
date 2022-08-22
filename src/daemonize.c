#include "daemonize.h"
#include "client.h"
#include "request_handler.h"
#include <pthread.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>

#define BUFFERSIZE 201
#define NUMCONN 10
#define PORT 12345

#define DEFAULT_PORT "8080"
#define DEFAULT_MAX_CLIENTS 10
#define PIDFILE "/var/etc/casper.pid"
 

struct ClientConnection *head = NULL;
int listenfd;

static void sigterm_handler(int signum)
{
    syslog(LOG_INFO, "SIGTERM received, exiting");
    exit(0);
}
int daemonize(const char *dir, const char *pidfile, int logfd)
{
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

static void showHelp()
{
    printf("Usage: daemonize [options]\n");
    printf("Options:\n");
    printf("\t-c <file>\tConfiguration file\n");
    printf("\t-n <name>\tConfiguration name\n");
    printf("\t-h\t\tShow this help\n");
    printf("\t-V\t\tShow version\n");
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
            FD_SET(client->socket, &clients);
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
            client->socket = client_socket;
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
            if (!FD_ISSET(client->socket, &clients))
            {
                close(client->socket);
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
                    result = send(client->socket, logFileBuffer + sent, logFileBufferPos - sent, MSG_NOSIGNAL);
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
                result = send(client->socket, logFileBuffer + logFileBufferSize - sent, logFileBufferSize - logFileBufferPos - sent, MSG_NOSIGNAL);
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
                result = send(client->socket, logFileBuffer + sent, logFileBufferPos - sent, MSG_NOSIGNAL);
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
                result = send(client->socket, logFileBuffer + logFileBufferSize - sent, logFileBufferSize - logFileBufferPos - sent, MSG_NOSIGNAL);
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
            close(client->socket);
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
