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

/* flags */
#define RANGE_EMPTY		0x01
#define RANGE_LB_INC	0x02
#define RANGE_LB_NULL	0x04
#define RANGE_LB_INF	0x08
#define RANGE_UB_INC	0x10
#define RANGE_UB_NULL	0x20
#define RANGE_UB_INF	0x40

#define RANGE_HAS_LBOUND(flags) (!(flags & (RANGE_EMPTY |   \
											RANGE_LB_NULL |	\
											RANGE_LB_INF)))

#define RANGE_HAS_UBOUND(flags) (!(flags & (RANGE_EMPTY |	\
											RANGE_UB_NULL |	\
											RANGE_UB_INF)))
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
static bool range_contains_internal(RangeType *r1, RangeType *r2);

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
	Datum		 range;
	char		*lbound_str;
	char		*ubound_str;

	Oid			subtype;
	regproc		subInput;
	FmgrInfo	subInputFn;
	Oid			ioParam;

	RangeBound	*lower = palloc0(sizeof(RangeBound));
	RangeBound	*upper = palloc0(sizeof(RangeBound));

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

	lower->rngtypid	 = rngtypoid;
	lower->isnull	 = flags &	RANGE_LB_NULL;
	lower->infinite	 = flags &	RANGE_LB_INF;
	lower->inclusive = flags &	RANGE_LB_INC;
	lower->lower	 = true;
	upper->rngtypid	 = rngtypoid;
	upper->isnull	 = flags &	RANGE_UB_NULL;
	upper->infinite	 = flags &	RANGE_UB_INF;
	upper->inclusive = flags &	RANGE_UB_INC;
	upper->lower	 = false;

	if (RANGE_HAS_LBOUND(flags))
		lower->val = InputFunctionCall(&subInputFn, lbound_str,
									   ioParam, typmod);
	if (RANGE_HAS_UBOUND(flags))
		upper->val = InputFunctionCall(&subInputFn, ubound_str,
									   ioParam, typmod);

	/* serialize and canonicalize */
	range = make_range(lower, upper, flags & RANGE_EMPTY);

	PG_RETURN_RANGE(range);
}

Datum
range_out(PG_FUNCTION_ARGS)
{
	RangeType *range = PG_GETARG_RANGE(0);

	Oid			subtype;

	regproc		subOutput;
	FmgrInfo	subOutputFn;
	bool		isVarlena;

	char		 flags = 0;
	char		*lbound_str;
	char		*ubound_str;
	char		*output_str;

	bool		empty;
	RangeBound	lower;
	RangeBound	upper;

	/* deserialize */
	range_deserialize(range, &lower, &upper, &empty);

	if (lower.rngtypid != upper.rngtypid)
		elog(ERROR, "range types do not match");

	subtype = get_range_subtype(lower.rngtypid);

	if (empty)
		flags |= RANGE_EMPTY;

	flags |= (lower.inclusive)	? RANGE_LB_INC  : 0;
	flags |= (lower.infinite)	? RANGE_LB_INF  : 0;
	flags |= (lower.isnull)		? RANGE_LB_NULL : 0;
	flags |= (upper.inclusive)	? RANGE_UB_INC  : 0;
	flags |= (upper.infinite)	? RANGE_UB_INF  : 0;
	flags |= (upper.isnull)		? RANGE_UB_NULL : 0;

	/* output */
	getTypeOutputInfo(subtype, &subOutput, &isVarlena);
	fmgr_info(subOutput, &subOutputFn);

	if (RANGE_HAS_LBOUND(flags))
		lbound_str = OutputFunctionCall(&subOutputFn, lower.val);
	if (RANGE_HAS_UBOUND(flags))
		ubound_str = OutputFunctionCall(&subOutputFn, upper.val);

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
	Oid			 subtype  = get_fn_expr_argtype(fcinfo->flinfo,0);
	Oid			 rngtypid = get_range_from_subtype(subtype);
	RangeType	*range;
	RangeBound	*lower	  = palloc0(sizeof(RangeBound));
	RangeBound	*upper	  = palloc0(sizeof(RangeBound));

	if (PG_ARGISNULL(0))
		elog(ERROR, "NULL range boundaries are not supported");

	lower->rngtypid	 = rngtypid;
	lower->inclusive = true;
	lower->val		 = arg;
	lower->lower	 = true;
	upper->rngtypid	 = rngtypid;
	upper->inclusive = true;
	upper->val		 = arg;
	upper->lower	 = false;

	range = DatumGetRangeType(make_range(lower, upper, false));

	PG_RETURN_RANGE(range);
}

