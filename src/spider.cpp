#include <stdio.h>
#include <sys/epoll.h>
#include <sys/resource.h>
#include <getopt.h>
#include <fcntl.h>
#include "spider.h"
#include "threads.h"
#include "qstring.h"

int g_epfd;
Config *g_conf;

static int set_nofile(rlim_t limit);
static void daemonize();

static void version()
{
    printf("Version: spiderq/1.0 by qteqpid\n");
    exit(1);
}

static void usage()
{
    printf("Usage: ./spider [Options]\n"
            "\nOptions:\n"
            "  -h\t: this help\n"
            "  -v\t: print spiderq's version\n"
            "  -d\t: run program as a daemon process\n\n");
    exit(1);
}

int main(int argc, void *argv[]) 
{
    struct epoll_event events[10];
    int daemonized = 0;
    char ch;

    /* parse opt */
    while ((ch = getopt(argc, (char* const*)argv, "vhd")) != -1) {
	switch(ch) {
		case 'v':
			version();
			break;
		case 'd':
			daemonized = 1;
			break;
		case 'h':
		case '?':
		default:
			usage();
        }
    }

    /* parse log */
    g_conf = initconfig();
    loadconfig(g_conf);

    /* change wd to download directory */
    chdir("download"); 
    
    /* set max value of fd num to 1024 */
    set_nofile(1024); 

    /* add seeds */
    if (g_conf->seeds == NULL) {
        SPIDER_LOG(SPIDER_LEVEL_INFO, "We have no seeds, Buddy!");
	exit(0);
    } else {
        int c = 0;
	char ** splits = strsplit(g_conf->seeds, ',', &c, 0);
        while (c--)
            push_surlqueue(splits[c]);
    }	

    /* daemonized if setted */
    if (daemonized)
	daemonize();

    /* create a thread for DNS parsing and parse seed surl to ourl */
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
    g_epfd = epoll_create(g_conf->max_job_num);

    while(ourl_num++ < g_conf->max_job_num) {
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

static void daemonize()
{
	int fd;
	SPIDER_LOG(SPIDER_LEVEL_INFO, "Daemonized...");	
	if (fork() != 0) exit(0);
	setsid();
	
	if ((fd = open("/dev/null", O_RDWR, 0)) != -1) {
		dup2(fd, STDIN_FILENO);
		dup2(fd, STDOUT_FILENO);
		dup2(fd, STDERR_FILENO);
		if (fd > STDERR_FILENO)
			close(fd);
	}

	if (g_conf->logfile != NULL && (fd = open(g_conf->logfile, O_RDWR | O_APPEND | O_CREAT, 0)) != -1) {
		dup2(fd, STDOUT_FILENO);
		if (fd > STDERR_FILENO)
			close(fd);
	}

}
