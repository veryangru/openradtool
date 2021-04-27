/*	$Id$ */
/*
 * Copyright (c) 2017--2018 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "config.h"

#if HAVE_SYS_QUEUE
# include <sys/queue.h>
#endif

#include <assert.h>
#if HAVE_ERR
# include <err.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ort.h"

static	const char *const modtypes[MODTYPE__MAX] = {
	"cat", /* MODTYPE_CONCAT */
	"dec", /* MODTYPE_DEC */
	"inc", /* MODTYPE_INC */
	"set", /* MODTYPE_SET */
	"strset", /* MODTYPE_STRSET */
};

static	const char *const stypes[STYPE__MAX] = {
	"count", /* STYPE_COUNT */
	"get", /* STYPE_SEARCH */
	"list", /* STYPE_LIST */
	"iterate", /* STYPE_ITERATE */
};

static	const char *const optypes[OPTYPE__MAX] = {
	"eq", /* OPTYPE_EQUAL */
	"ge", /* OPTYPE_GE */
	"gt", /* OPTYPE_GT */
	"le", /* OPTYPE_LE */
	"lt", /* OPTYPE_LT */
	"neq", /* OPTYPE_NEQUAL */
	"like", /* OPTYPE_LIKE */
	"and", /* OPTYPE_AND */
	"or", /* OPTYPE_OR */
	"streq", /* OPTYPE_STREQ */
	"strneq", /* OPTYPE_STRNEQ */
	/* Unary types... */
	"isnull", /* OPTYPE_ISNULL */
	"notnull", /* OPTYPE_NOTNULL */
};

static	const char *const utypes[UP__MAX] = {
	"update", /* UP_MODIFY */
	"delete", /* UP_DELETE */
};

static int
check_rolemap(const struct rolemap *rm, const struct role *r)
{
	const struct rref	*rr;
	const struct role	*rc;

	TAILQ_FOREACH(rr, &rm->rq, entries)
		for (rc = r; rc != NULL; rc = rc->parent)
			if (rc == rr->role)
				return 1;

	return 0;
}

static size_t
print_name_db_insert(const struct strct *p)
{
	int	 rc;

	return (rc = printf("db_%s_insert", p->name)) > 0 ? rc : 0;
}

static size_t
print_name_db_search(const struct search *s)
{
	const struct sent *sent;
	size_t		   sz = 0;
	int	 	   rc;

	rc = printf("db_%s_%s", s->parent->name, stypes[s->type]);
	sz += rc > 0 ? rc : 0;

	if (s->name == NULL && !TAILQ_EMPTY(&s->sntq)) {
		sz += (rc = printf("_by")) > 0 ? rc : 0;
		TAILQ_FOREACH(sent, &s->sntq, entries) {
			rc = printf("_%s_%s", 
				sent->uname, optypes[sent->op]);
			sz += rc > 0 ? rc : 0;
		}
	} else if (s->name != NULL)
		sz += (rc = printf("_%s", s->name)) > 0 ? rc : 0;

	return sz;
}

static size_t
print_name_db_update(const struct update *u)
{
	const struct uref *ur;
	size_t	 	   col = 0;
	int		   rc;

	rc = printf("db_%s_%s", u->parent->name, utypes[u->type]);
	col = rc > 0 ? rc : 0;

	if (u->name == NULL && u->type == UP_MODIFY) {
		if (!(u->flags & UPDATE_ALL))
			TAILQ_FOREACH(ur, &u->mrq, entries) {
				rc = printf("_%s_%s", 
					ur->field->name, 
					modtypes[ur->mod]);
				col += rc > 0 ? rc : 0;
			}
		if (!TAILQ_EMPTY(&u->crq)) {
			col += (rc = printf("_by")) > 0 ? rc : 0;
			TAILQ_FOREACH(ur, &u->crq, entries) {
				rc = printf("_%s_%s", 
					ur->field->name, 
					optypes[ur->op]);
				col += rc > 0 ? rc : 0;
			}
		}
	} else if (u->name == NULL) {
		if (!TAILQ_EMPTY(&u->crq)) {
			col += (rc = printf("_by")) > 0 ? rc : 0;
			TAILQ_FOREACH(ur, &u->crq, entries) {
				rc = printf("_%s_%s", 
					ur->field->name, 
					optypes[ur->op]);
				col += rc > 0 ? rc : 0;
			}
		}
	} else 
		col += (rc = printf("_%s", u->name)) > 0 ? rc : 0;

	return col;
}

