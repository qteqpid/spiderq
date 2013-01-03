#ifndef CONFPARSER_H
#define CONFPARSER_H

#include <vector>

using namespace std;

#define MAX_CONF_LEN 1024

#define CONF_FILE "spiderq.conf"

typedef struct Config {
	int             max_job_num;
	char           *seeds;
	char            *include_prefixes; 
	char            *exclude_prefixes; 
	char            *logfile; 
 	int              log_level;
	int              max_depth;
	int              make_hostdir;

	char *           module_path;
	vector<char *>   modules;

};

extern Config * initconfig();
extern void loadconfig(Config *conf);


#endif
