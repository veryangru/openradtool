/*	$Id$ */
/*
 * Copyright (c) 2017--2019 Kristaps Dzonsons <kristaps@bsd.lv>
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
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ort.h"
#include "lang.h"
#include "ort-lang-sql.h"

static	const char *const upacts[UPACT__MAX] = {
	"NO ACTION", /* UPACT_NONE */
	"RESTRICT", /* UPACT_RESTRICT */
	"SET NULL", /* UPACT_NULLIFY */
	"CASCADE", /* UPACT_CASCADE */
	"SET DEFAULT", /* UPACT_DEFAULT */
};

static	const char *const ftypes[FTYPE__MAX] = {
	"INTEGER", /* FTYPE_BIT */
	"INTEGER", /* FTYPE_DATE */
	"INTEGER", /* FTYPE_EPOCH */
	"INTEGER", /* FTYPE_INT */
	"REAL", /* FTYPE_REAL */
	"BLOB", /* FTYPE_BLOB */
	"TEXT", /* FTYPE_TEXT */
	"TEXT", /* FTYPE_PASSWORD */
	"TEXT", /* FTYPE_EMAIL */
	NULL, /* FTYPE_STRUCT */
	"INTEGER", /* FTYPE_ENUM */
	"INTEGER", /* FTYPE_BITFIELD */
};

/* Forward declarations to get __attribute__ bits. */

static void gen_warnx(const struct pos *, const char *, ...)
	__attribute__((format(printf, 2, 3)));
static void diff_errx(const struct pos *,
		const struct pos *, const char *, ...)
	__attribute__((format(printf, 3, 4)));

static void
gen_warnx(const struct pos *pos, const char *fmt, ...)
{
	va_list	 ap;
	char	 buf[1024];

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	fprintf(stderr, "%s:%zu:%zu: %s\n", 
		pos->fname, pos->line, pos->column, buf);
}

static void
diff_errx(const struct pos *posold, 
	const struct pos *posnew, const char *fmt, ...)
{
	va_list	 ap;
	char	 buf[1024];

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	fprintf(stderr, "%s:%zu:%zu -> %s:%zu:%zu: error: %s\n", 
		posold->fname, posold->line, posold->column, 
		posnew->fname, posnew->line, posnew->column, 
		buf);
}

/*
 * Generate all PRAGMA prologue statements and sets "prol" if they've
 * been emitted or not.
 */
static void
gen_prologue(int *prol)
{

	if (*prol == 1)
		return;
	puts("PRAGMA foreign_keys=ON;\n");
	*prol = 1;
}

/*
 * Generate the "UNIQUE" statements on this table.
 */
static void
gen_unique(const struct unique *n, int *first)
{
	struct nref	*ref;
	int		 ffirst = 1;

	printf("%s\n\tUNIQUE(", *first ? "" : ",");

	TAILQ_FOREACH(ref, &n->nq, entries) {
		printf("%s%s", ffirst ? "" : ", ",
			ref->field->name);
		ffirst = 0;
	}
	putchar(')');
	*first = 0;
}

/*
 * Generate the "FOREIGN KEY" statements on this table.
 */
static void
gen_fkeys(const struct field *f, int *first)
{

	if (f->type == FTYPE_STRUCT || f->ref == NULL)
		return;

	printf("%s\n\tFOREIGN KEY(%s) REFERENCES %s(%s)",
		*first ? "" : ",",
		f->ref->source->name,
		f->ref->target->parent->name,
		f->ref->target->name);
	
	if (UPACT_NONE != f->actdel)
		printf(" ON DELETE %s", upacts[f->actdel]);
	if (UPACT_NONE != f->actup)
		printf(" ON UPDATE %s", upacts[f->actup]);

	*first = 0;
}

/*
 * Generate the columns for this table.
 */
static void
gen_field(const struct field *f, int *first, int comments)
{

	if (f->type == FTYPE_STRUCT)
		return;

	printf("%s\n", *first ? "" : ",");
	if (comments)
		print_commentt(1, COMMENT_SQL, f->doc);
	if (f->type == FTYPE_EPOCH || f->type == FTYPE_DATE)
		print_commentt(1, COMMENT_SQL, 
			"(Stored as a UNIX epoch value.)");
	printf("\t%s %s", f->name, ftypes[f->type]);
	if (FIELD_ROWID & f->flags)
		printf(" PRIMARY KEY");
	if (FIELD_UNIQUE & f->flags)
		printf(" UNIQUE");
	if (!(FIELD_ROWID & f->flags) &&
	    !(FIELD_NULL & f->flags))
		printf(" NOT NULL");
	*first = 0;
}

