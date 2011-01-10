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

static void range_parse(const char *input_str,  char *flags,
						   char **lbound_str, char **ubound_str);
static char *range_deparse(char flags, char *lbound_str, char *ubound_str);

/*
 *----------------------------------------------------------
 * I/O FUNCTIONS
 *----------------------------------------------------------
 */

Datum
range_in(PG_FUNCTION_ARGS)
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
	range_parse(input_str, &flags, &lbound_str, &ubound_str);

	/* input */
	getTypeInputInfo(subtype, &subInput, &ioParam);
	fmgr_info(subInput, &subInputFn);

	if (RANGE_HAS_LBOUND(flags))
		lbound = InputFunctionCall(&subInputFn, lbound_str, ioParam, typmod);
	if (RANGE_HAS_UBOUND(flags))
		ubound = InputFunctionCall(&subInputFn, ubound_str, ioParam, typmod);

	/* serialize and canonicalize */
	range = make_range(rngtypoid, flags, lbound, ubound);

	PG_RETURN_DATUM(range);
}

Datum
range_out(PG_FUNCTION_ARGS)
{
	RangeType *range = PG_GETARG_RANGE(0);

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
	range_deserialize(range, &rngtypoid, &flags, &lbound, &ubound);
	subtype = get_range_subtype(rngtypoid);

	/* output */
	getTypeOutputInfo(subtype, &subOutput, &isVarlena);
	fmgr_info(subOutput, &subOutputFn);

	if (RANGE_HAS_LBOUND(flags))
		lbound_str = OutputFunctionCall(&subOutputFn, lbound);
	if (RANGE_HAS_UBOUND(flags))
		ubound_str = OutputFunctionCall(&subOutputFn, ubound);

	/* deparse */
	output_str = range_deparse(flags, lbound_str, ubound_str);

	PG_RETURN_CSTRING(output_str);
}

Datum
range_recv(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}

Datum
range_send(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}


/*
 *----------------------------------------------------------
 * GENERIC FUNCTIONS
 *----------------------------------------------------------
 */


/* range -> subtype */
Datum
range_lbound(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}

Datum
range_ubound(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}


/* range -> bool */
Datum
range_empty(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}

Datum
range_lb_null(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}

Datum
range_ub_null(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}

Datum
range_lb_inf(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}

Datum
range_ub_inf(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}


/* range, range -> bool */
Datum
range_eq(PG_FUNCTION_ARGS)
{
	RangeType *r1 = PG_GETARG_RANGE(0);
	RangeType *r2 = PG_GETARG_RANGE(1);

	Oid			rtype1, rtype2;
	char		fl1, fl2;
	Datum		lb1, lb2;
	Datum		ub1, ub2;
	regproc		cmpFn;

	range_deserialize(r1, &rtype1, &fl1, &lb1, &ub1);
	range_deserialize(r2, &rtype2, &fl2, &lb2, &ub2);

	if (rtype1 != rtype2)
		elog(ERROR, "range types do not match");

	if (fl1 != fl2)
		PG_RETURN_BOOL(false);

	cmpFn = get_range_subtype_cmp(rtype1);

	if (DatumGetInt32(OidFunctionCall2(cmpFn, lb1, lb2)) != 0)
		PG_RETURN_BOOL(false);

	if (DatumGetInt32(OidFunctionCall2(cmpFn, ub1, ub2)) != 0)
		PG_RETURN_BOOL(false);

	PG_RETURN_BOOL(true);
}

Datum
range_neq(PG_FUNCTION_ARGS)
{
	bool eq = DatumGetBool(range_eq(fcinfo));

	PG_RETURN_BOOL(!eq);
}

Datum
range_contains(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}

Datum
range_contained_by(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}

Datum
range_before(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}

Datum
range_after(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}

Datum
range_overlaps(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}

Datum
range_overleft(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}

Datum
range_overright(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}


/* range, range -> range */
Datum
range_minus(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}

Datum
range_union(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}

Datum
range_intersect(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}


/* GiST support */
Datum
range_gist_consistent(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}

Datum
range_gist_union(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}

Datum
range_gist_penalty(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}

Datum
range_gist_picksplit(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}

Datum
range_gist_same(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}


/*
 *----------------------------------------------------------
 * SUPPORT FUNCTIONS
 *----------------------------------------------------------
 */

