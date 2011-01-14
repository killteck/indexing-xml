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
#include "utils/fmgroids.h"
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
static Datum range_make2(PG_FUNCTION_ARGS);
static bool range_contains_internal(RangeType *r1, RangeType *r2,
									bool *isnull);

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

	PG_RETURN_RANGE(range);
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


/* constructors */

Datum
range_make1(PG_FUNCTION_ARGS)
{
	Datum		 arg	  = PG_GETARG_DATUM(0);
	char		 flags	  = RANGE_LB_INC | RANGE_UB_INC;
	Oid			 subtype  = get_fn_expr_argtype(fcinfo->flinfo,0);
	Oid			 rngtypid = get_range_from_subtype(subtype);
	RangeType	*range;

	if (PG_ARGISNULL(0))
		flags = RANGE_LB_NULL | RANGE_UB_NULL;

	range = DatumGetRangeType(make_range(rngtypid, flags, arg, arg));

	PG_RETURN_RANGE(range);
}

Datum
range_linf_(PG_FUNCTION_ARGS)
{
	Datum		 arg	  = PG_GETARG_DATUM(0);
	char		 flags	  = RANGE_LB_INF;
	Oid			 subtype  = get_fn_expr_argtype(fcinfo->flinfo,0);
	Oid			 rngtypid = get_range_from_subtype(subtype);
	RangeType	*range;

	if (PG_ARGISNULL(0))
		flags |= RANGE_UB_NULL;

	range = DatumGetRangeType(make_range(rngtypid, flags, arg, arg));

	PG_RETURN_RANGE(range);
}

Datum
range_uinf_(PG_FUNCTION_ARGS)
{
	Datum		 arg	  = PG_GETARG_DATUM(0);
	char		 flags	  = RANGE_UB_INF;
	Oid			 subtype  = get_fn_expr_argtype(fcinfo->flinfo,0);
	Oid			 rngtypid = get_range_from_subtype(subtype);
	RangeType	*range;

	if (PG_ARGISNULL(0))
		flags |= RANGE_LB_NULL;

	range = DatumGetRangeType(make_range(rngtypid, flags, arg, arg));

	PG_RETURN_RANGE(range);
}

Datum
range_linfi(PG_FUNCTION_ARGS)
{
	Datum		 arg	  = PG_GETARG_DATUM(0);
	char		 flags	  = RANGE_LB_INF;
	Oid			 subtype  = get_fn_expr_argtype(fcinfo->flinfo,0);
	Oid			 rngtypid = get_range_from_subtype(subtype);
	RangeType	*range;

	if (PG_ARGISNULL(0))
		flags |= RANGE_UB_NULL;
	else
		flags |= RANGE_UB_INC;

	range = DatumGetRangeType(make_range(rngtypid, flags, arg, arg));

	PG_RETURN_RANGE(range);
}

Datum
range_uinfi(PG_FUNCTION_ARGS)
{
	Datum		 arg	  = PG_GETARG_DATUM(0);
	char		 flags	  = RANGE_UB_INF;
	Oid			 subtype  = get_fn_expr_argtype(fcinfo->flinfo,0);
	Oid			 rngtypid = get_range_from_subtype(subtype);
	RangeType	*range;

	if (PG_ARGISNULL(0))
		flags |= RANGE_LB_NULL;
	else
		flags |= RANGE_LB_INC;

	range = DatumGetRangeType(make_range(rngtypid, flags, arg, arg));

	PG_RETURN_RANGE(range);
}

Datum
range(PG_FUNCTION_ARGS)
{
	return range_make2(fcinfo);
}

Datum
range__(PG_FUNCTION_ARGS)
{
	return range_make2(fcinfo);
}

Datum
range_i(PG_FUNCTION_ARGS)
{
	return range_make2(fcinfo);
}

Datum
rangei_(PG_FUNCTION_ARGS)
{
	return range_make2(fcinfo);
}

Datum
rangeii(PG_FUNCTION_ARGS)
{
	return range_make2(fcinfo);
}

/* range -> subtype */
Datum
range_lbound(PG_FUNCTION_ARGS)
{
	RangeType *r1 = PG_GETARG_RANGE(0);

	Oid			rtype1;
	char		fl1;
	Datum		lb1;
	Datum		ub1;

	range_deserialize(r1, &rtype1, &fl1, &lb1, &ub1);

	if (fl1 & RANGE_EMPTY)
		elog(ERROR, "range is empty");
	if (fl1 & RANGE_LB_INF)
		elog(ERROR, "range lower bound is infinite");

	if (fl1 & RANGE_LB_NULL)
		PG_RETURN_NULL();

	PG_RETURN_DATUM(lb1);
}