/*
 * Generate a table and all of its components: fields, foreign keys, and
 * unique statements.
 */
static void
gen_struct(const struct strct *p, int comments)
{
	const struct field 	*f;
	const struct unique 	*n;
	int	 		 first = 1;

	if (comments)
		print_commentt(0, COMMENT_SQL, p->doc);

	printf("CREATE TABLE %s (", p->name);
	TAILQ_FOREACH(f, &p->fq, entries)
		gen_field(f, &first, comments);
	TAILQ_FOREACH(f, &p->fq, entries)
		gen_fkeys(f, &first);
	TAILQ_FOREACH(n, &p->nq, entries)
		gen_unique(n, &first);
	puts("\n);\n"
	     "");
}

void
gen_sql(const struct config *cfg)
{
	const struct strct *p;

	puts("PRAGMA foreign_keys=ON;\n"
	     "");

	TAILQ_FOREACH(p, &cfg->sq, entries)
		gen_struct(p, 1);
}

/*
 * This is the ALTER TABLE version of the field generators in
 * gen_struct().
 */
static void
gen_diff_field_new(const struct field *fd)
{

	printf("ALTER TABLE %s ADD COLUMN %s %s",
		fd->parent->name, fd->name, ftypes[fd->type]);

	if (fd->flags & FIELD_ROWID)
		printf(" PRIMARY KEY");
	if (fd->flags & FIELD_UNIQUE)
		printf(" UNIQUE");
	if (!(fd->flags & FIELD_ROWID) && !(fd->flags & FIELD_NULL))
		printf(" NOT NULL");

	if (fd->ref != NULL)
		printf(" REFERENCES %s(%s)",
			fd->ref->target->parent->name,
			fd->ref->target->name);

	if (fd->actup != UPACT_NONE)
		printf(" ON UPDATE %s", upacts[fd->actup]);
	if (fd->actdel != UPACT_NONE)
		printf(" ON DELETE %s", upacts[fd->actdel]);

	if (fd->flags & FIELD_HASDEF) {
		printf(" DEFAULT ");
		switch (fd->type) {
		case FTYPE_BIT:
		case FTYPE_BITFIELD:
		case FTYPE_DATE:
		case FTYPE_EPOCH:
		case FTYPE_INT:
			printf("%" PRId64, fd->def.integer);
			break;
		case FTYPE_REAL:
			printf("%g", fd->def.decimal);
			break;
		case FTYPE_EMAIL:
		case FTYPE_TEXT:
			printf("'%s'", fd->def.string);
			break;
		case FTYPE_ENUM:
			printf("%" PRId64, fd->def.eitem->value);
			break;
		default:
			abort();
			break;
		}
	}

	puts(";");
}

static size_t
gen_check_fields(const struct diffq *q, int destruct)
{
	const struct diff	*d;
	const struct field	*f, *df;
	size_t	 		 errors = 0;
	unsigned int		 mask;

	mask = FIELD_ROWID | FIELD_NULL | FIELD_UNIQUE;

	TAILQ_FOREACH(d, q, entries) {
		switch (d->type) {
		case DIFF_DEL_FIELD:
			if (destruct || d->field->type == FTYPE_STRUCT)
				break;
			gen_warnx(&d->field->pos, 
				"field column was dropped");
			errors++;
			break;
		case DIFF_MOD_FIELD_BITF:
		case DIFF_MOD_FIELD_ENM:
		case DIFF_MOD_FIELD_TYPE:
			f = d->field_pair.into;
			df = d->field_pair.from;
			diff_errx(&df->pos, &f->pos, 
				"field type has changed");
			errors++;
			break;
		case DIFF_MOD_FIELD_FLAGS:
			f = d->field_pair.into;
			df = d->field_pair.from;

			/* We only care about SQL flags. */

			if ((f->flags & mask) == (df->flags & mask))
				break;
			diff_errx(&df->pos, &f->pos, 
				"field flag has changed");
			errors++;
			break;
		case DIFF_MOD_FIELD_ACTIONS:
			f = d->field_pair.into;
			df = d->field_pair.from;
			diff_errx(&df->pos, &f->pos, 
				"field action has changed");
			errors++;
			break;
		case DIFF_MOD_FIELD_REFERENCE:
			f = d->field_pair.into;
			df = d->field_pair.from;

			/* We only care about remote references. */

			if (f->type == FTYPE_STRUCT ||
			    df->type == FTYPE_STRUCT)
				break;
			diff_errx(&df->pos, &f->pos, 
				"field reference has changed");
			errors++;
			break;
		default:
			break;
		}
	}

	return errors;
}

