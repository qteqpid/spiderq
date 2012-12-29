#include <sys/socket.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include "url.h"
#include "socket.h"
#include "spider.h"

static const char * HREF_PATTERN = "href=\"\\s*\\([^>\"]*\\)\"";

int buildConnect(int *fd, char *ip, int port)
{
    struct sockaddr_in server_addr;
    bzero(&server_addr, sizeof(struct sockaddr_in));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (!inet_aton(ip, &(server_addr.sin_addr))) {
        return -1;
    }

    if ((*fd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        return -1;
    }

    if (connect(*fd, (struct sockaddr *)&server_addr, sizeof(struct sockaddr_in)) < 0) {
        close(*fd);
        return -1;
    }

    return 0;
}

int sendRequest(int fd, void *arg)
{
    int need, begin, n;
    char request[1024] = {0};
    Url *url = (Url *)arg;

    sprintf(request, "GET /%s HTTP/1.0\r\nHost: %s\r\nConnection: keep-alive\r\nReferer: %s\r\n\r\n", url->path, url->domain, url->domain);

    need = strlen(request);
    begin = 0;
    while(need) {
        n = write(fd, request+begin, need);
        if (n <= 0) {
            if (errno == EAGAIN) { //write buffer full, delay retry
                usleep(1000);
                continue;
            }
            SPIDER_LOG(SPIDER_LEVEL_DEBUG, "thread %lu recv ERROR: %d", pthread_self(), n);
            free_url(url);
            close(fd);
            return -1;
        }
        begin += n;
        need -= n;
    }
    return 0;
}

void set_nonblocking(int fd)
{
    int flag;
    if ((flag = fcntl(fd, F_GETFL)) < 0) {
        perror("fcntl getfl fail");
        exit(1);
    }
    flag |= O_NONBLOCK;
    if ((flag = fcntl(fd, F_SETFL, flag)) < 0) {
        perror("fcntl setfl fail");
        exit(1);
    }
}

void * recvResponse(void * arg)
{
    evso_arg * narg = (evso_arg *)arg;
    char * fn = url2fn(narg->url);
    int fd;
    regex_t re;
    
    if ((fd = open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
        SPIDER_LOG(SPIDER_LEVEL_WARN, "open file for writing fail: %s", fn);
        free(fn);
        free_url(narg->url);
        close(fd);
        return NULL;
    }

    char buffer[1024];
    int i, len = 0, n, trunc_head = 0;
    int str_pos = 0;
    char * body_ptr = NULL;
    if (regcomp(&re, HREF_PATTERN, 0) != 0) {// 编译失败
        SPIDER_LOG(SPIDER_LEVEL_WARN, "compile regex error");
	exit(1);
    }

    while(1) {
        n = read(narg->fd, buffer+len, 1023-len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) { 
                /**
                 * TODO: Why always recv EAGAIN?
                 * should we deal EINTR
                 */
                SPIDER_LOG(SPIDER_LEVEL_DEBUG, "thread %lu meet EAGAIN or EWOULDBLOCK, sleep", pthread_self());
                usleep(1000);
                continue;
            } 
            SPIDER_LOG(SPIDER_LEVEL_WARN, "read socket to %s fail: %s", fn, strerror(errno));
            break;

        } else if (n == 0) {
            if (len > 0) {
            	extract_url(&re, buffer, narg->url->domain);
                write(fd, buffer, len);
	    }
            break;

        } else {
            len += n;
            buffer[len] = '\0';

            if (!trunc_head) {
                if ((body_ptr = strstr(buffer, "\r\n\r\n")) != NULL) {
                    body_ptr += 4;
                    for (i = 0; *body_ptr; i++) {
                        buffer[i] = *body_ptr;
                        body_ptr++;
                    }
                    buffer[i] = '\0';
                    len = i;
                    trunc_head = 1;
                } else {
                    len = 0;
                }
                continue;
            }

            str_pos = extract_url(&re, buffer, narg->url->domain);
            char *p = rindex(buffer, ' ');
            if (p == NULL) {
                // 没有空格，应该也不会有href被截断
                write(fd, buffer, len);
                len = 0;

            } else if (p-buffer >= str_pos) {
                /* 空格在找到的最后一个链接后，则空格前
                (包括空格)的内容都写入，空格后的内容保留 */
                write(fd, buffer, ((p-buffer)+1));
                len -= ((p-buffer)+1);
                /* 空格后的内容左移 */
                for (i = 0; i < len; i++) {
                    buffer[i] = *(++p);
                }

            } else {
                /* 空格在找到的最后一个链接前，几乎不可能吧 */
                write(fd, buffer, len);
                len = 0;
            }
        }
    }

    SPIDER_LOG(SPIDER_LEVEL_DEBUG, "thread %lu end", pthread_self());
    free(fn);
    close(narg->fd);
    free_url(narg->url);
    close(fd);
    regfree(&re);
    return NULL;
}

