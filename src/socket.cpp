#include <sys/socket.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include "url.h"
#include "socket.h"
#include "threads.h"

static const char * HREF_PATTERN = "href=\"\\s*\\([^ >\"]*\\)\\s*\"";

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
                     "Accept: */*\r\n"
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
            SPIDER_LOG(SPIDER_LEVEL_WARN, "Thread %lu recv ERROR: %d", pthread_self(), n);
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

    int fd;
    int str_pos = 0;
    int i, len = 0, n, trunc_head = 0;
    char buffer[1024];
    char * body_ptr = NULL;
    evso_arg * narg = (evso_arg *)arg;
    char * fn = url2fn(narg->url);
    regex_t re;
    
    if (regcomp(&re, HREF_PATTERN, 0) != 0) {// 编译失败
        SPIDER_LOG(SPIDER_LEVEL_ERROR, "compile regex error");
    }

    SPIDER_LOG(SPIDER_LEVEL_INFO, "Crawling url: %s/%s", narg->url->domain, narg->url->path);

    if ((fd = open(fn, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
        SPIDER_LOG(SPIDER_LEVEL_WARN, "open file for writing fail: %s", fn);
	goto leave;
    }

    while(1) {
        n = read(narg->fd, buffer+len, 1023-len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) { 
                /**
                 * TODO: Why always recv EAGAIN?
                 * should we deal EINTR
                 */
                SPIDER_LOG(SPIDER_LEVEL_WARN, "thread %lu meet EAGAIN or EWOULDBLOCK, sleep", pthread_self());
                usleep(10000);
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
		/* TODO: skip if status code is NOT 200 */
		
		/* filter out !(Content-Type: text/html)  */
                if ((body_ptr = strstr(buffer, "Content-Type: ")) != NULL) {
			if (strncmp(body_ptr+14, "text/html", 9) != 0) {
                		SPIDER_LOG(SPIDER_LEVEL_INFO, "Content-Type is not text/html");
				goto leave;
			}
		}

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

leave:
    free(fn);
    close(fd); /* close file */
    close(narg->fd); /* close socket */
    free_url(narg->url); /* free Url object */
    regfree(&re); /* free regex object */

    /* wait for dns to prepare new ourl */
    if (is_ourlqueue_empty())
    	usleep(1000000);
    end_thread();
    return NULL;
}

