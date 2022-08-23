
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

#include "daemonize.h"

#define BUFFER_SIZE 100

struct RequestHandler *request_handler_head = NULL;
struct RequestHandler *request_handler_current = NULL;

struct Connection *connection_head = NULL;
struct Connection *connection_current = NULL;

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

static void showHelp()
{
    printf("Usage: daemonize [options]\n");
    printf("Options:\n");
    printf("\t-c <file>\tConfiguration file\n");
    printf("\t-n <name>\tConfiguration name\n");
    printf("\t-h\t\tShow this help\n");
    printf("\t-V\t\tShow version\n");
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

int main(int argc, char *argv[])
{
    char *dir, *pidfile;
    int logfd;

    if (argc != 4)
    {
        fprintf(stderr, "Usage: %s <directory> <pidfile> <logfile>\n",
                argv[0]);
        exit(1);
    }

    dir = argv[1];
    pidfile = argv[2];
    logfd = open(argv[3], O_RDWR | O_CREAT | O_APPEND, 0644);
    if (logfd < 0)
    {
        fprintf(stderr, "open failed: %s (%d)\n", strerror(errno), errno);
        exit(1);
    }

    /* Daemonize */
    if (daemonize(dir, pidfile, logfd) < 0)
    {
        fprintf(stderr, "daemonize failed\n");
        exit(1);
    }

    /* Handle signals */
    signal(SIGTERM, sig_handler);

    return 0;
}