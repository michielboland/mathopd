#include "mathopd.h"

int process_redirect(struct request *r)
{
	char *location, *c;
	FILE *fp;

	location = r->path_args;
	if (r->method != M_GET && r->method != M_HEAD) {
		r->error = "invalid method for redirect";
		return 405;
	}
	if ((fp = fopen(r->path_translated, "r")) == 0) {
		lerror("fopen");
		r->error = "cannot open redirect file";
		return 500;
	}
	fgets(location, STRLEN, fp);
	fclose(fp);
	c = strchr(location, '\n');
	if (c)
		*c = '\0';
	else {
		r->error = "redirect url too long";
		return 500;
	}
	if (strncmp(location, "http://", 7)) {
		r->error = "can only redirect to http urls";
		return 500;
	}
	escape_url(location);
	r->location = location;
	return 302;
}