static void
gen_audit_exportable(const struct strct *p, const struct audit *a,
	const struct role *r)
{
	const struct field 	*f;
	size_t			 i;

	printf("\t\t\t\"exportable\": %s,\n"
	       "\t\t\t\"data\": [\n", 
	       a->ar.exported ? "true" : "false");

	TAILQ_FOREACH(f, &p->fq, entries)
		printf("\t\t\t\t\"%s\"%s\n", f->name, 
		       TAILQ_NEXT(f, entries) != NULL ?  "," : "");

	puts("\t\t\t],\n"
	     "\t\t\t\"accessfrom\": [");

	for (i = 0; i < a->ar.srsz; i++) {
		printf("\t\t\t\t{ \"function\": \"");
		print_name_db_search(a->ar.srs[i].sr);
		printf("\",\n"
		       "\t\t\t\t  \"exporting\": %s,\n"
		       "\t\t\t\t  \"path\": \"%s\" }%s\n",
		       a->ar.srs[i].exported ? "true" : "false",
		       a->ar.srs[i].path == NULL ? 
		       	"" : a->ar.srs[i].path,
		       i < a->ar.srsz - 1 ? "," : "");
	}

	puts("\t\t\t],");
}

static void
print_doc(const char *cp)
{
	char	 c;

	if (NULL == cp) {
		printf("null");
		return;
	}

	putchar('"');

	while ('\0' != (c = *cp++))
		switch (c) {
		case ('"'):
		case ('\\'):
		case ('/'):
			putchar('\\');
			putchar(c);
			break;
		case ('\b'):
			fputs("\\b", stdout);
			break;
		case ('\f'):
			fputs("\\f", stdout);
			break;
		case ('\n'):
			fputs("\\n", stdout);
			break;
		case ('\r'):
			fputs("\\r", stdout);
			break;
		case ('\t'):
			fputs("\\t", stdout);
			break;
		default:
			putchar(c);
			break;
		}

	putchar('"');
}

static void
gen_audit_inserts(const struct strct *p, const struct auditq *aq)
{
	const struct audit	*a;

	printf("\t\t\t\"insert\": ");

	TAILQ_FOREACH(a, aq, entries)
		if (a->type == AUDIT_INSERT && a->st == p) {
			putchar('"');
			print_name_db_insert(p);
			puts("\",");
			return;
		}

	puts("null,");
}

static void
gen_audit_deletes(const struct strct *p, const struct auditq *aq)
{
	const struct audit	*a;
	int	 		 first = 1;

	printf("\t\t\t\"deletes\": [");

	TAILQ_FOREACH(a, aq, entries)
		if (a->type == AUDIT_UPDATE &&
		    a->up->parent == p &&
		    a->up->type == UP_DELETE) {
			printf("%s\n\t\t\t\t\"", first ? "" : ",");
			print_name_db_update(a->up);
			putchar('"');
			first = 0;
		}

	puts("],");
}

static void
gen_audit_updates(const struct strct *p, const struct auditq *aq)
{
	const struct audit	*a;
	int			 first = 1;

	printf("\t\t\t\"updates\": [");

	TAILQ_FOREACH(a, aq, entries)
		if (a->type == AUDIT_UPDATE &&
		    a->up->parent == p &&
		    a->up->type == UP_MODIFY) {
			printf("%s\n\t\t\t\t\"", first ? "" : ",");
			print_name_db_update(a->up);
			putchar('"');
			first = 0;
		}

	puts("],");
}

