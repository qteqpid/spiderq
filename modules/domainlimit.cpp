#include "dso.h"
#include "url.h"
#include "qstring.h"
#include <vector>
using namespace std;

typedef struct dlnode {
	char *prefix;
	int   len;
} dlnode;

static vector<dlnode> include_nodes;
static vector<dlnode> exclude_nodes;

static int handler(void * data) {
	unsigned int i;
	Surl *url = (Surl *)data;
	for (i = 0; i < include_nodes.size(); i++) {
		if (strncmp(url->url, include_nodes[i].prefix, include_nodes[i].len) == 0)
			return MODULE_OK;
	}
	if (include_nodes.size() > 0)
		return MODULE_ERR;

	for (i = 0; i < exclude_nodes.size(); i++) {
		if (strncmp(url->url, exclude_nodes[i].prefix, exclude_nodes[i].len) == 0)
			return MODULE_ERR;
	}
	return MODULE_OK;
}

static void init(Module *mod)
{
	SPIDER_ADD_MODULE_PRE_SURL(mod);

	if (g_conf->include_prefixes != NULL) {
		int c = 0;
		char ** ss = strsplit(g_conf->include_prefixes, ',', &c, 0);
		while (c--) {
			dlnode n;
			n.prefix = strim(ss[c]);
                        n.len = strlen(n.prefix);
			include_nodes.push_back(n);
		}
	} else if (g_conf->exclude_prefixes != NULL) {
		int c = 0;
		char ** ss = strsplit(g_conf->exclude_prefixes, ',', &c, 0);
		while (c--) {
			dlnode n;
			n.prefix = strim(ss[c]);
                        n.len = strlen(n.prefix);
			exclude_nodes.push_back(n);
		}
	}
}

Module domainlimit = {
	STANDARD_MODULE_STUFF,
	init,
	handler
};
