/*-------------------------------------------------------------------------
 *
 * enum.c
 *	  I/O functions, operators, aggregates etc for enum types
 *
 * Copyright (c) 2006-2010, PostgreSQL Global Development Group
 *
 *
 * IDENTIFICATION
 *	  src/backend/utils/adt/enum.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "catalog/pg_range.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/rangetypes.h"

typedef enum
{
	RANGE_PSTATE_INIT,
	RANGE_PSTATE_LB,
	RANGE_PSTATE_UB,
	RANGE_PSTATE_DONE
} RangePState;

static void anyrange_parse(const char *input_str,  char *flags,
						   char **lbound_str, char **ubound_str);
static char *anyrange_deparse(char flags, char *lbound_str, char *ubound_str);

/* Basic I/O support */

Datum
anyrange_in(PG_FUNCTION_ARGS)
{
	char		*input_str = PG_GETARG_CSTRING(0);
	Oid			 rngtypoid = PG_GETARG_OID(1);
	Oid			 typmod	   = PG_GETARG_INT32(2);

	char		 flags;
	Datum		 lbound;
	Datum		 ubound;
	Datum		 range;
	char		*lbound_str;
	char		*ubound_str;

	Oid			subtype;
	regproc		subInput;
	FmgrInfo	subInputFn;
	Oid			ioParam;

	if (rngtypoid == ANYRANGEOID)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot accept a value of type anyrange")));

	subtype = get_range_subtype(rngtypoid);

	/* parse */
	anyrange_parse(input_str, &flags, &lbound_str, &ubound_str);

	/* input */
	getTypeInputInfo(subtype, &subInput, &ioParam);
	fmgr_info(subInput, &subInputFn);

	lbound = InputFunctionCall(&subInputFn, lbound_str, ioParam, typmod);
	ubound = InputFunctionCall(&subInputFn, ubound_str, ioParam, typmod);

	/* serialize and canonicalize */
	range = make_range(rngtypoid, flags, lbound, ubound);

	PG_RETURN_DATUM(range);
}

Datum
anyrange_out(PG_FUNCTION_ARGS)
{
	Datum		range = PG_GETARG_DATUM(0);

	Oid			rngtypoid;
	Oid			subtype;

	regproc		subOutput;
	FmgrInfo	subOutputFn;
	bool		isVarlena;

	char		 flags;
	Datum		 lbound;
	Datum		 ubound;
	char		*lbound_str;
	char		*ubound_str;
	char		*output_str;

	/* deserialize */
	anyrange_deserialize(range, &rngtypoid, &flags, &lbound, &ubound);
	subtype = get_range_subtype(rngtypoid);

	/* output */
	getTypeOutputInfo(subtype, &subOutput, &isVarlena);
	fmgr_info(subOutput, &subOutputFn);
	lbound_str = OutputFunctionCall(&subOutputFn, lbound);
	ubound_str = OutputFunctionCall(&subOutputFn, ubound);

	/* deparse */
	output_str = anyrange_deparse(flags, lbound_str, ubound_str);

	PG_RETURN_CSTRING(output_str);
}

Datum
anyrange_recv(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}

Datum
anyrange_send(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}

/*
 * This serializes a range, but does not canonicalize it. This should
 * only be called by a canonicalization function.
 */
Datum
anyrange_serialize(Oid rngtypoid, char flags, Datum lbound, Datum ubound)
{
	Datum		 range;
	size_t		 msize;
	char		*ptr;
	Oid			 subtype  = get_range_subtype(rngtypoid);
	int16		 typlen	  = get_typlen(subtype);
	char		 typalign = get_typalign(subtype);
	size_t		 llen	  = att_addlength_datum(0, typlen, lbound);
	size_t		 ulen	  = att_addlength_datum(0, typlen, ubound);

	msize  = VARHDRSZ;
	msize += sizeof(Oid);
	msize += sizeof(char);

	if (RANGE_HAS_LBOUND(flags))
	{
		msize = att_align_nominal(msize, typalign);
		msize += llen;
	}

	if (RANGE_HAS_UBOUND(flags))
	{
		msize = att_align_nominal(msize, typalign);
		msize += ulen;
	}

	ptr = palloc(msize);
	range = (Datum) ptr;

	ptr += VARHDRSZ;

	memcpy(ptr, &rngtypoid, sizeof(Oid));
	ptr += sizeof(Oid);
	memcpy(ptr, &flags, sizeof(char));
	ptr += sizeof(char);

	if (RANGE_HAS_LBOUND(flags))
	{
		ptr = (char *) att_align_nominal(ptr, typalign);
		memcpy(ptr, (void *) lbound, llen);
		ptr += llen;
	}

	if (RANGE_HAS_UBOUND(flags))
	{
		ptr = (char *) att_align_nominal(ptr, typalign);
		memcpy(ptr, (void *) ubound, ulen);
		ptr += ulen;
	}

	SET_VARSIZE(range, msize);
	PG_RETURN_DATUM(range);
}

