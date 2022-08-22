#ifndef _DAEMONIZE_H
#define _DAEMONIZE_H

int daemonize(const char *dir, const char *pidfile, int logfd);
void sig_handler(int signo);

#endif /* _DAEMONIZE_H */