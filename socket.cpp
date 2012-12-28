#include <sys/socket.h>
#include <pthread.h>
#include <errno.h>
#include <regex.h>
#include <fcntl.h>
#include "url.h"
#include "socket.h"
#include "spider.h"


static int extract_url(char *str, char *domain);
static char * attach_domain(char *url, const char *domain);
static char * url2fn(const Url * url);

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

    sprintf(request, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: keep-alive\r\nReferer: %s\r\n\r\n", url->path, url->domain, url->domain);

    need = strlen(request);
    begin = 0;
    while(need) {
        n = write(fd, request+begin, need);
        if (n <= 0) {
            if (errno == EAGAIN) { //write buffer full, delay retry
                SPIDER_LOG(SPIDER_LEVEL_DEBUG, "thread %d recv EAGAIN", pthread_self());
                usleep(1000);
                continue;
            }
            SPIDER_LOG(SPIDER_LEVEL_DEBUG, "thread %d recv ERROR: %d", pthread_self(), n);
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
    
    if ((fd = open(fn, O_WRONLY | O_CREAT | O_APPEND, 0644)) < 0) {
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

    while(1) {
        n = read(narg->fd, buffer+len, 1023-len);
        if (n < 0) {
            if (errno == EAGAIN) { 
                /**
                 * TODO: Why always recv EAGAIN?
                 */
                SPIDER_LOG(SPIDER_LEVEL_DEBUG, "thread %d meet EAGAIN, sleep", pthread_self());
                usleep(1000);
                continue;
            } 
            SPIDER_LOG(SPIDER_LEVEL_WARN, "read socket to %s fail: %s", fn, strerror(errno));
            break;

        } else if (n == 0) {
            if (len > 0)
                write(fd, buffer, len);

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

            str_pos = extract_url(buffer, narg->url->domain);
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

    SPIDER_LOG(SPIDER_LEVEL_DEBUG, "thread %s end", pthread_self());
    free(fn);
    close(narg->fd);
    free_url(narg->url);
    close(fd);
    return NULL;
}

/*
 * 返回最后找到的链接的下一个下标
 */
static int extract_url(char *str, char *domain)
{
    const char * pattern = "href=\"([^ >\"]+)\"";
    regex_t re;
    const size_t nmatch = 2;
    regmatch_t matchptr[nmatch];
    int len;

    if (regcomp(&re, pattern, 0) != 0) {// 编译失败
        perror("compile regex error");
        exit(1);
    }

    char *p = str;
    while (regexec(&re, p, nmatch, matchptr, 0) != REG_NOMATCH) {
        len = (matchptr[1].rm_eo - matchptr[1].rm_so) + 1;
        p = p + matchptr[1].rm_so;
        char *tmp = (char *)calloc(len+1, 1);
        strncpy(tmp, p, len);
        tmp[len] = '\0';
        char *url = attach_domain(tmp, domain);
        if (url != NULL) {
            /* TODO: Why not url ? */
            SPIDER_LOG(SPIDER_LEVEL_DEBUG, "extract url:%s\n", url);
            push_surlqueue(url);
        }
        p = p + len;
    }

    return (p-str);
}

static char * attach_domain(char *url, const char *domain)
{
    if (url == NULL)
        return NULL;


    if (strncmp(url, "http", 4) == 0) {
        return url;

    } else if (*url == '/') {
        int i;
        int ulen = strlen(url);
        int dlen = strlen(domain);
        char *tmp = (char *)malloc(ulen+dlen+1);
        for (i = 0; i < dlen; i++)
            tmp[i] = domain[i];
        for (i = 0; i < ulen; i++)
            tmp[i+dlen] = url[i];
        tmp[ulen+dlen] = '\0';
        free(url);
        return tmp;
         
    } else {
        //do nothing
        free(url);
        return NULL;
    }
}

static char * url2fn(const Url * url)
{
    int i = 0;
    int l1 = strlen(url->domain);
    int l2 = strlen(url->path);
    char *fn = (char *)malloc(l1+l2+1);
    
    for (i = 0; i < l1; i++)
        fn[i] = url->domain[i];
    
    for (i = 0; i < l2; i++)
        fn[l1+i] = (url->path[i] == '/' ? '_' : url->path[i]);

    fn[l1+l2] = '\0';

    return fn;
}