Datum
range_linf_(PG_FUNCTION_ARGS)
{
	Datum		 arg	  = PG_GETARG_DATUM(0);
	Oid			 subtype  = get_fn_expr_argtype(fcinfo->flinfo,0);
	Oid			 rngtypid = get_range_from_subtype(subtype);
	RangeType	*range;
	RangeBound	*lower	  = palloc0(sizeof(RangeBound));
	RangeBound	*upper	  = palloc0(sizeof(RangeBound));

	if (PG_ARGISNULL(0))
		elog(ERROR, "NULL range boundaries are not supported");

	lower->rngtypid	 = rngtypid;
	lower->inclusive = false;
	lower->infinite	 = true;
	lower->lower	 = true;

	upper->rngtypid	 = rngtypid;
	upper->inclusive = false;
	upper->val		 = arg;
	upper->lower	 = false;

	range = DatumGetRangeType(make_range(lower, upper, false));

	PG_RETURN_RANGE(range);
}

Datum
range_uinf_(PG_FUNCTION_ARGS)
{
	Datum		 arg	  = PG_GETARG_DATUM(0);
	Oid			 subtype  = get_fn_expr_argtype(fcinfo->flinfo,0);
	Oid			 rngtypid = get_range_from_subtype(subtype);
	RangeType	*range;
	RangeBound	*lower	  = palloc0(sizeof(RangeBound));
	RangeBound	*upper	  = palloc0(sizeof(RangeBound));

	if (PG_ARGISNULL(0))
		elog(ERROR, "NULL range boundaries are not supported");

	lower->rngtypid	 = rngtypid;
	lower->inclusive = false;
	lower->val		 = arg;
	lower->lower	 = true;

	upper->rngtypid	 = rngtypid;
	upper->inclusive = false;
	upper->infinite	 = true;
	upper->lower	 = false;

	range = DatumGetRangeType(make_range(lower, upper, false));

	PG_RETURN_RANGE(range);
}

Datum
range_linfi(PG_FUNCTION_ARGS)
{
	Datum		 arg	  = PG_GETARG_DATUM(0);
	Oid			 subtype  = get_fn_expr_argtype(fcinfo->flinfo,0);
	Oid			 rngtypid = get_range_from_subtype(subtype);
	RangeType	*range;
	RangeBound	*lower	  = palloc0(sizeof(RangeBound));
	RangeBound	*upper	  = palloc0(sizeof(RangeBound));

	if (PG_ARGISNULL(0))
		elog(ERROR, "NULL range boundaries are not supported");

	lower->rngtypid	 = rngtypid;
	lower->inclusive = false;
	lower->infinite	 = true;
	lower->lower	 = true;

	upper->rngtypid	 = rngtypid;
	upper->inclusive = true;
	upper->val		 = arg;
	upper->lower	 = false;

	range = DatumGetRangeType(make_range(lower, upper, false));

	PG_RETURN_RANGE(range);
}

