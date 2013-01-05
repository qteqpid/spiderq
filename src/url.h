#ifndef QURL_H
#define QURL_H

#include <event.h>
#include <evdns.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <regex.h>
#include <queue>
#include <map>
#include <string>
#include "spider.h"
#include "bloomfilter.h"

using namespace std;

#define MAX_LINK_LEN 128

typedef struct Surl {
    char  *url;
    int    level;
} Surl;

typedef struct Url {
    char *domain;
    char *path;
    int  port;
    char *ip;
    int  level;
} Url;

typedef struct evso_arg {
    int     fd;
    Url     *url;
} evso_arg;

extern void push_surlqueue(Surl * url);
extern Url * pop_ourlqueue();
extern void * urlparser(void * arg);
extern void free_url(Url * ourl);
extern int is_ourlqueue_empty();
extern int is_surlqueue_empty();
extern int extract_url(regex_t *re, char *str, Url *domain);
extern char * url2fn(const Url * url);
extern char * url_normalized(char *url);
extern int get_surl_queue_size();
extern int get_ourl_queue_size();

#endif
