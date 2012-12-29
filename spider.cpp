#include <stdio.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include "spider.h"
#include "threads.h"

static char *seed = NULL;
int g_epfd;
extern int g_max_thread_num;

static int set_nofile(rlim_t limit);

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
    struct epoll_event events[10];

    /* parse opt */
    g_max_thread_num = 10;

    chdir("download"); /* change wd to download directory */

    set_nofile(1024); /* set max value of fd num to 1024 */

    /* test */
    //seed = "http://www.blue.com";
    //seed = "http://trac.instreet.cn:81";
    seed = "http://www.imeiding.com/";
    //seed = "http://www.imeiding.com/test.php";
    push_surlqueue(seed);
//    push_surlqueue(seed);

    /* create a thread for parse seed surl to ourl */
    int err;
    if ((err = create_thread(urlparser, NULL, NULL, NULL)) < 0) {
        SPIDER_LOG(SPIDER_LEVEL_ERROR, "Create urlparser thread fail: %s", strerror(err));
    }

    /* waiting seed ourl ready */
    int try_num = 1;
    while(try_num < 8 && is_ourlqueue_empty())
        usleep((10000 << try_num++));

    if (try_num >= 8) {
        SPIDER_LOG(SPIDER_LEVEL_ERROR, "NO ourl! DNS parse error?");
    }

    /* begin create epoll to run */
    int ourl_num = 0;
    g_epfd = epoll_create(g_max_thread_num);

    while(ourl_num++ < g_max_thread_num) {
	if (attach_epoll_task() < 0)
	    break;
    }

    /* epoll wait */
    int n, i;
    while(1) {
        n = epoll_wait(g_epfd, events, 10, 2000);
            printf("epoll:%d\n",n);
            fflush(stdout);
        for (i = 0; i < n; i++) {
            evso_arg * arg = (evso_arg *)(events[i].data.ptr);
            if ((events[i].events & EPOLLERR) ||
                (events[i].events & EPOLLHUP) ||
                (!(events[i].events & EPOLLIN))) {
                SPIDER_LOG(SPIDER_LEVEL_WARN, "epoll fail, close socket %d",arg->fd);
                close(arg->fd);
                continue;
            }
            /* I don't know why epoll_wait return the same socket event multi times
             * so I have to del event here
             */
            epoll_ctl(g_epfd, EPOLL_CTL_DEL, arg->fd, &events[i]); /* del event */

            printf("hello epoll:event=%d\n",events[i].events);
            fflush(stdout);
            create_thread(recv_response, arg, NULL, NULL);
        }
    }

    close(g_epfd);
    return 0;
}

int attach_epoll_task()
{
	struct epoll_event ev;
    	int sock_rv;
        SPIDER_LOG(SPIDER_LEVEL_DEBUG, "Ready to pop ourlqueue");
        Url * ourl = pop_ourlqueue();
        if (ourl == NULL) {
            SPIDER_LOG(SPIDER_LEVEL_WARN, "Pop ourlqueue fail!");
            return -1;
	}
        
        SPIDER_LOG(SPIDER_LEVEL_DEBUG, "Pop ourlqueue OK!");

        /* connect socket and get sockfd */
        int sockfd;
        if ((sock_rv = build_connect(&sockfd, ourl->ip, ourl->port)) < 0) {
            SPIDER_LOG(SPIDER_LEVEL_WARN, "Build socket connect fail: %s", ourl->ip);
	    return -1;
        }
        
        set_nonblocking(sockfd);

        if ((sock_rv = send_request(sockfd, ourl)) < 0) {
            SPIDER_LOG(SPIDER_LEVEL_WARN, "Send socket request fail: %s", ourl->ip);
	    return -1;
        } else {
            SPIDER_LOG(SPIDER_LEVEL_DEBUG, "Send socket request success: %s", ourl->ip);
        }

        evso_arg * arg = (evso_arg *)calloc(1, sizeof(evso_arg));
        arg->fd = sockfd;
        arg->url = ourl;
        ev.data.ptr = arg;
        ev.events = EPOLLIN | EPOLLET;
        if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, sockfd, &ev) == 0) {/* add event */
            SPIDER_LOG(SPIDER_LEVEL_DEBUG, "Attach an epoll event success!");
	} else {
            SPIDER_LOG(SPIDER_LEVEL_WARN, "Attach an epoll event fail!");
	    return -1;
	}
	return 0;
}

static int set_nofile(rlim_t limit)
{
	struct rlimit rl;
	if (getrlimit(RLIMIT_NOFILE, &rl) < 0) {
		SPIDER_LOG(SPIDER_LEVEL_WARN, "getrlimit fail");
		return -1;
	}
	if (limit > rl.rlim_max) {
		SPIDER_LOG(SPIDER_LEVEL_WARN, "limit should NOT be greater than %lu", rl.rlim_max);
		return -1;
	}
	rl.rlim_cur = limit;
	if (setrlimit(RLIMIT_NOFILE, &rl) < 0) {
		SPIDER_LOG(SPIDER_LEVEL_WARN, "setrlimit fail");
		return -1;
	}
	return 0;
}