static void
gen_protos_insert(const struct strct *s,
	int *first, const struct role *r)
{

	if (s->ins != NULL && s->ins->rolemap != NULL &&
	    check_rolemap(s->ins->rolemap, r)) {
		printf("%s\n\t\t\"", *first ? "" : ",");
		print_name_db_insert(s);
		fputs("\": {\n\t\t\t\"doc\": null,\n"
			"\t\t\t\"type\": \"insert\" }", stdout);
		*first = 0;
	}
}

static void
gen_protos_updates(const struct updateq *uq,
	int *first, const struct role *r)
{
	const struct update	*u;

	TAILQ_FOREACH(u, uq, entries) {
		if (u->rolemap == NULL ||
		    !check_rolemap(u->rolemap, r))
			continue;
		printf("%s\n\t\t\"", *first ? "" : ",");
		print_name_db_update(u);
		fputs("\": {\n\t\t\t\"doc\": ", stdout);
		print_doc(u->doc);
		printf(",\n\t\t\t\"type\": \"%s\" }", utypes[u->type]);
		*first = 0;
	}
}

static void
gen_protos_queries(const struct searchq *sq,
	int *first, const struct role *r)
{
	const struct search	*s;

	TAILQ_FOREACH(s, sq, entries) {
		if (s->rolemap == NULL || 
		    !check_rolemap(s->rolemap, r))
			continue;
		printf("%s\n\t\t\"", *first ? "" : ",");
		print_name_db_search(s);
		fputs("\": {\n\t\t\t\"doc\": ", stdout);
		print_doc(s->doc);
		printf(",\n\t\t\t\"type\": \"%s\" }", stypes[s->type]);
		*first = 0;
	}
}

static void
gen_protos_fields(const struct strct *s,
	int *first, const struct role *r)
{
	const struct field	*f;
	int			 noexport;

	TAILQ_FOREACH(f, &s->fq, entries) {
		noexport = f->type == FTYPE_PASSWORD ||
			(f->flags & FIELD_NOEXPORT) ||
			(f->rolemap != NULL &&
			check_rolemap(f->rolemap, r));
		printf("%s\n\t\t\"%s.%s\": {\n"
		       "\t\t\t\"export\": %s,\n"
		       "\t\t\t\"doc\": ",
			*first ? "" : ",",
			f->parent->name, f->name,
			noexport ? "false" : "true");
		print_doc(f->doc);
		printf(" }");
		*first = 0;
	}
}

static void
gen_audit_queries(const struct strct *p, const struct auditq *aq,
	enum stype t, const char *tp)
{
	const struct audit	*a;
	int			 first = 1;

	printf("\t\t\t\"%s\": [", tp);

	TAILQ_FOREACH(a, aq, entries)
		if (a->type == AUDIT_QUERY &&
		    a->sr->parent == p &&
		    a->sr->type == t) {
			printf("%s\n\t\t\t\t\"", first ? "" : ",");
			print_name_db_search(a->sr);
			putchar('"');
			first = 0;
		}

	/* Last item: don't have a trailing comma. */

	printf("]%s\n", t != STYPE_SEARCH ? "," : "");
}