/*
 * This serializes a range, but does not canonicalize it. This should
 * only be called by a canonicalization function.
 */
Datum
range_serialize(Oid rngtypoid, char flags, Datum lbound, Datum ubound)
{
	Datum		 range;
	size_t		 msize;
	char		*ptr;
	Oid			 subtype  = get_range_subtype(rngtypoid);
	int16		 typlen	  = get_typlen(subtype);
	char		 typalign = get_typalign(subtype);
	size_t		 llen;
	size_t		 ulen;

	msize  = VARHDRSZ;
	msize += sizeof(Oid);
	msize += sizeof(char);

	if (RANGE_HAS_LBOUND(flags))
	{
		llen = att_addlength_datum(0, typlen, lbound);
		msize = att_align_nominal(msize, typalign);
		msize += llen;
	}

	if (RANGE_HAS_UBOUND(flags))
	{
		ulen = att_addlength_datum(0, typlen, ubound);
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
range_deserialize(RangeType *range, Oid *rngtypoid, char *flags,
				  Datum *lbound, Datum *ubound)
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
	else
		*lbound = (Datum) 0;

	if (RANGE_HAS_UBOUND(*flags))
	{
		ptr = (char *) att_align_nominal(ptr, typalign);
		ulen = att_addlength_datum(0, typlen, (Datum) ptr);
		*ubound = (Datum) palloc(ulen);
		memcpy((void *) *ubound, ptr, ulen);
		ptr += ulen;
	}
	else
		*ubound = (Datum) 0;
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

	range = range_serialize(rngtypoid, flags, lbound, ubound);

	if (OidIsValid(canonical))
		range = OidFunctionCall1(canonical, range);

	PG_RETURN_DATUM(range);
}

/*
 *----------------------------------------------------------
 * STATIC FUNCTIONS
 *----------------------------------------------------------
 */

static void
range_parse(const char *input_str,  char *flags, char **lbound_str,
			   char **ubound_str)
{
	int			 ilen		   = strlen(input_str);
	char		*lb			   = palloc0(ilen + 1);
	char		*ub			   = palloc0(ilen + 1);
	int			 lidx		   = 0;
	int			 uidx		   = 0;
	bool		 inside_quotes = false;
	bool		 lb_quoted	   = false;
	bool		 ub_quoted	   = false;
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
			{
				if (pstate == RANGE_PSTATE_LB)
					lb_quoted = true;
				else if (pstate == RANGE_PSTATE_UB)
					ub_quoted = true;
				inside_quotes = !inside_quotes;
			}
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

		elog(ERROR, "syntax error on range input: characters after end of input");
	}

	if (inside_quotes)
		elog(ERROR, "syntax error on range input: unterminated quotation");

	if (fl & RANGE_EMPTY)
		fl = RANGE_EMPTY;

	if (!lb_quoted && strncmp(lb, "NULL", ilen) == 0)
		fl |= RANGE_LB_NULL;
	if (!ub_quoted && strncmp(ub, "NULL", ilen) == 0)
		fl |= RANGE_UB_NULL;
	if (!lb_quoted && strncmp(lb, "INF", ilen) == 0)
		fl |= RANGE_LB_INF;
	if (!ub_quoted && strncmp(ub, "INF", ilen) == 0)
		fl |= RANGE_UB_INF;

	if (!RANGE_HAS_LBOUND(fl))
	{
		lb	= NULL;
		fl &= ~RANGE_LB_INC;
	}

	if (!RANGE_HAS_UBOUND(fl))
	{
		ub	= NULL;
		fl &= ~RANGE_UB_INC;
	}

	*lbound_str = lb;
	*ubound_str = ub;
	*flags		= fl;

	return;
}

static char *
range_deparse(char flags, char *lbound_str, char *ubound_str)
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

	lb_str = lbound_str;
	ub_str = ubound_str;

	if (!RANGE_HAS_LBOUND(flags))
		lb_str = (flags & RANGE_LB_NULL) ? "NULL" : "INF";
	if (!RANGE_HAS_UBOUND(flags))
		ub_str = (flags & RANGE_UB_NULL) ? "NULL" : "INF";

	appendStringInfo(str, "%c %s, %s %c", lb_c, lb_str, ub_str, ub_c);

	return str->data;
}
