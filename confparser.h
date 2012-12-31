#ifndef CONFPARSER_H
#define CONFPARSER_H

#define MAX_CONF_LEN 1024

#define CONF_FILE "spiderq.conf"

typedef struct Config {
	int max_job_num;
};

extern Config * initconfig();
extern void loadconfig(Config *conf);


#endif
