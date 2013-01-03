#ifndef DSO_H
#define DSO_H

#include <vector>
using namespace std;

#define MODULE_OK 0
#define MODULE_ERR 1

#define MAGIC_MAJOR_NUMBER 20130101
#define MAGIC_MINOR_NUMBER 0


#define STANDARD_MODULE_STUFF MAGIC_MAJOR_NUMBER, \
			       MAGIC_MINOR_NUMBER, \
			       __FILE__

typedef struct Module{
	int version;
	int minor_version;
	const char *name;
	void (*init)(Module *);
	int (*handle)(void *);
} Module;

extern vector<Module *> modules_pre_surl;

#define SPIDER_ADD_MODULE_PRE_SURL(module) do {\
    modules_pre_surl.push_back(module); \
} while(0)

extern Module * dso_load(const char *path, const char *name);

#endif
