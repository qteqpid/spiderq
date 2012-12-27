#include <stdio.h>
#include <sys/epoll.h>
#include "spider.h"

static char *seed = NULL;
int g_epfd;
int g_max_thread_num = 2;

static int createThread(void *(*start_func)(void *), void * arg, pthread_t *pid, pthread_attr_t * pattr);

static void usage()
{
    printf("Usage: ./spider -u <seed_url> [-i <pattern>] [-e <pattern>] [-m <num>]\n"
            "\nOptions:\n"
            "  -h\t: this help\n"
            "  -u\t: set seed url to crawl from\n"
            "  -i\t: only crawl urls that satisfy. Comma seperated if you have more than one\n"
            "  -e\t: opposite to -i option, so should NOT work with -i(NOT supported yet)\n"
            "  -m\t: set max_num of crawling threads(NOT supported yet)\n"
            "Example: ./spider -u www.imeiding.com -i www.imeiding.com/question\n\n");
    exit(1);
}

int main(int argc, void *argv[]) 
{
    struct epoll_event ev, events[10];

    /* parse opt */

    chdir("download"); /* change wd to download directory */

    /* test */
    //seed = "http://www.blue.com";
    //seed = "http://www.imeiding.com";
    seed = "http://trac.instreet.cn:81";
    push_surlqueue(seed);

    /* create a thread for parse surl to ourl */
    int            err;
    if ((err = createThread(urlparser, NULL, NULL, NULL)) < 0) {
        SPIDER_LOG(SPIDER_LEVEL_ERROR, "Create urlparser thread fail: %s", strerror(err));
    }

    while(is_ourlqueue_empty())
        usleep(10000);

    /* begin */
    int sock_rv;
    int ourl_num = 0;
    g_epfd = epoll_create(g_max_thread_num);

    while(ourl_num++ < g_max_thread_num) {
        Url * ourl = pop_ourlqueue();
        if (ourl == NULL)
            break;
        
        /* connect socket and get sockfd */
        int sockfd;
        if ((sock_rv = buildConnect(&sockfd, ourl->ip, ourl->port)) < 0) {
            SPIDER_LOG(SPIDER_LEVEL_ERROR, "Build socket connect fail: %s", ourl->ip);
            exit(1);
        }
        
        set_noblock(sockfd);

        if ((sock_rv = sendRequest(sockfd, ourl)) < 0) {
            SPIDER_LOG(SPIDER_LEVEL_ERROR, "Send socket request fail: %s", ourl->ip);
            exit(1);
        }

        evso_arg * arg = (evso_arg *)calloc(1, sizeof(evso_arg));
        arg->fd = sockfd;
        arg->url = ourl;
        ev.data.ptr = arg;
        ev.events = EPOLLIN | EPOLLET;
        epoll_ctl(g_epfd, EPOLL_CTL_ADD, sockfd, &ev); /* add event */
    }

    int n, i;
    while(1) {
        n = epoll_wait(g_epfd, events, 10, 2000);
        for (i = 0; i < n; i++) {
            evso_arg * arg = (evso_arg *)(events[i].data.ptr);
            createThread(recvResponse, arg, NULL, NULL);
        }
    }

    return 0;
}


static int createThread(void *(*start_func)(void *), void * arg, pthread_t *pid, pthread_attr_t * pattr)
{
    pthread_attr_t attr;
    pthread_t pt;

    if (pattr == NULL) {
        pattr = &attr;
        pthread_attr_init(pattr);
        pthread_attr_setstacksize(pattr, 1024*1024);
        pthread_attr_setdetachstate(pattr, PTHREAD_CREATE_DETACHED);
    }

    if (pid == NULL)
        pid = &pt;

    int rv = pthread_create(pid, pattr, start_func, arg);
    pthread_attr_destroy(pattr);
    return rv;
}