/*
 * See gen_check_enms().
 * Same but for the bitfield types.
 */
static size_t
gen_check_bitfs(const struct diffq *q, int destruct)
{
	const struct diff	*d;
	size_t	 		 errors = 0;

	TAILQ_FOREACH(d, q, entries) {
		switch (d->type) {
		case DIFF_DEL_BITF:
			if (destruct)
				break;
			gen_warnx(&d->bitf->pos, 
				"deleted bitfield");
			errors++;
			break;
		case DIFF_MOD_BITIDX_VALUE:
			diff_errx(&d->bitidx_pair.from->pos, 
			  	&d->bitidx_pair.into->pos,
				"bitfield item has changed value");
			errors++;
			break;
		case DIFF_DEL_BITIDX:
			if (destruct)
				break;
			gen_warnx(&d->bitidx->pos, 
				"deleted bitfield item");
			errors++;
			break;
		default:
			break;
		}
	}

	return errors;
}

/*
 * Compare enumeration object return the number of errors.
 * If "destruct" is non-zero, allow dropped enumerations and enumeration
 * items.
 */
static size_t
gen_check_enms(const struct diffq *q, int destruct)
{
	const struct diff	*d;
	size_t	 		 errors = 0;

	TAILQ_FOREACH(d, q, entries) {
		switch (d->type) {
		case DIFF_DEL_ENM:
			if (destruct)
				break;
			gen_warnx(&d->enm->pos, 
				"deleted enumeration");
			errors++;
			break;
		case DIFF_MOD_EITEM_VALUE:
			diff_errx(&d->eitem_pair.from->pos, 
			  	&d->eitem_pair.into->pos,
				"item has changed value");
			errors++;
			break;
		case DIFF_DEL_EITEM:
			if (destruct)
				break;
			gen_warnx(&d->eitem->pos, 
				"deleted enumeration item");
			errors++;
			break;
		default:
			break;
		}
	}

	return errors;
}

static size_t
gen_check_strcts(const struct diffq *q, int destruct)
{
	const struct diff	*d;
	size_t			 errors = 0;

	TAILQ_FOREACH(d, q, entries) 
		switch (d->type) {
		case DIFF_DEL_STRCT:
			if (destruct)
				break;
			gen_warnx(&d->strct->pos, "deleted table");
			errors++;
			break;
		default:
			break;
		}

	return errors;
}

static size_t
gen_check_uniques(const struct diffq *q, int destruct)
{
	const struct diff	*d;
	size_t			 errors = 0;

	TAILQ_FOREACH(d, q, entries)
		switch (d->type) {
		case DIFF_ADD_UNIQUE:
			gen_warnx(&d->unique->pos, "new unique field");
			errors++;
			break;
		default:
			break;
		}

	return errors;
}

/*
 * Generate an SQL diff.
 * This returns zero on failure, non-zero on success.
 * "Failure" means that there were irreconcilable errors between the two
 * configurations, such as new tables or removed colunms or some such.
 * If "destruct" is non-zero, this allows for certain modifications that
 * would change the database, such as dropping tables.
 */
int
gen_diff_sql(const struct diffq *q, int destruct)
{
	const struct diff	*d;
	size_t	 		 errors = 0;
	int	 		 prol = 0;

	errors += gen_check_enms(q, destruct);
	errors += gen_check_bitfs(q, destruct);
	errors += gen_check_fields(q, destruct);
	errors += gen_check_strcts(q, destruct);
	errors += gen_check_uniques(q, destruct);

	if (errors)
		return 0;


	TAILQ_FOREACH(d, q, entries)
		if (d->type == DIFF_ADD_STRCT) {
			gen_prologue(&prol);
			gen_struct(d->strct, 0);
		}

	TAILQ_FOREACH(d, q, entries)
		if (d->type == DIFF_ADD_FIELD) {
			gen_prologue(&prol);
			gen_diff_field_new(d->field);
		}

	TAILQ_FOREACH(d, q, entries) 
		if (d->type == DIFF_DEL_STRCT && destruct) {
			gen_prologue(&prol);
			printf("DROP TABLE %s;\n", d->strct->name);
		}

	TAILQ_FOREACH(d, q, entries)
		if (d->type == DIFF_DEL_FIELD && destruct) {
			gen_prologue(&prol);
			printf("-- ALTER TABLE %s DROP COLUMN %s;\n", 
				d->field->parent->name, 
				d->field->name);
		}

	return 1;
}