Datum
range_ubound(PG_FUNCTION_ARGS)
{
	RangeType *r1 = PG_GETARG_RANGE(0);

	Oid			rtype1;
	char		fl1;
	Datum		lb1;
	Datum		ub1;

	range_deserialize(r1, &rtype1, &fl1, &lb1, &ub1);

	if (fl1 & RANGE_EMPTY)
		elog(ERROR, "range is empty");
	if (fl1 & RANGE_UB_INF)
		elog(ERROR, "range upper bound is infinite");

	if (fl1 & RANGE_UB_NULL)
		PG_RETURN_NULL();

	PG_RETURN_DATUM(ub1);
}


/* range -> bool */
Datum
range_empty(PG_FUNCTION_ARGS)
{
	RangeType *r1 = PG_GETARG_RANGE(0);

	Oid			rtype1;
	char		fl1;
	Datum		lb1;
	Datum		ub1;

	range_deserialize(r1, &rtype1, &fl1, &lb1, &ub1);

	PG_RETURN_BOOL(fl1 & RANGE_EMPTY);
}

Datum
range_lb_inf(PG_FUNCTION_ARGS)
{
	RangeType *r1 = PG_GETARG_RANGE(0);

	Oid			rtype1;
	char		fl1;
	Datum		lb1;
	Datum		ub1;

	range_deserialize(r1, &rtype1, &fl1, &lb1, &ub1);

	PG_RETURN_BOOL(fl1 & RANGE_LB_INF);
}

