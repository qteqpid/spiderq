#include <sys/socket.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include "url.h"
#include "socket.h"
#include "threads.h"
#include "qstring.h"
#include "dso.h"

/* regex pattern for parsing href */
static const char * HREF_PATTERN = "href=\"\\s*\\([^ >\"]*\\)\\s*\"";

/* convert header string to Header object */
static Header * parse_header(char *header);

/* call modules to check header */
static int header_postcheck(Header *header);

int build_connect(int *fd, char *ip, int port)
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

int send_request(int fd, void *arg)
{
    int need, begin, n;
    char request[1024] = {0};
    Url *url = (Url *)arg;

    sprintf(request, "GET /%s HTTP/1.0\r\n"
            "Host: %s\r\n"
            "Accept: text/html\r\n"
            "Connection: Keep-Alive\r\n"
            "User-Agent: Mozilla/5.0 (compatible; Qteqpidspider/1.0;)\r\n"
            "Referer: %s\r\n\r\n", url->path, url->domain, url->domain);

    need = strlen(request);
    begin = 0;
    while(need) {
        n = write(fd, request+begin, need);
        if (n <= 0) {
            if (errno == EAGAIN) { //write buffer full, delay retry
                usleep(1000);
                continue;
            }
            SPIDER_LOG(SPIDER_LEVEL_WARN, "Thread %lu send ERROR: %d", pthread_self(), n);
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
        SPIDER_LOG(SPIDER_LEVEL_ERROR, "fcntl getfl fail");
    }
    flag |= O_NONBLOCK;
    if ((flag = fcntl(fd, F_SETFL, flag)) < 0) {
        SPIDER_LOG(SPIDER_LEVEL_ERROR, "fcntl setfl fail");
    }
}

void * recv_response(void * arg)
{
    begin_thread();

    int fd = -1;
    int str_pos = 0;
    int i, len = 0, n, trunc_head = 0;
    char buffer[1024] = {0};
    char header[4096] = {0};
    Header *h = NULL;
    char * body_ptr = NULL;
    evso_arg * narg = (evso_arg *)arg;
    char * fn = url2fn(narg->url);
    regex_t re;

    if (regcomp(&re, HREF_PATTERN, 0) != 0) {/* compile error */
        SPIDER_LOG(SPIDER_LEVEL_ERROR, "compile regex error");
    }

    SPIDER_LOG(SPIDER_LEVEL_INFO, "Crawling url: %s/%s", narg->url->domain, narg->url->path);

    while(1) {
        n = read(narg->fd, buffer+len, 1023-len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) { 
                /**
                 * TODO: Why always recv EAGAIN?
                 * should we deal EINTR
                 */
                //SPIDER_LOG(SPIDER_LEVEL_WARN, "thread %lu meet EAGAIN or EWOULDBLOCK, sleep", pthread_self());
                usleep(100000);
                continue;
            } 
            SPIDER_LOG(SPIDER_LEVEL_WARN, "Read socket to %s fail: %s", fn, strerror(errno));
            break;

        } else if (n == 0) {
            /* finish reading */
            if (len > 0) {
                extract_url(&re, buffer, narg->url);
                write(fd, buffer, len);
            }
            break;

        } else {
            //SPIDER_LOG(SPIDER_LEVEL_WARN, "read socket ok! len=%d", n);
            len += n;
            buffer[len] = '\0';

            if (!trunc_head) {
                if ((body_ptr = strstr(buffer, "\r\n\r\n")) != NULL) {
                    *(body_ptr+2) = '\0';
                    strcat(header, buffer);
                    h = parse_header(header);
                    if (!header_postcheck(h)) {
                        goto leave; /* modulues filter fail */
                    }
                    trunc_head = 1;

                    /* cover header */
                    body_ptr += 4;
                    for (i = 0; *body_ptr; i++) {
                        buffer[i] = *body_ptr;
                        body_ptr++;
                    }
                    buffer[i] = '\0';
                    len = i;

                    /* open file to save */
                    if ((fd = open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
                        SPIDER_LOG(SPIDER_LEVEL_WARN, "Open file for writing fail: %s", fn);
                        goto leave;
                    }

                } else {
                    /* header is so long ... */
                    strcat(header, buffer);
                    len = 0;
                }
                continue;
            }

            str_pos = extract_url(&re, buffer, narg->url);
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

leave:
    close(fd); /* close file */
    free(fn);
    close(narg->fd); /* close socket */
    free_url(narg->url); /* free Url object */
    regfree(&re); /* free regex object */
    if (h != NULL) free(h);

    end_thread();
    return NULL;
}


static int header_postcheck(Header *header)
{
    unsigned int i;
    for (i = 0; i < modules_post_header.size(); i++) {
        if (modules_post_header[i]->handle(header) != MODULE_OK)
            return 0;
    }
    return 1;
}

static Header * parse_header(char *header)
{
    int c = 0;
    char *p = NULL;
    char **sps = NULL;
    char *start = header;
    Header *h = (Header *)calloc(1, sizeof(Header));

    if ((p = strstr(start, "\r\n")) != NULL) {
        *p = '\0';
        sps = strsplit(start, ' ', &c, 2);
        if (c == 3) {
            h->status_code = atoi(sps[1]);
        } else {
            h->status_code = 600; 
        }
        start = p + 2;
    }

    while ((p = strstr(start, "\r\n")) != NULL) {
        *p = '\0';
        sps = strsplit(start, ':', &c, 1);
        if (c == 2) {
            if (strcasecmp(sps[0], "content-type") == 0) {
                h->content_type = strdup(strim(sps[1]));
            }
        }
        start = p + 2;
    }
    return h;
}
