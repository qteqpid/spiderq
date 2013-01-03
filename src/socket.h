#ifndef QSOCKET_H
#define QSOCKET_H

extern int build_connect(int *fd, char *ip, int port);
extern int send_request(int fd, void *arg);
extern void set_nonblocking(int fd);
extern void * recv_response(void *arg);

#endif
