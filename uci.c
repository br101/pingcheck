#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <uci.h>
#include "main.h"

#define UCI_BUF_LEN 255

static struct uci_context* uci;

int uci_init(void)
{
	uci = uci_alloc_context();
	return uci != NULL;
}

void uci_get_pingcheck_interfaces(struct ping_intf* intf, int len)
{
	struct uci_package* p = uci_lookup_package(uci, "pingcheck");
	struct uci_element* e;
	struct uci_option* opt;
	int idx = 0;

	if (uci_load(uci, "pingcheck", &p))
		return;

	uci_foreach_element(&p->sections, e) {
		struct uci_section *s = uci_to_section(e);
		if (strcmp(s->type, "interface") == 0) {
			opt = uci_lookup_option(uci, s, "name");
			if (opt == NULL)
				continue;
			if (strlen(opt->v.string) >= MAX_IFNAME_LEN) {
				printlog(LOG_ERR, "UCI: Interface name too long");
				continue;
			}
			strcpy(intf[idx].name, opt->v.string);
			if (++idx > len) {
				printlog(LOG_ERR, "UCI: Can not handle more than %d interfaces", len);
				return;
			}
		}
	}

	uci_unload(uci, p);
}

int uci_get_option(const char* nameIn, char* value, size_t valLen)
{
	int ret = 0;
	struct uci_ptr p;
	char* name = strdup(nameIn);

	if (uci_lookup_ptr(uci, &p, name, true) != UCI_OK) {
		printlog(LOG_ERR, "UCI: Error in lookup of '%s'", nameIn);
	}
	else if (p.flags & UCI_LOOKUP_COMPLETE) {
		if (strlen(p.o->v.string) >= valLen) {
			printlog(LOG_ERR, "UCI: Value too short for option");
		} else {
			strcpy(value, p.o->v.string);
			ret = 1;
		}
	}

	free(name);
	if (p.p)
		uci_unload(uci, p.p);
	return ret;
}

int uci_get_option_int(const char* nameIn)
{
	int val = -1;
	struct uci_ptr p;
	char* name = strdup(nameIn);

	if (uci_lookup_ptr(uci, &p, name, true) != UCI_OK) {
		printlog(LOG_ERR, "UCI: Error in lookup of '%s'", nameIn);
	}
	else if (p.flags & UCI_LOOKUP_COMPLETE) {
		val = atoi(p.o->v.string);
	}

	free(name);
	if (p.p)
		uci_unload(uci, p.p);
	return val;
}

void uci_finish(void)
{
	if (uci != NULL)
		uci_free_context(uci);
}