Datum
range_ub_inf(PG_FUNCTION_ARGS)
{
	RangeType *r1 = PG_GETARG_RANGE(0);

	Oid			rtype1;
	char		fl1;
	Datum		lb1;
	Datum		ub1;

	range_deserialize(r1, &rtype1, &fl1, &lb1, &ub1);

	PG_RETURN_BOOL(fl1 & RANGE_UB_INF);
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
range_contains_elem(PG_FUNCTION_ARGS)
{
	RangeType	*r1		  = PG_GETARG_RANGE(0);
	RangeType	*r2;
	Datum		 val	  = PG_GETARG_DATUM(1);
	bool		 result;
	bool		 isnull;
	char		 flags	  = RANGE_LB_INC | RANGE_UB_INC;
	Oid			 rngtypid = get_fn_expr_argtype(fcinfo->flinfo,0);

	r2 = DatumGetRangeType(make_range(rngtypid, flags, val, val));

	result = range_contains_internal(r1, r2, &isnull);
	if (isnull)
		PG_RETURN_NULL();
	else
		PG_RETURN_BOOL(result);
}

Datum
range_contains(PG_FUNCTION_ARGS)
{
	RangeType	*r1 = PG_GETARG_RANGE(0);
	RangeType	*r2 = PG_GETARG_RANGE(1);
	bool		 result;
	bool		 isnull;

	result = range_contains_internal(r1, r2, &isnull);
	if (isnull)
		PG_RETURN_NULL();
	else
		PG_RETURN_BOOL(result);
}

Datum
range_contained_by(PG_FUNCTION_ARGS)
{
	RangeType	*r1 = PG_GETARG_RANGE(0);
	RangeType	*r2 = PG_GETARG_RANGE(1);
	bool		 result;
	bool		 isnull;

	result = range_contains_internal(r1, r2, &isnull);
	if (isnull)
		PG_RETURN_NULL();
	else
		PG_RETURN_BOOL(result);
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

Datum range_adjacent(PG_FUNCTION_ARGS)
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
	PG_RETURN_RANGE(range);
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

	PG_RETURN_RANGE(range);
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
	if (!lb_quoted && strncmp(lb, "-INF", ilen) == 0)
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
		lb_str = (flags & RANGE_LB_NULL) ? "NULL" : "-INF";
	if (!RANGE_HAS_UBOUND(flags))
		ub_str = (flags & RANGE_UB_NULL) ? "NULL" : "INF";

	appendStringInfo(str, "%c %s, %s %c", lb_c, lb_str, ub_str, ub_c);

	return str->data;
}

static Datum
range_make2(PG_FUNCTION_ARGS)
{
	Datum		 arg1	  = PG_GETARG_DATUM(0);
	Datum		 arg2	  = PG_GETARG_DATUM(1);
	char		 flags;
	Oid			 subtype  = get_fn_expr_argtype(fcinfo->flinfo,0);
	Oid			 rngtypid = get_range_from_subtype(subtype);
	RangeType	*range;

	switch(fcinfo->flinfo->fn_oid)
	{
		case F_RANGE__:
			flags = 0;
			break;
		case F_RANGE_I:
			flags = RANGE_UB_INC;
			break;
		case F_RANGE:
		case F_RANGEI_:
			flags = RANGE_LB_INC;
			break;
		case F_RANGEII:
			flags = RANGE_LB_INC | RANGE_UB_INC;
			break;
	}

	if (PG_ARGISNULL(0))
	{
		flags &= ~RANGE_LB_INC;
		flags |= RANGE_LB_NULL;
	}

	if (PG_ARGISNULL(1))
	{
		flags &= ~RANGE_UB_INC;
		flags |= RANGE_UB_NULL;
	}

	range = DatumGetRangeType(make_range(rngtypid, flags, arg1, arg2));

	PG_RETURN_RANGE(range);
}

static bool
range_contains_internal(RangeType *r1, RangeType *r2, bool *isnull)
{
	Oid			rtype1, rtype2;
	char		fl1, fl2;
	Datum		lb1, lb2;
	Datum		ub1, ub2;
	regproc		cmpFn;
	int			cmp_lb;
	int			cmp_ub;

	range_deserialize(r1, &rtype1, &fl1, &lb1, &ub1);
	range_deserialize(r2, &rtype2, &fl2, &lb2, &ub2);

	*isnull = false;

	if (rtype1 != rtype2)
		elog(ERROR, "range types do not match");

	if ((fl1 & RANGE_EMPTY) && !(fl2 & RANGE_EMPTY))
		return false;
	if (fl2 & RANGE_EMPTY)
		return true;

	cmpFn = get_range_subtype_cmp(rtype1);

	/* if either range has a NULL, then we can only return false or NULL */
	if ((fl1 | fl2) & (RANGE_LB_NULL | RANGE_UB_NULL))
	{
		/* we can return false if r2.lb > r1.ub ... */
		if (!(fl1 & RANGE_UB_NULL) && !(fl2 & RANGE_LB_NULL))
		{
			int cmp = DatumGetInt32(OidFunctionCall2(cmpFn, lb2, ub1));
			/* if they are the same and not both inclusive */
			if (cmp == 0 &&
				!((fl1 & RANGE_UB_INC) && (fl2 & RANGE_LB_INC)))
				return false;
			else if (cmp > 0)
				return false;
		}
		/* ... or if r2.ub < r1.lb */
		if (!(fl1 & RANGE_LB_NULL) && !(fl2 & RANGE_UB_NULL))
		{
			int cmp = DatumGetInt32(OidFunctionCall2(cmpFn, ub2, lb1));
			/* if they are the same and not both inclusive */
			if (cmp == 0 &&
				!((fl1 & RANGE_LB_INC) && (fl2 & RANGE_UB_INC)))
				return false;
			else if (cmp < 0)
				return false;
		}
		*isnull = true;
		return false;
	}

	if ((fl1 & RANGE_LB_INF) && (fl2 & RANGE_LB_INF))
		cmp_lb = 0;
	else if (!(fl1 | RANGE_LB_INF) && (fl2 | RANGE_LB_INF))
		cmp_lb = 1;
	else if ((fl1 | RANGE_LB_INF) && !(fl2 | RANGE_LB_INF))
		cmp_lb = -1;
	else
	{
		cmp_lb = DatumGetInt32(OidFunctionCall2(cmpFn, lb1, lb2));
		if (cmp_lb == 0)
		{
			if ((fl1 & RANGE_LB_INC) && !(fl2 & RANGE_LB_INC))
				cmp_lb = -1;
			else if (!(fl1 & RANGE_LB_INC) && (fl2 & RANGE_LB_INC))
				cmp_lb = 1;
		}
	}

	if ((fl1 & RANGE_UB_INF) && (fl2 & RANGE_UB_INF))
		cmp_ub = 0;
	else if (!(fl1 | RANGE_UB_INF) && (fl2 | RANGE_UB_INF))
		cmp_ub = 1;
	else if ((fl1 | RANGE_UB_INF) && !(fl2 | RANGE_UB_INF))
		cmp_ub = -1;
	else
	{
		cmp_ub = DatumGetInt32(OidFunctionCall2(cmpFn, ub1, ub2));
		if (cmp_ub == 0)
		{
			if ((fl1 & RANGE_UB_INC) && !(fl2 & RANGE_UB_INC))
				cmp_ub = 1;
			else if (!(fl1 & RANGE_UB_INC) && (fl2 & RANGE_UB_INC))
				cmp_ub = -1;
		}
	}

	if (cmp_lb <= 0 && cmp_ub >= 0)
		return true;
	else
		return false;
}
