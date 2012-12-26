#ifndef SPIDER_H
#define SPIDER_H

#include <pthread.h>
#include <stdarg.h>
#include "url.h"
#include "socket.h"

/* macros */
#define MAX_MESG_LEN   1024

#define SPIDER_LOG_LEVEL   0
#define SPIDER_LEVEL_DEBUG 0
#define SPIDER_LEVEL_INFO  1
#define SPIDER_LEVEL_WARN  2
#define SPIDER_LEVEL_ERROR 3

static const char * LOG_STR[] = { 
    "DEBUG",
    "INFO",
    "WARN",
    "ERROR"
};

#define SPIDER_LOG(level, format, ...) do{ \
    if (level >= SPIDER_LOG_LEVEL) {\
        char msg[MAX_MESG_LEN]; \
        sprintf(msg, format, ##__VA_ARGS__); \
        fprintf(stdout, "[%s] %s\n", LOG_STR[level], msg); \
        fflush(stdout); \
    } \
} while(0)



int CreateThread(void *(*start_routine) (void *), void *arg, pthread_t * thread, pthread_attr_t * pAttr);

#endif