Datum
range_uinfi(PG_FUNCTION_ARGS)
{
	Datum		 arg	  = PG_GETARG_DATUM(0);
	Oid			 subtype  = get_fn_expr_argtype(fcinfo->flinfo,0);
	Oid			 rngtypid = get_range_from_subtype(subtype);
	RangeType	*range;
	RangeBound	*lower	  = palloc0(sizeof(RangeBound));
	RangeBound	*upper	  = palloc0(sizeof(RangeBound));

	if (PG_ARGISNULL(0))
		elog(ERROR, "NULL range boundaries are not supported");

	lower->rngtypid	 = rngtypid;
	lower->inclusive = true;
	lower->val		 = arg;
	lower->lower	 = true;
	upper->rngtypid	 = rngtypid;
	upper->inclusive = false;
	upper->infinite	 = true;
	upper->lower	 = false;

	range = DatumGetRangeType(make_range(lower, upper, false));

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
range_lower(PG_FUNCTION_ARGS)
{
	RangeType	*r1 = PG_GETARG_RANGE(0);
	RangeBound	 lower;
	RangeBound	 upper;
	bool		 empty;

	range_deserialize(r1, &lower, &upper, &empty);

	if (empty)
		elog(ERROR, "range is empty");
	if (lower.infinite)
		elog(ERROR, "range lower bound is infinite");

	Assert(!lower.isnull);

	PG_RETURN_DATUM(lower.val);
}

Datum
range_upper(PG_FUNCTION_ARGS)
{
	RangeType	*r1 = PG_GETARG_RANGE(0);
	RangeBound	 lower;
	RangeBound	 upper;
	bool		 empty;

	range_deserialize(r1, &lower, &upper, &empty);

	if (empty)
		elog(ERROR, "range is empty");
	if (upper.infinite)
		elog(ERROR, "range lower bound is infinite");

	Assert(!upper.isnull);

	PG_RETURN_DATUM(upper.val);
}


/* range -> bool */
Datum
range_empty(PG_FUNCTION_ARGS)
{
	RangeType	*r1 = PG_GETARG_RANGE(0);
	RangeBound	 lower;
	RangeBound	 upper;
	bool		 empty;

	range_deserialize(r1, &lower, &upper, &empty);

	PG_RETURN_BOOL(empty);
}

Datum
range_lower_inc(PG_FUNCTION_ARGS)
{
	RangeType	*r1 = PG_GETARG_RANGE(0);
	RangeBound	 lower;
	RangeBound	 upper;
	bool		 empty;

	range_deserialize(r1, &lower, &upper, &empty);

	PG_RETURN_BOOL(lower.inclusive);
}

Datum
range_upper_inc(PG_FUNCTION_ARGS)
{
	RangeType	*r1 = PG_GETARG_RANGE(0);
	RangeBound	 lower;
	RangeBound	 upper;
	bool		 empty;

	range_deserialize(r1, &lower, &upper, &empty);

	PG_RETURN_BOOL(upper.inclusive);
}

Datum
range_lower_inf(PG_FUNCTION_ARGS)
{
	RangeType	*r1 = PG_GETARG_RANGE(0);
	RangeBound	 lower;
	RangeBound	 upper;
	bool		 empty;

	range_deserialize(r1, &lower, &upper, &empty);

	PG_RETURN_BOOL(lower.infinite);
}

Datum
range_upper_inf(PG_FUNCTION_ARGS)
{
	RangeType	*r1 = PG_GETARG_RANGE(0);
	RangeBound	 lower;
	RangeBound	 upper;
	bool		 empty;

	range_deserialize(r1, &lower, &upper, &empty);

	PG_RETURN_BOOL(upper.infinite);
}


/* range, range -> bool */
Datum
range_eq(PG_FUNCTION_ARGS)
{
	RangeType *r1 = PG_GETARG_RANGE(0);
	RangeType *r2 = PG_GETARG_RANGE(1);

	RangeBound	lower1, lower2;
	RangeBound	upper1, upper2;
	bool		empty1, empty2;

	bool		isnull;

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (lower1.rngtypid != upper1.rngtypid ||
		lower1.rngtypid != lower2.rngtypid ||
		lower1.rngtypid != upper2.rngtypid)
		elog(ERROR, "range types do not match");

	if (empty1 && empty2)
		PG_RETURN_BOOL(true);
	if (empty1 != empty2)
		PG_RETURN_BOOL(false);

	if (range_cmp_bounds(&lower1, &lower2, &isnull) != 0)
		PG_RETURN_BOOL(false);

	Assert(!isnull);

	if (range_cmp_bounds(&upper1, &upper2, &isnull) != 0)
		PG_RETURN_BOOL(false);

	Assert(!isnull);

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
	Oid			 rngtypid = get_fn_expr_argtype(fcinfo->flinfo,0);
	RangeBound	 lower;
	RangeBound	 upper;

	lower.rngtypid	= rngtypid;
	lower.inclusive = true;
	lower.infinite	= false;
	lower.isnull	= false;
	lower.lower		= true;
	lower.val		= val;

	upper.rngtypid	= rngtypid;
	upper.inclusive = true;
	upper.infinite	= false;
	upper.isnull	= false;
	upper.lower		= false;
	upper.val		= val;

	r2 = DatumGetRangeType(make_range(&lower, &upper, false));

	PG_RETURN_BOOL(range_contains_internal(r1, r2));
}

Datum
range_contains(PG_FUNCTION_ARGS)
{
	RangeType	*r1 = PG_GETARG_RANGE(0);
	RangeType	*r2 = PG_GETARG_RANGE(1);

	PG_RETURN_BOOL(range_contains_internal(r1, r2));
}

Datum
range_contained_by(PG_FUNCTION_ARGS)
{
	RangeType	*r1 = PG_GETARG_RANGE(0);
	RangeType	*r2 = PG_GETARG_RANGE(1);

	PG_RETURN_BOOL(range_contains_internal(r1, r2));
}

Datum
range_before(PG_FUNCTION_ARGS)
{
	RangeType *r1 = PG_GETARG_RANGE(0);
	RangeType *r2 = PG_GETARG_RANGE(1);

	RangeBound	lower1, lower2;
	RangeBound	upper1, upper2;
	bool		empty1, empty2;

	bool		isnull;

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (lower1.rngtypid != upper1.rngtypid ||
		lower1.rngtypid != lower2.rngtypid ||
		lower1.rngtypid != upper2.rngtypid)
		elog(ERROR, "range types do not match");

	if (empty1 || empty2)
		elog(ERROR, "empty range");

	if (range_cmp_bounds(&upper1, &lower2, &isnull) < 0)
		PG_RETURN_BOOL(true);
	else
		PG_RETURN_BOOL(false);
}

Datum
range_after(PG_FUNCTION_ARGS)
{
	RangeType *r1 = PG_GETARG_RANGE(0);
	RangeType *r2 = PG_GETARG_RANGE(1);

	RangeBound	lower1, lower2;
	RangeBound	upper1, upper2;
	bool		empty1, empty2;

	bool		isnull;

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (lower1.rngtypid != upper1.rngtypid ||
		lower1.rngtypid != lower2.rngtypid ||
		lower1.rngtypid != upper2.rngtypid)
		elog(ERROR, "range types do not match");

	if (empty1 || empty2)
		elog(ERROR, "empty range");

	if (range_cmp_bounds(&lower1, &upper2, &isnull) > 0)
		PG_RETURN_BOOL(true);
	else
		PG_RETURN_BOOL(false);
}

Datum range_adjacent(PG_FUNCTION_ARGS)
{
	RangeType *r1 = PG_GETARG_RANGE(0);
	RangeType *r2 = PG_GETARG_RANGE(1);

	RangeBound	lower1, lower2;
	RangeBound	upper1, upper2;
	bool		empty1, empty2;

	Oid			cmpFn;

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (lower1.rngtypid != upper1.rngtypid ||
		lower1.rngtypid != lower2.rngtypid ||
		lower1.rngtypid != upper2.rngtypid)
		elog(ERROR, "range types do not match");

	if (empty1 || empty2)
		elog(ERROR, "empty range");

	/*
	 * For two ranges to be adjacent, the lower boundary of one range
	 * has to match the upper boundary of the other. However, the
	 * inclusivity of those two boundaries must also be different.
	 *
	 * The semantics for range_cmp_bounds aren't quite what we need
	 * here, so we do the comparison more directly.
	 */

	cmpFn = get_range_subtype_cmp(lower1.rngtypid);

	if (lower1.inclusive != upper2.inclusive)
	{
		if (DatumGetInt32(OidFunctionCall2(cmpFn, lower1.val, upper2.val)) == 0)
			PG_RETURN_BOOL(true);
	}

	if (upper1.inclusive != lower2.inclusive)
	{
		if (DatumGetInt32(OidFunctionCall2(cmpFn, upper1.val, lower2.val)) == 0)
			PG_RETURN_BOOL(true);
	}

	PG_RETURN_BOOL(false);
}

Datum
range_overlaps(PG_FUNCTION_ARGS)
{
	RangeType *r1 = PG_GETARG_RANGE(0);
	RangeType *r2 = PG_GETARG_RANGE(1);

	RangeBound	lower1, lower2;
	RangeBound	upper1, upper2;
	bool		empty1, empty2;

	bool		isnull;

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (lower1.rngtypid != upper1.rngtypid ||
		lower1.rngtypid != lower2.rngtypid ||
		lower1.rngtypid != upper2.rngtypid)
		elog(ERROR, "range types do not match");

	if (empty1 || empty2)
		PG_RETURN_BOOL(false);

	if (range_cmp_bounds(&lower1, &lower2, &isnull) >= 0 &&
		range_cmp_bounds(&lower1, &upper2, &isnull) <= 0)
		PG_RETURN_BOOL(true);

	if (range_cmp_bounds(&lower2, &lower1, &isnull) >= 0 &&
		range_cmp_bounds(&lower2, &upper1, &isnull) <= 0)
		PG_RETURN_BOOL(true);

	PG_RETURN_BOOL(false);
}

Datum
range_overleft(PG_FUNCTION_ARGS)
{
	RangeType *r1 = PG_GETARG_RANGE(0);
	RangeType *r2 = PG_GETARG_RANGE(1);

	RangeBound	lower1, lower2;
	RangeBound	upper1, upper2;
	bool		empty1, empty2;

	bool		isnull;

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (lower1.rngtypid != upper1.rngtypid ||
		lower1.rngtypid != lower2.rngtypid ||
		lower1.rngtypid != upper2.rngtypid)
		elog(ERROR, "range types do not match");

	if (empty1 || empty2)
		PG_RETURN_BOOL(false);

	if (range_cmp_bounds(&upper1, &upper2, &isnull) <= 0)
		PG_RETURN_BOOL(true);

	PG_RETURN_BOOL(false);
}

Datum
range_overright(PG_FUNCTION_ARGS)
{
	RangeType *r1 = PG_GETARG_RANGE(0);
	RangeType *r2 = PG_GETARG_RANGE(1);

	RangeBound	lower1, lower2;
	RangeBound	upper1, upper2;
	bool		empty1, empty2;

	bool		isnull;

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (lower1.rngtypid != upper1.rngtypid ||
		lower1.rngtypid != lower2.rngtypid ||
		lower1.rngtypid != upper2.rngtypid)
		elog(ERROR, "range types do not match");

	if (empty1 || empty2)
		PG_RETURN_BOOL(false);

	if (range_cmp_bounds(&lower1, &lower2, &isnull) >= 0)
		PG_RETURN_BOOL(true);

	PG_RETURN_BOOL(false);
}


/* range, range -> range */
Datum
range_minus(PG_FUNCTION_ARGS)
{
	RangeType *r1 = PG_GETARG_RANGE(0);
	RangeType *r2 = PG_GETARG_RANGE(1);

	RangeBound	lower1, lower2;
	RangeBound	upper1, upper2;
	bool		empty1, empty2;

	bool		isnull;

	int cmp_l1l2, cmp_l1u2, cmp_u1l2, cmp_u1u2;

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (lower1.rngtypid != upper1.rngtypid ||
		lower1.rngtypid != lower2.rngtypid ||
		lower1.rngtypid != upper2.rngtypid)
		elog(ERROR, "range types do not match");

	if (empty1 || empty2)
		PG_RETURN_RANGE(r1);

	cmp_l1l2 = range_cmp_bounds(&lower1, &lower2, &isnull);
	Assert(!isnull);
	cmp_l1u2 = range_cmp_bounds(&lower1, &upper2, &isnull);
	Assert(!isnull);
	cmp_u1l2 = range_cmp_bounds(&upper1, &lower2, &isnull);
	Assert(!isnull);
	cmp_u1u2 = range_cmp_bounds(&upper1, &upper2, &isnull);
	Assert(!isnull);

	if (cmp_l1l2 < 0 && cmp_u1u2 > 0)
		elog(ERROR, "range_minus resulted in two ranges");

	if (cmp_l1u2 > 0 || cmp_u1l2 < 0)
		PG_RETURN_RANGE(r1);

	if (cmp_l1l2 >= 0 && cmp_u1u2 <= 0)
		PG_RETURN_RANGE(make_empty_range(lower1.rngtypid));

	if (cmp_l1l2 <= 0 && cmp_u1l2 >= 0 && cmp_u1u2 <= 0)
	{
		lower2.inclusive = !lower2.inclusive;
		lower2.lower = false; /* it will become the upper bound */
		PG_RETURN_RANGE(make_range(&lower1, &lower2, false));
	}

	if (cmp_l1l2 >= 0 && cmp_u1u2 >= 0 && cmp_l1u2 <= 0)
	{
		upper2.inclusive = !upper2.inclusive;
		upper2.lower = true; /* it will become the lower bound */
		PG_RETURN_RANGE(make_range(&upper2, &upper1, false));
	}

	elog(ERROR, "unexpected error in range_minus");
	PG_RETURN_VOID();
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
range_serialize(RangeBound *lower, RangeBound *upper, bool empty)
{
	Datum		 range;
	size_t		 msize;
	char		*ptr;
	Oid			 subtype  = get_range_subtype(lower->rngtypid);
	int16		 typlen	  = get_typlen(subtype);
	char		 typalign = get_typalign(subtype);
	size_t		 llen;
	size_t		 ulen;
	char		 flags = 0;

	if (lower->rngtypid != upper->rngtypid)
		elog(ERROR, "range types do not match");

	if (lower->isnull || upper->isnull)
		elog(ERROR, "NULL range boundaries are not supported");

	if (empty)
		flags |= RANGE_EMPTY;

	flags |= (lower->inclusive) ? RANGE_LB_INC  : 0;
	flags |= (lower->infinite)  ? RANGE_LB_INF  : 0;
	flags |= (lower->isnull)	? RANGE_LB_NULL : 0;
	flags |= (upper->inclusive) ? RANGE_UB_INC  : 0;
	flags |= (upper->infinite)  ? RANGE_UB_INF  : 0;
	flags |= (upper->isnull)	? RANGE_UB_NULL : 0;

	msize  = VARHDRSZ;
	msize += sizeof(Oid);
	msize += sizeof(char);

	if (RANGE_HAS_LBOUND(flags))
	{
		llen = att_addlength_datum(0, typlen, lower->val);
		msize = att_align_nominal(msize, typalign);
		msize += llen;
	}

	if (RANGE_HAS_UBOUND(flags))
	{
		ulen = att_addlength_datum(0, typlen, upper->val);
		msize = att_align_nominal(msize, typalign);
		msize += ulen;
	}

	ptr = palloc(msize);
	range = (Datum) ptr;

	ptr += VARHDRSZ;

	memcpy(ptr, &lower->rngtypid, sizeof(Oid));
	ptr += sizeof(Oid);
	memcpy(ptr, &flags, sizeof(char));
	ptr += sizeof(char);

	if (RANGE_HAS_LBOUND(flags))
	{
		Assert(lower->lower);
		ptr = (char *) att_align_nominal(ptr, typalign);
		memcpy(ptr, (void *) lower->val, llen);
		ptr += llen;
	}

	if (RANGE_HAS_UBOUND(flags))
	{
		Assert(!upper->lower);
		ptr = (char *) att_align_nominal(ptr, typalign);
		memcpy(ptr, (void *) upper->val, ulen);
		ptr += ulen;
	}

	SET_VARSIZE(range, msize);
	PG_RETURN_RANGE(range);
}

void
range_deserialize(RangeType *range, RangeBound *lower, RangeBound *upper,
				  bool *empty)
{
	char		*ptr = VARDATA(range);
	int			 llen;
	int			 ulen;
	char		 typalign;
	int16		 typlen;
	char		 flags;
	Oid			 rngtypid;
	Datum		 lbound;
	Datum		 ubound;

	memset(lower, 0, sizeof(RangeBound));
	memset(upper, 0, sizeof(RangeBound));

	memcpy(&rngtypid, ptr, sizeof(Oid));
	ptr += sizeof(Oid);
	memcpy(&flags, ptr, sizeof(char));
	ptr += sizeof(char);

	if (rngtypid == ANYRANGEOID)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot output a value of type anyrange")));

	typalign = get_typalign(rngtypid);
	typlen	 = get_typlen(rngtypid);

	if (RANGE_HAS_LBOUND(flags))
	{
		ptr = (char *) att_align_nominal(ptr, typalign);
		llen = att_addlength_datum(0, typlen, (Datum) ptr);
		lbound = (Datum) palloc(llen);
		memcpy((void *) lbound, ptr, llen);
		ptr += llen;
	}
	else
		lbound = (Datum) 0;

	if (RANGE_HAS_UBOUND(flags))
	{
		ptr = (char *) att_align_nominal(ptr, typalign);
		ulen = att_addlength_datum(0, typlen, (Datum) ptr);
		ubound = (Datum) palloc(ulen);
		memcpy((void *) ubound, ptr, ulen);
		ptr += ulen;
	}
	else
		ubound = (Datum) 0;

	*empty = flags & RANGE_EMPTY;

	lower->rngtypid	 = rngtypid;
	lower->val		 = lbound;
	lower->inclusive = flags &	RANGE_LB_INC;
	lower->infinite	 = flags &	RANGE_LB_INF;
	lower->isnull	 = flags &	RANGE_LB_NULL;
	lower->lower	 = true;

	upper->rngtypid	 = rngtypid;
	upper->val		 = ubound;;
	upper->inclusive = flags &	RANGE_UB_INC;
	upper->infinite	 = flags &	RANGE_UB_INF;
	upper->isnull	 = flags &	RANGE_UB_NULL;
	upper->lower	 = false;
}

/*
 * This both serializes and caonicalizes (if applicable) the
 * range. This should be used by most callers.
 */
Datum
make_range(RangeBound *lower, RangeBound *upper, bool empty)
{
	Datum range;
	Oid canonical = get_range_canonical(lower->rngtypid);

	if (lower->rngtypid != upper->rngtypid)
		elog(ERROR, "range types do not match");

	range = range_serialize(lower, upper, empty);

	if (OidIsValid(canonical))
		range = OidFunctionCall1(canonical, range);

	PG_RETURN_RANGE(range);
}

int
range_cmp_bounds(RangeBound *b1, RangeBound *b2, bool *isnull)
{
	regproc		cmpFn;
	int			result;

	if(b1->isnull || b2->isnull)
		elog(ERROR, "NULL range boundaries are not supported");

	*isnull = false;

	if (b1->infinite && b2->infinite)
	{
		if (b1->lower == b2->lower)
			return 0;
		else
			return (b1->lower) ? -1 : 1;
	}
	else if (b1->infinite && !b2->infinite)
		return (b1->lower) ? -1 : 1;
	else if (!b1->infinite && b2->infinite)
		return (b2->lower) ? 1 : -1;

	cmpFn = get_range_subtype_cmp(b1->rngtypid);
	result = DatumGetInt32(OidFunctionCall2(cmpFn, b1->val, b2->val));

	if (result == 0)
	{
		if (b1->inclusive && !b2->inclusive)
			return (b2->lower) ? -1 : 1;
		else if (!b1->inclusive && b2->inclusive)
			return (b1->lower) ? 1 : -1;
	}

	return result;
}

RangeType *
make_empty_range(Oid rngtypid)
{
	RangeBound lower;
	RangeBound upper;

	memset(&lower, 0, sizeof(RangeBound));
	memset(&upper, 0, sizeof(RangeBound));

	lower.rngtypid = rngtypid;
	lower.lower = true;
	upper.rngtypid = rngtypid;
	upper.lower = false;

	return DatumGetRangeType(make_range(&lower, &upper, true));
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
	Oid			 subtype  = get_fn_expr_argtype(fcinfo->flinfo,0);
	Oid			 rngtypid = get_range_from_subtype(subtype);
	RangeType	*range;
	RangeBound	 lower;
	RangeBound	 upper;

	memset(&lower, 0, sizeof(RangeBound));
	memset(&upper, 0, sizeof(RangeBound));

	switch(fcinfo->flinfo->fn_oid)
	{
		case F_RANGE__:
			break;
		case F_RANGE_I:
			upper.inclusive = true;
			break;
		case F_RANGE:
		case F_RANGEI_:
			lower.inclusive = true;
			break;
		case F_RANGEII:
			lower.inclusive = true;
			upper.inclusive = true;
			break;
	}

	if (PG_ARGISNULL(0) || PG_ARGISNULL(1))
		elog(ERROR, "NULL range boundaries are not supported");

	lower.rngtypid = rngtypid;
	lower.lower	   = true;
	lower.val	   = arg1;

	upper.rngtypid = rngtypid;
	upper.lower	   = false;
	upper.val	   = arg2;

	range = DatumGetRangeType(make_range(&lower, &upper, false));

	PG_RETURN_RANGE(range);
}

static bool
range_contains_internal(RangeType *r1, RangeType *r2)
{
	RangeBound	lower1;
	RangeBound	upper1;
	bool		empty1;
	RangeBound	lower2;
	RangeBound	upper2;
	bool		empty2;

	bool		isnull;

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (lower1.rngtypid != upper1.rngtypid ||
		lower1.rngtypid != lower2.rngtypid ||
		lower1.rngtypid != upper2.rngtypid)
		elog(ERROR, "range types do not match");

	if (empty1 && !empty2)
		return false;
	else if (empty2)
		return true;

	if (range_cmp_bounds(&lower1, &lower2, &isnull) > 0)
		return false;
	if (range_cmp_bounds(&upper1, &upper2, &isnull) < 0)
		return false;

	return true;
}
