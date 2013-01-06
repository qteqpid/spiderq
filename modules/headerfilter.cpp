#include "dso.h"
#include "socket.h"

static int handler(void * data) {
    Header *h = (Header *)data;
    char *p = NULL;

    /* skip if not 2xx */
    if (h->status_code < 200 || h->status_code >= 300)
        return MODULE_ERR;

    /* filter out !(Content-Type: text/html)  */
    if (h->content_type != NULL && (p = strstr(h->content_type, "text/html")) == NULL) 
        return MODULE_ERR;

    return MODULE_OK;
}

static void init(Module *mod)
{
    SPIDER_ADD_MODULE_POST_HEADER(mod);
}

Module headerfilter = {
    STANDARD_MODULE_STUFF,
    init,
    handler
};
