#include "dso.h"

static int handler(void * data) {
	return MODULE_OK;
}

static void init(Module *mod)
{
	SPIDER_ADD_MODULE_PRE_SURL(mod);
}

Module maxdepth = {
	STANDARD_MODULE_STUFF,
	init,
	handler
};
