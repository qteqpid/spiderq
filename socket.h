#ifndef QSOCKET_H
#define QSOCKET_H

extern int buildConnect(int *fd, char *ip);
extern int sendRequest(int fd, void *arg);
extern void set_noblock(int fd);
extern void * recvResponse(void *arg);

#endif
