#include "url.h"

/* store uncrawled urls here */
static queue <char *> surl_queue;

/* store normalized Url objects here */
static queue<Url *> ourl_queue;

/* ? */
static map<string, string> host_ip_map;

static void dns_callback(int result, char type, int count, int ttl, void *addresses, void *arg);
static Url * spliturl(char *url);
static char * url_normalized(char *url);
static int iscrawled(const char * url);
static char * nake_path = "/";

void push_surlqueue(char * url)
{
    if (url != NULL) {
        char * nurl = strdup(url);
        surl_queue.push(nurl);
    }
}

Url * pop_ourlqueue()
{
    if (!ourl_queue.empty()) {
        Url * url = ourl_queue.front();
        ourl_queue.pop();
        return url;
    } else {
        return NULL;
    }
}

void * urlparser(void *arg)
{
    char *url = NULL;
    Url  *ourl = NULL;
    map<string, string>::const_iterator itr;

    while(1) {
        while (surl_queue.empty()) {
            SPIDER_LOG(SPIDER_LEVEL_DEBUG, "surl_queue is empty, sleep 0.5s");
            usleep(500000); /* sleep 0.5s */
        }
        url = surl_queue.front();
        surl_queue.pop();

        /* normalize url */
        if ((url = url_normalized(url)) == NULL) {
            SPIDER_LOG(SPIDER_LEVEL_WARN, "Normalize url fail");
            continue;
        }

        if (iscrawled(url)) { /* if is crawled */
            SPIDER_LOG(SPIDER_LEVEL_DEBUG, "Url is crawled: %s", url);
            free(url);
            url = NULL;
            continue;
        } else {
            /* spilt url into Url object */
            ourl = spliturl(url);
   
            itr = host_ip_map.find(ourl->domain);
            if (itr == host_ip_map.end()) { // not found
                /* dns resolve */
                event_base * base = event_init();
                evdns_init();
                evdns_resolve_ipv4(ourl->domain, 0, dns_callback, ourl);
                event_dispatch();
                event_base_free(base);

            } else {
                ourl->ip = strdup(itr->second.c_str());
                ourl_queue.push(ourl);
            }
        }

    }
    return NULL;
}

static int iscrawled(const char * url) {
    return search(url); /* use bloom filter algorithm */
}

static void dns_callback(int result, char type, int count, int ttl, void *addresses, void *arg) 
{
    Url * ourl = (Url *)arg;
    struct in_addr *addrs = (in_addr *)addresses;

    if (result != DNS_ERR_NONE || count == 0) {
        SPIDER_LOG(SPIDER_LEVEL_WARN, "Dns resolve fail: %s", ourl->domain);
    } else {
        char * ip = inet_ntoa(addrs[0]);
        SPIDER_LOG(SPIDER_LEVEL_DEBUG, "Dns resolve OK: %s -> %s", ourl->domain, ip);
        host_ip_map[ourl->domain] = strdup(ip);
        ourl->ip = ip;
        ourl_queue.push(ourl);
    }
    event_loopexit(NULL); // not safe for multithreads 
}
    
static Url * spliturl(char *url)
{
    Url *ourl = (Url *)calloc(1, sizeof(Url));
    char *p = index(url, '/');
    if (p == NULL) {
        ourl->domain = url;
        ourl->path = strdup(nake_path);
    } else {
        *p = '\0';
        ourl->domain = url;
        ourl->path = p+1;
    }
    return ourl;
}

static char * url_normalized(char *url) 
{
    if (url == NULL) return NULL;

    int len = strlen(url);
    if (len == 0) {
        free(url);
        return NULL;
    }

    /* remove http(s):// */
    if (len > 7 && strncmp(url, "http", 4) == 0) {
        int vlen = 7;
        if (url[4] == 's') /* https */
            vlen++;

        len -= vlen;
        char *tmp = (char *)malloc(len+1);
        strncpy(tmp, url+vlen, len);
        tmp[len] = '\0';
        free(url);
        url = tmp;
    }

    /* remove '/' at end of url if have */
    if (url[len] == '/') {
        url[len--] = '\0';
    }

    if (len > MAX_LINK_LEN) {
        free(url);
        return NULL;
    }

    return url;
}


void free_url(Url * ourl)
{
    free(ourl->domain);
    free(ourl->path);
    free(ourl->ip);
    free(ourl);
}