static void
gen_audit_json(const struct config *cfg, const struct auditq *aq,
	const struct role *r)
{
	const struct strct	*s;
	const struct audit	*a;
	int	 		 first;

	printf("(function(root) {\n"
	       "\t'use strict';\n"
	       "\tvar audit = {\n"
	       "\t    \"role\": \"%s\",\n"
	       "\t    \"doc\": ", r->name);
	print_doc(r->doc);
	puts(",\n\t    \"access\": [");

	TAILQ_FOREACH(s, &cfg->sq, entries) {
		printf("\t\t{ \"name\": \"%s\",\n"
		       "\t\t  \"access\": {\n", s->name);

		TAILQ_FOREACH(a, aq, entries)
			if (a->type == AUDIT_REACHABLE &&
			    a->ar.st == s) {
				gen_audit_exportable(s, a, r);
				break;
			}

		gen_audit_inserts(s, aq);
		gen_audit_updates(s, aq);
		gen_audit_deletes(s, aq);
		gen_audit_queries(s, aq, STYPE_ITERATE, "iterates");
		gen_audit_queries(s, aq, STYPE_LIST, "lists");
		gen_audit_queries(s, aq, STYPE_SEARCH, "searches");

		printf("\t\t}}%s\n", 
			TAILQ_NEXT(s, entries) != NULL ? "," : "");
	}

	fputs("\t],\n"
	      "\t\"functions\": {", stdout);

	first = 1;
	TAILQ_FOREACH(s, &cfg->sq, entries) {
		gen_protos_queries(&s->sq, &first, r);
		gen_protos_updates(&s->uq, &first, r);
		gen_protos_updates(&s->dq, &first, r);
		gen_protos_insert(s, &first, r);
	}

	puts("\n\t},\n"
	     "\t\"fields\": {");

	first = 1;
	TAILQ_FOREACH(s, &cfg->sq, entries)
		gen_protos_fields(s, &first, r);

	puts("\n\t}};\n"
	     "\n"
	     "\troot.audit = audit;\n"
	     "})(this);");
}

int
main(int argc, char *argv[])
{
	const char		 *role = "default";
	struct config		 *cfg = NULL;
	const struct role	 *r;
	int			  c, rc = 0;
	size_t			  i;
	FILE			**confs = NULL;
	struct auditq		 *aq = NULL;

#if HAVE_PLEDGE
	if (pledge("stdio rpath", NULL) == -1)
		err(EXIT_FAILURE, "pledge");
#endif

	while ((c = getopt(argc, argv, "r:")) != -1)
		switch (c) {
		case 'r':
			role = optarg;
			break;
		default:
			goto usage;
		}

	argc -= optind;
	argv += optind;

	/* Read in all of our files now so we can repledge. */

	if (argc > 0 &&
	    (confs = calloc(argc, sizeof(FILE *))) == NULL)
		err(EXIT_FAILURE, NULL);

	for (i = 0; i < (size_t)argc; i++)
		if ((confs[i] = fopen(argv[i], "r")) == NULL)
			err(EXIT_FAILURE, "%s", argv[i]);

#if HAVE_PLEDGE
	if (pledge("stdio", NULL) == -1)
		err(EXIT_FAILURE, "pledge");
#endif

	if ((cfg = ort_config_alloc()) == NULL)
		err(EXIT_FAILURE, NULL);

	for (i = 0; i < (size_t)argc; i++)
		if (!ort_parse_file(cfg, confs[i], argv[i]))
			goto out;

	if (argc == 0 && !ort_parse_file(cfg, stdin, "<stdin>"))
		goto out;

	if (!ort_parse_close(cfg))
		goto out;

	if (TAILQ_EMPTY(&cfg->arq)) {
		warnx("roles not enabled");
		goto out;
	}

	TAILQ_FOREACH(r, &cfg->arq, allentries)
		if (strcasecmp(r->name, role) == 0)
			break;

	if (r == NULL) {
		warnx("role not found: %s", role);
		goto out;
	} else if ((aq = ort_audit(r, cfg)) == NULL) {
		warn(NULL);
		goto out;
	}

	gen_audit_json(cfg, aq, r);
	rc = 1;
out:
	ort_write_msg_file(stderr, &cfg->mq);
	ort_auditq_free(aq);
	ort_config_free(cfg);

	for (i = 0; i < (size_t)argc; i++)
		fclose(confs[i]);

	free(confs);
	return rc ? EXIT_SUCCESS : EXIT_FAILURE;
usage:
	fprintf(stderr, "usage: %s [-r role] [config...]\n",
		getprogname());
	return EXIT_FAILURE;
}
