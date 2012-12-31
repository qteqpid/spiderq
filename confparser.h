#ifndef CONFPARSER_H
#define CONFPARSER_H

#define MAX_CONF_LEN 1024

#define CONF_FILE "spiderq.conf"

typedef struct Config {
	int       max_job_num;
	char     *seed_urls;
	char     *include_prefixes; 
	char     *exclude_prefixes; 
	char     *logfile; 
 	int       log_level;
};

extern Config * initconfig();
extern void loadconfig(Config *conf);


#endif