void
anyrange_deserialize(Datum range, Oid *rngtypoid, char *flags, Datum *lbound,
					 Datum *ubound)
{
	char		*ptr = VARDATA(range);
	int			 llen;
	int			 ulen;
	char		 typalign;
	int16		 typlen;

	memcpy(rngtypoid, ptr, sizeof(Oid));
	ptr += sizeof(Oid);
	memcpy(flags, ptr, sizeof(char));
	ptr += sizeof(char);

	if (*rngtypoid == ANYRANGEOID)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot output a value of type anyrange")));

	typalign = get_typalign(*rngtypoid);
	typlen	 = get_typlen(*rngtypoid);

	if (RANGE_HAS_LBOUND(*flags))
	{
		ptr = (char *) att_align_nominal(ptr, typalign);
		llen = att_addlength_datum(0, typlen, (Datum) ptr);
		*lbound = (Datum) palloc(llen);
		memcpy((void *) *lbound, ptr, llen);
		ptr += llen;
	}

	if (RANGE_HAS_UBOUND(*flags))
	{
		ptr = (char *) att_align_nominal(ptr, typalign);
		ulen = att_addlength_datum(0, typlen, (Datum) ptr);
		*ubound = (Datum) palloc(ulen);
		memcpy((void *) *ubound, ptr, ulen);
		ptr += ulen;
	}
}

/*
 * This both serializes and caonicalizes (if applicable) the
 * range. This should be used by most callers.
 */
Datum
make_range(Oid rngtypoid, char flags, Datum lbound, Datum ubound)
{
	Datum range;
	Oid canonical = get_range_canonical(rngtypoid);

	range = anyrange_serialize(rngtypoid, flags, lbound, ubound);

	if (OidIsValid(canonical))
		range = OidFunctionCall1(canonical, range);

	PG_RETURN_DATUM(range);
}

/* STATIC FUNCTIONS */

static void
anyrange_parse(const char *input_str,  char *flags, char **lbound_str,
			   char **ubound_str)
{
	int			 ilen		   = strlen(input_str);
	char		*lb			   = palloc0(ilen + 1);
	char		*ub			   = palloc0(ilen + 1);
	int			 lidx		   = 0;
	int			 uidx		   = 0;
	bool		 inside_quotes = false;
	bool		 escape		   = false;
	char		 fl			   = 0;
	RangePState  pstate		   = RANGE_PSTATE_INIT;
	int			 i;

	for (i = 0; i < ilen; i++)
	{
		char ch = input_str[i];

		if((ch == '"' || ch == '\\') && !escape)
		{
			if (pstate != RANGE_PSTATE_LB && pstate != RANGE_PSTATE_UB)
				elog(ERROR, "syntax error on range input, character %d", i);
			if (ch == '"')
				inside_quotes = !inside_quotes;
			else if (ch == '\\')
				escape = true;
			continue;
		}

		escape = false;

		if(isspace(ch) && !inside_quotes)
			continue;

		if(ch == '-' && pstate == RANGE_PSTATE_INIT)
		{
			fl |= RANGE_EMPTY;
			pstate = RANGE_PSTATE_DONE;
			/* read the rest to make sure it's whitespace */
			continue;
		}

		if((ch == '[' || ch == '(') && !inside_quotes)
		{
			if (pstate != RANGE_PSTATE_INIT)
				elog(ERROR, "syntax error on range input, character %d", i);
			if (ch == '[')
				fl |= RANGE_LB_INC;
			pstate = RANGE_PSTATE_LB;
			continue;
		}

		if((ch == ')' || ch == ']') && !inside_quotes)
		{
			if (pstate != RANGE_PSTATE_UB)
				elog(ERROR, "syntax error on range input, character %d", i);
			if (ch == ']')
				fl |= RANGE_UB_INC;
			pstate = RANGE_PSTATE_DONE;
			continue;
		}

		if(ch == ',' && !inside_quotes)
		{
			if (pstate != RANGE_PSTATE_LB)
				elog(ERROR, "syntax error on range input, character %d", i);
			pstate = RANGE_PSTATE_UB;
			continue;
		}

		if (pstate == RANGE_PSTATE_LB)
		{
			lb[lidx++] = ch;
			continue;
		}

		if (pstate == RANGE_PSTATE_UB)
		{
			ub[uidx++] = ch;
			continue;
		}

		elog(ERROR, "syntax error on range input, character %d", i);
	}

	if (!RANGE_HAS_LBOUND(fl))
		fl &= ~RANGE_LB_INC;

	if (!RANGE_HAS_UBOUND(fl))
		fl &= ~RANGE_UB_INC;

	if (fl & RANGE_EMPTY)
		fl = RANGE_EMPTY;

	*flags		= fl;
	*lbound_str = lb;
	*ubound_str = ub;
	return;
}

static char *
anyrange_deparse(char flags, char *lbound_str, char *ubound_str)
{
	StringInfo	 str = makeStringInfo();
	char		 lb_c;
	char		 ub_c;
	char		*lb_str;
	char		*ub_str;

	if (flags & RANGE_EMPTY)
		return pstrdup("-");

	lb_c = (flags & RANGE_LB_INC) ? '[' : '(';
	ub_c = (flags & RANGE_UB_INC) ? ']' : ')';

	lb_str = (flags & RANGE_LB_NULL) ? "NULL" : lbound_str;
	ub_str = (flags & RANGE_UB_NULL) ? "NULL" : ubound_str;

	lb_str = (flags & RANGE_LB_INF) ? "INF" : lbound_str;
	ub_str = (flags & RANGE_UB_INF) ? "INF" : ubound_str;

	appendStringInfo(str, "%c %s, %s %c", lb_c, lb_str, ub_str, ub_c);

	return str->data;
}
