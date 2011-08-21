/*-------------------------------------------------------------------------
 *
 * rangetypes.c
 *	  I/O functions, operators, and support functions for range types
 *
 * Copyright (c) 2006-2011, PostgreSQL Global Development Group
 *
 *
 * IDENTIFICATION
 *	  src/backend/utils/adt/rangetypes.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/hash.h"
#include "catalog/pg_range.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "utils/builtins.h"
#include "utils/date.h"
#include "utils/fmgroids.h"
#include "utils/lsyscache.h"
#include "utils/rangetypes.h"
#include "utils/typcache.h"

#define TYPE_IS_PACKABLE(typlen, typstorage) \
	(typlen == -1 && typstorage != 'p')

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
	RANGE_PSTATE_PRE_LB,
	RANGE_PSTATE_LB,
	RANGE_PSTATE_SEP,
	RANGE_PSTATE_PRE_UB,
	RANGE_PSTATE_UB,
	RANGE_PSTATE_UB_INC,
	RANGE_PSTATE_DONE
} RangePState;

static void range_parse(const char *input_str,  char *flags,
						   char **lbound_str, char **ubound_str);
static char *range_deparse(char flags, char *lbound_str, char *ubound_str);
static Datum range_make2(PG_FUNCTION_ARGS);
static bool range_contains_internal(RangeType *r1, RangeType *r2);
static Size datum_compute_size(Size sz, Datum datum, bool typbyval,
							   char typalign, int16 typlen, char typstorage);
static Pointer datum_write(Pointer ptr, Datum datum, bool typbyval,
						   char typalign, int16 typlen, char typstorage);

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

	RangeBound	lower;
	RangeBound	upper;

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

	lower.rngtypid	= rngtypoid;
	lower.infinite	= flags &	RANGE_LB_INF;
	lower.inclusive = flags &	RANGE_LB_INC;
	lower.lower		= true;
	upper.rngtypid	= rngtypoid;
	upper.infinite	= flags &	RANGE_UB_INF;
	upper.inclusive = flags &	RANGE_UB_INC;
	upper.lower		= false;

	if (RANGE_HAS_LBOUND(flags))
		lower.val = InputFunctionCall(&subInputFn, lbound_str,
									   ioParam, typmod);
	if (RANGE_HAS_UBOUND(flags))
		upper.val = InputFunctionCall(&subInputFn, ubound_str,
									   ioParam, typmod);

	/* serialize and canonicalize */
	range = make_range(&lower, &upper, flags & RANGE_EMPTY);

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
	char		*lbound_str = NULL;
	char		*ubound_str = NULL;
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
	flags |= (upper.inclusive)	? RANGE_UB_INC  : 0;
	flags |= (upper.infinite)	? RANGE_UB_INF  : 0;

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
	RangeBound	 lower;
	RangeBound	 upper;

	if (PG_ARGISNULL(0))
		elog(ERROR, "NULL range boundaries are not supported");

	if (!OidIsValid(subtype) || !OidIsValid(rngtypid))
		elog(ERROR, "cannot determine argument types");

	lower.rngtypid	= rngtypid;
	lower.inclusive = true;
	lower.val		= arg;
	lower.lower		= true;
	lower.infinite	= false;
	upper.rngtypid	= rngtypid;
	upper.inclusive = true;
	upper.val		= arg;
	upper.lower		= false;
	upper.infinite	= false;

	range = DatumGetRangeType(make_range(&lower, &upper, false));

	PG_RETURN_RANGE(range);
}

Datum
range_linf_(PG_FUNCTION_ARGS)
{
	Datum		 arg	  = PG_GETARG_DATUM(0);
	Oid			 subtype  = get_fn_expr_argtype(fcinfo->flinfo,0);
	Oid			 rngtypid = get_range_from_subtype(subtype);
	RangeType	*range;
	RangeBound	 lower;
	RangeBound	 upper;

	if (PG_ARGISNULL(0))
		elog(ERROR, "NULL range boundaries are not supported");

	lower.rngtypid	= rngtypid;
	lower.inclusive = false;
	lower.infinite	= true;
	lower.lower		= true;
	lower.val		= (Datum) 0;

	upper.rngtypid	= rngtypid;
	upper.inclusive = false;
	upper.infinite	= false;
	upper.lower		= false;
	upper.val		= arg;

	range = DatumGetRangeType(make_range(&lower, &upper, false));

	PG_RETURN_RANGE(range);
}

Datum
range_uinf_(PG_FUNCTION_ARGS)
{
	Datum		 arg	  = PG_GETARG_DATUM(0);
	Oid			 subtype  = get_fn_expr_argtype(fcinfo->flinfo,0);
	Oid			 rngtypid = get_range_from_subtype(subtype);
	RangeType	*range;
	RangeBound	 lower;
	RangeBound	 upper;

	if (PG_ARGISNULL(0))
		elog(ERROR, "NULL range boundaries are not supported");

	lower.rngtypid	= rngtypid;
	lower.inclusive = false;
	lower.infinite	= false;
	lower.lower		= true;
	lower.val		= arg;

	upper.rngtypid	= rngtypid;
	upper.inclusive = false;
	upper.infinite	= true;
	upper.lower		= false;
	upper.val		= (Datum) 0;

	range = DatumGetRangeType(make_range(&lower, &upper, false));

	PG_RETURN_RANGE(range);
}

Datum
range_linfi(PG_FUNCTION_ARGS)
{
	Datum		 arg	  = PG_GETARG_DATUM(0);
	Oid			 subtype  = get_fn_expr_argtype(fcinfo->flinfo,0);
	Oid			 rngtypid = get_range_from_subtype(subtype);
	RangeType	*range;
	RangeBound	 lower;
	RangeBound	 upper;

	if (PG_ARGISNULL(0))
		elog(ERROR, "NULL range boundaries are not supported");

	lower.rngtypid	= rngtypid;
	lower.inclusive = false;
	lower.infinite	= true;
	lower.lower		= true;
	lower.val		= (Datum) 0;

	upper.rngtypid	= rngtypid;
	upper.inclusive = true;
	upper.infinite	= false;
	upper.lower		= false;
	upper.val		= arg;

	range = DatumGetRangeType(make_range(&lower, &upper, false));

	PG_RETURN_RANGE(range);
}

Datum
range_uinfi(PG_FUNCTION_ARGS)
{
	Datum		 arg	  = PG_GETARG_DATUM(0);
	Oid			 subtype  = get_fn_expr_argtype(fcinfo->flinfo,0);
	Oid			 rngtypid = get_range_from_subtype(subtype);
	RangeType	*range;
	RangeBound	 lower;
	RangeBound	 upper;

	if (PG_ARGISNULL(0))
		elog(ERROR, "NULL range boundaries are not supported");

	lower.rngtypid	= rngtypid;
	lower.inclusive = true;
	lower.infinite	= false;
	lower.lower		= true;
	lower.val		= arg;

	upper.rngtypid	= rngtypid;
	upper.inclusive = false;
	upper.infinite	= true;
	upper.lower		= false;
	upper.val		= (Datum) 0;

	range = DatumGetRangeType(make_range(&lower, &upper, false));

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
range_non_empty(PG_FUNCTION_ARGS)
{
	RangeType	*r1 = PG_GETARG_RANGE(0);
	RangeBound	 lower;
	RangeBound	 upper;
	bool		 empty;

	range_deserialize(r1, &lower, &upper, &empty);

	PG_RETURN_BOOL(!empty);
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

	if (range_cmp_bounds(&lower1, &lower2) != 0)
		PG_RETURN_BOOL(false);

	if (range_cmp_bounds(&upper1, &upper2) != 0)
		PG_RETURN_BOOL(false);

	PG_RETURN_BOOL(true);
}

Datum
range_ne(PG_FUNCTION_ARGS)
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

	RangeBound	lower1, lower2;
	RangeBound	upper1, upper2;
	bool		empty1;

	range_deserialize(r1, &lower1, &upper1, &empty1);

	lower2.rngtypid	 = lower1.rngtypid;
	lower2.inclusive = true;
	lower2.infinite	 = false;
	lower2.lower	 = true;
	lower2.val		 = val;

	upper2.rngtypid	 = lower1.rngtypid;
	upper2.inclusive = true;
	upper2.infinite	 = false;
	upper2.lower	 = false;
	upper2.val		 = val;

	r2 = DatumGetRangeType(make_range(&lower2, &upper2, false));

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
elem_contained_by_range(PG_FUNCTION_ARGS)
{
	RangeType	*r1		  = PG_GETARG_RANGE(1);
	RangeType	*r2;
	Datum		 val	  = PG_GETARG_DATUM(0);

	RangeBound	lower1, lower2;
	RangeBound	upper1, upper2;
	bool		empty1;

	range_deserialize(r1, &lower1, &upper1, &empty1);

	lower2.rngtypid	 = lower1.rngtypid;
	lower2.inclusive = true;
	lower2.infinite	 = false;
	lower2.lower	 = true;
	lower2.val		 = val;

	upper2.rngtypid	 = lower1.rngtypid;
	upper2.inclusive = true;
	upper2.infinite	 = false;
	upper2.lower	 = false;
	upper2.val		 = val;

	r2 = DatumGetRangeType(make_range(&lower2, &upper2, false));

	PG_RETURN_BOOL(range_contains_internal(r1, r2));
}

Datum
range_contained_by(PG_FUNCTION_ARGS)
{
	RangeType	*r1 = PG_GETARG_RANGE(0);
	RangeType	*r2 = PG_GETARG_RANGE(1);

	PG_RETURN_BOOL(range_contains_internal(r2, r1));
}

Datum
range_before(PG_FUNCTION_ARGS)
{
	RangeType *r1 = PG_GETARG_RANGE(0);
	RangeType *r2 = PG_GETARG_RANGE(1);

	RangeBound	lower1, lower2;
	RangeBound	upper1, upper2;
	bool		empty1, empty2;

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (lower1.rngtypid != upper1.rngtypid ||
		lower1.rngtypid != lower2.rngtypid ||
		lower1.rngtypid != upper2.rngtypid)
		elog(ERROR, "range types do not match");

	if (empty1 || empty2)
		elog(ERROR, "empty range");

	if (range_cmp_bounds(&upper1, &lower2) < 0)
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

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (lower1.rngtypid != upper1.rngtypid ||
		lower1.rngtypid != lower2.rngtypid ||
		lower1.rngtypid != upper2.rngtypid)
		elog(ERROR, "range types do not match");

	if (empty1 || empty2)
		elog(ERROR, "empty range");

	if (range_cmp_bounds(&lower1, &upper2) > 0)
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

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (lower1.rngtypid != upper1.rngtypid ||
		lower1.rngtypid != lower2.rngtypid ||
		lower1.rngtypid != upper2.rngtypid)
		elog(ERROR, "range types do not match");

	if (empty1 || empty2)
		PG_RETURN_BOOL(false);

	if (range_cmp_bounds(&lower1, &lower2) >= 0 &&
		range_cmp_bounds(&lower1, &upper2) <= 0)
		PG_RETURN_BOOL(true);

	if (range_cmp_bounds(&lower2, &lower1) >= 0 &&
		range_cmp_bounds(&lower2, &upper1) <= 0)
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

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (lower1.rngtypid != upper1.rngtypid ||
		lower1.rngtypid != lower2.rngtypid ||
		lower1.rngtypid != upper2.rngtypid)
		elog(ERROR, "range types do not match");

	if (empty1 || empty2)
		PG_RETURN_BOOL(false);

	if (range_cmp_bounds(&upper1, &upper2) <= 0)
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

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (lower1.rngtypid != upper1.rngtypid ||
		lower1.rngtypid != lower2.rngtypid ||
		lower1.rngtypid != upper2.rngtypid)
		elog(ERROR, "range types do not match");

	if (empty1 || empty2)
		PG_RETURN_BOOL(false);

	if (range_cmp_bounds(&lower1, &lower2) >= 0)
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

	int cmp_l1l2, cmp_l1u2, cmp_u1l2, cmp_u1u2;

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (lower1.rngtypid != upper1.rngtypid ||
		lower1.rngtypid != lower2.rngtypid ||
		lower1.rngtypid != upper2.rngtypid)
		elog(ERROR, "range types do not match");

	if (empty1 || empty2)
		PG_RETURN_RANGE(r1);

	cmp_l1l2 = range_cmp_bounds(&lower1, &lower2);
	cmp_l1u2 = range_cmp_bounds(&lower1, &upper2);
	cmp_u1l2 = range_cmp_bounds(&upper1, &lower2);
	cmp_u1u2 = range_cmp_bounds(&upper1, &upper2);

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
	RangeType *r1 = PG_GETARG_RANGE(0);
	RangeType *r2 = PG_GETARG_RANGE(1);

	RangeBound	 lower1, lower2;
	RangeBound	 upper1, upper2;
	bool		 empty1, empty2;
	RangeBound	*result_lower;
	RangeBound	*result_upper;

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (empty1)
		PG_RETURN_RANGE(r2);
	if (empty2)
		PG_RETURN_RANGE(r1);

	if (!DatumGetBool(range_overlaps(fcinfo)) &&
		!DatumGetBool(range_adjacent(fcinfo)))
		elog(ERROR, "range union resulted in two ranges");

	if (range_cmp_bounds(&lower1, &lower2) < 0)
		result_lower = &lower1;
	else
		result_lower = &lower2;

	if (range_cmp_bounds(&upper1, &upper2) > 0)
		result_upper = &upper1;
	else
		result_upper = &upper2;

	PG_RETURN_RANGE(make_range(result_lower, result_upper, false));
}

Datum
range_intersect(PG_FUNCTION_ARGS)
{
	RangeType *r1 = PG_GETARG_RANGE(0);
	RangeType *r2 = PG_GETARG_RANGE(1);

	RangeBound	 lower1, lower2;
	RangeBound	 upper1, upper2;
	bool		 empty1, empty2;
	RangeBound	*result_lower;
	RangeBound	*result_upper;

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (empty1 || empty2 || !DatumGetBool(range_overlaps(fcinfo)))
		PG_RETURN_RANGE(make_empty_range(lower1.rngtypid));

	if (range_cmp_bounds(&lower1, &lower2) >= 0)
		result_lower = &lower1;
	else
		result_lower = &lower2;

	if (range_cmp_bounds(&upper1, &upper2) <= 0)
		result_upper = &upper1;
	else
		result_upper = &upper2;

	PG_RETURN_RANGE(make_range(result_lower, result_upper, false));
}

/* Btree support */

Datum
range_cmp(PG_FUNCTION_ARGS)
{
	RangeType *r1 = PG_GETARG_RANGE(0);
	RangeType *r2 = PG_GETARG_RANGE(1);

	RangeBound	lower1, lower2;
	RangeBound	upper1, upper2;
	bool		empty1, empty2;

	int cmp;

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (lower1.rngtypid != upper1.rngtypid ||
		lower1.rngtypid != lower2.rngtypid ||
		lower1.rngtypid != upper2.rngtypid)
		elog(ERROR, "range types do not match");

	if (empty1 && empty2)
		PG_RETURN_INT32(0);
	else if (empty1)
		PG_RETURN_INT32(-1);
	else if (empty2)
		PG_RETURN_INT32(1);

	if ((cmp = range_cmp_bounds(&lower1, &lower2)) != 0)
		PG_RETURN_INT32(cmp);

	PG_RETURN_INT32(range_cmp_bounds(&upper1, &upper2));
}

Datum
range_lt(PG_FUNCTION_ARGS)
{
	int cmp = range_cmp(fcinfo);
	PG_RETURN_BOOL(cmp < 0);
}

Datum
range_le(PG_FUNCTION_ARGS)
{
	int cmp = range_cmp(fcinfo);
	PG_RETURN_BOOL(cmp <= 0);
}

Datum
range_ge(PG_FUNCTION_ARGS)
{
	int cmp = range_cmp(fcinfo);
	PG_RETURN_BOOL(cmp >= 0);
}

Datum
range_gt(PG_FUNCTION_ARGS)
{
	int cmp = range_cmp(fcinfo);
	PG_RETURN_BOOL(cmp > 0);
}

/* Hash support */
Datum
hash_range(PG_FUNCTION_ARGS)
{
	RangeType	*r			= PG_GETARG_RANGE(0);
	RangeBound	 lower;
	RangeBound	 upper;
	bool		 empty;
	char		 flags		= 0;
	uint32		 lower_hash = 0;
	uint32		 upper_hash = 0;
	uint32		 result		= 0;

	TypeCacheEntry *typentry;
	Oid subtype;
	FunctionCallInfoData locfcinfo;


	range_deserialize(r, &lower, &upper, &empty);

	if (lower.rngtypid != upper.rngtypid)
		elog(ERROR, "range types do not match");

	if (empty)
		flags |= RANGE_EMPTY;

	flags |= (lower.inclusive)	? RANGE_LB_INC  : 0;
	flags |= (lower.infinite)	? RANGE_LB_INF  : 0;
	flags |= (upper.inclusive)	? RANGE_UB_INC  : 0;
	flags |= (upper.infinite)	? RANGE_UB_INF  : 0;

	subtype = get_range_subtype(lower.rngtypid);

	/*
	 * We arrange to look up the hash function only once per series of
	 * calls, assuming the subtype doesn't change underneath us.  The
	 * typcache is used so that we have no memory leakage when being
	 * used as an index support function.
	 */
	typentry = (TypeCacheEntry *) fcinfo->flinfo->fn_extra;
	if (typentry == NULL || typentry->type_id != subtype)
	{
		typentry = lookup_type_cache(subtype, TYPECACHE_HASH_PROC_FINFO);
		if (!OidIsValid(typentry->hash_proc_finfo.fn_oid))
			ereport(ERROR,
					(errcode(ERRCODE_UNDEFINED_FUNCTION),
					 errmsg("could not identify a hash function for type %s",
							format_type_be(subtype))));
		fcinfo->flinfo->fn_extra = (void *) typentry;
	}

	/*
	 * Apply the hash function to each array element (the hash
	 * function shouldn't care about the collation).
	 */
	InitFunctionCallInfoData(locfcinfo, &typentry->hash_proc_finfo, 1,
							 InvalidOid, NULL, NULL);

	if (RANGE_HAS_LBOUND(flags))
	{
		locfcinfo.arg[0]	 = lower.val;
		locfcinfo.argnull[0] = false;
		locfcinfo.isnull	 = false;
		lower_hash = DatumGetUInt32(FunctionCallInvoke(&locfcinfo));
	}
	if (RANGE_HAS_UBOUND(flags))
	{
		locfcinfo.arg[0]	 = upper.val;
		locfcinfo.argnull[0] = false;
		locfcinfo.isnull	 = false;
		upper_hash = DatumGetUInt32(FunctionCallInvoke(&locfcinfo));
	}

	result	= hash_uint32((uint32) flags);
	result	= (result << 1) | (result >> 31);
	result ^= lower_hash;
	result	= (result << 1) | (result >> 31);
	result ^= upper_hash;

	PG_RETURN_INT32(result);
}

/*
 *----------------------------------------------------------
 * CANONICAL FUNCTIONS
 *----------------------------------------------------------
 */

Datum
int4range_canonical(PG_FUNCTION_ARGS)
{
	RangeType	*r = PG_GETARG_RANGE(0);

	RangeBound	lower;
	RangeBound	upper;
	bool		empty;

	range_deserialize(r, &lower, &upper, &empty);

	if (empty)
		PG_RETURN_RANGE(r);

	if (!lower.infinite && !lower.inclusive)
	{
		lower.val = DirectFunctionCall2(int4pl, lower.val, Int32GetDatum(1));
		lower.inclusive = true;
	}

	if (!upper.infinite && upper.inclusive)
	{
		upper.val = DirectFunctionCall2(int4pl, upper.val, Int32GetDatum(1));
		upper.inclusive = false;
	}

	PG_RETURN_RANGE(range_serialize(&lower, &upper, false));
}

Datum
int8range_canonical(PG_FUNCTION_ARGS)
{
	RangeType	*r = PG_GETARG_RANGE(0);

	RangeBound	lower;
	RangeBound	upper;
	bool		empty;

	range_deserialize(r, &lower, &upper, &empty);

	if (empty)
		PG_RETURN_RANGE(r);

	if (!lower.infinite && !lower.inclusive)
	{
		lower.val = DirectFunctionCall2(int8pl, lower.val, Int64GetDatum(1));
		lower.inclusive = true;
	}

	if (!upper.infinite && upper.inclusive)
	{
		upper.val = DirectFunctionCall2(int8pl, upper.val, Int64GetDatum(1));
		upper.inclusive = false;
	}

	PG_RETURN_RANGE(range_serialize(&lower, &upper, false));
}

Datum
daterange_canonical(PG_FUNCTION_ARGS)
{
	RangeType	*r = PG_GETARG_RANGE(0);

	RangeBound	lower;
	RangeBound	upper;
	bool		empty;

	range_deserialize(r, &lower, &upper, &empty);

	if (empty)
		PG_RETURN_RANGE(r);

	if (!lower.infinite && !lower.inclusive)
	{
		lower.val = DirectFunctionCall2(date_pli, lower.val, Int32GetDatum(1));
		lower.inclusive = true;
	}

	if (!upper.infinite && upper.inclusive)
	{
		upper.val = DirectFunctionCall2(date_pli, upper.val, Int32GetDatum(1));
		upper.inclusive = false;
	}

	PG_RETURN_RANGE(range_serialize(&lower, &upper, false));
}

/*
 *----------------------------------------------------------
 * SUPPORT FUNCTIONS
 *----------------------------------------------------------
 */

/*
 * Serialized format is:
 *
 *  4 bytes: Range type Oid
 *  Lower boundary, if any, aligned according to subtype's typalign
 *  Upper boundary, if any, aligned according to subtype's typalign
 *  1 byte for flags
 *
 * This representation is chosen for best alignment behavior, to avoid
 * pad bytes when aligning. However, it requires an odd
 * deserialization strategy, because we have to read the flags byte
 * before we know whether to read a boundary.
 */

/*
 * This serializes a range, but does not canonicalize it. This should
 * only be called by a canonicalization function.
 */
Datum
range_serialize(RangeBound *lower, RangeBound *upper, bool empty)
{
	Datum		range;
	size_t		msize;
	Pointer		ptr;
	Oid			subtype	   = get_range_subtype(lower->rngtypid);
	int16		typlen	   = get_typlen(subtype);
	char		typalign   = get_typalign(subtype);
	char		typbyval   = get_typbyval(subtype);
	char		typstorage = get_typstorage(subtype);
	char		flags	   = 0;

	if (lower->rngtypid != upper->rngtypid)
		elog(ERROR, "range types do not match");

	if (empty)
		flags |= RANGE_EMPTY;
	else if (range_cmp_bounds(lower, upper) > 0)
		elog(ERROR, "range lower bound must be less than or equal to range upper bound");

	flags |= (lower->inclusive) ? RANGE_LB_INC  : 0;
	flags |= (lower->infinite)  ? RANGE_LB_INF  : 0;
	flags |= (upper->inclusive) ? RANGE_UB_INC  : 0;
	flags |= (upper->infinite)  ? RANGE_UB_INF  : 0;

	msize  = VARHDRSZ;
	msize += sizeof(Oid);

	if (RANGE_HAS_LBOUND(flags))
	{
		msize = datum_compute_size(msize, lower->val, typbyval, typalign,
								   typlen, typstorage);
	}

	if (RANGE_HAS_UBOUND(flags))
	{
		msize = datum_compute_size(msize, upper->val, typbyval, typalign,
								   typlen, typstorage);
	}

	msize += sizeof(char);

	ptr = palloc0(msize);
	range = (Datum) ptr;

	ptr += VARHDRSZ;

	memcpy(ptr, &lower->rngtypid, sizeof(Oid));
	ptr += sizeof(Oid);

	if (RANGE_HAS_LBOUND(flags))
	{
		Assert(lower->lower);
		ptr = datum_write(ptr, lower->val, typbyval, typalign, typlen,
						  typstorage);
	}

	if (RANGE_HAS_UBOUND(flags))
	{
		Assert(!upper->lower);
		ptr = datum_write(ptr, upper->val, typbyval, typalign, typlen,
						  typstorage);
	}

	memcpy(ptr, &flags, sizeof(char));
	ptr += sizeof(char);

	SET_VARSIZE(range, msize);
	PG_RETURN_RANGE(range);
}

void
range_deserialize(RangeType *range, RangeBound *lower, RangeBound *upper,
				  bool *empty)
{
	Pointer		ptr	= VARDATA(range);
	char		typalign;
	int16		typlen;
	int16		typbyval;
	char		flags;
	Oid			rngtypid;
	Oid			subtype;
	Datum		lbound;
	Datum		ubound;
	Pointer		flags_ptr;

	memset(lower, 0, sizeof(RangeBound));
	memset(upper, 0, sizeof(RangeBound));

	/* peek at last byte to read the flag byte */
	flags_ptr = ptr + VARSIZE(range) - VARHDRSZ - 1;
	memcpy(&flags, flags_ptr, sizeof(char));

	memcpy(&rngtypid, ptr, sizeof(Oid));
	ptr += sizeof(Oid);

	if (rngtypid == ANYRANGEOID)
		ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				 errmsg("cannot output a value of type anyrange")));

	subtype	 = get_range_subtype(rngtypid);
	typalign = get_typalign(subtype);
	typlen	 = get_typlen(subtype);
	typbyval = get_typbyval(subtype);

	if (RANGE_HAS_LBOUND(flags))
	{
		ptr = (Pointer) att_align_pointer(ptr, typalign, typlen, ptr);
		lbound = fetch_att(ptr, typbyval, typlen);
		ptr = (Pointer) att_addlength_datum(ptr, typlen, PointerGetDatum(ptr));
		if (typlen == -1)
			lbound = PointerGetDatum(PG_DETOAST_DATUM(lbound));
	}
	else
		lbound = (Datum) 0;

	if (RANGE_HAS_UBOUND(flags))
	{
		ptr = (Pointer) att_align_pointer(ptr, typalign, typlen, ptr);
		ubound = fetch_att(ptr, typbyval, typlen);
		ptr = (Pointer) att_addlength_datum(ptr, typlen, PointerGetDatum(ptr));
		if (typlen == -1)
			ubound = PointerGetDatum(PG_DETOAST_DATUM(ubound));
	}
	else
		ubound = (Datum) 0;

	*empty = flags & RANGE_EMPTY;

	lower->rngtypid	 = rngtypid;
	lower->val		 = lbound;
	lower->inclusive = flags &	RANGE_LB_INC;
	lower->infinite	 = flags &	RANGE_LB_INF;
	lower->lower	 = true;

	upper->rngtypid	 = rngtypid;
	upper->val		 = ubound;
	upper->inclusive = flags &	RANGE_UB_INC;
	upper->infinite	 = flags &	RANGE_UB_INF;
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
range_cmp_bounds(RangeBound *b1, RangeBound *b2)
{
	regproc		cmpFn;
	int			result;

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

/*
 * Parse input of the form:
 *  <lb-inc> <lb> <sep> <ub> <ub-inc>
 * where
 *  <lb-inc> is either "[" or "("
 *  <lb> and <ub> are either plain strings or quoted strings
 *  <sep> is ","
 *  <ub-inc> is either "]" or ")"
 *
 * Quoted strings preserve whitespace, and allow using special
 * characters like "," and special values like "INF" ("\" is the
 * escape character if you need to include a double quote or backslash
 * character). Unquoted strings preserve internal whitespace, but not
 * leading or trailing whitespace.
 */
static void
range_parse(const char *input_str,  char *flags, char **lbound_str,
			   char **ubound_str)
{
	int			 ilen		   = strlen(input_str);
	char		*lb			   = palloc0(ilen + 1);
	char		*ub			   = palloc0(ilen + 1);
	int			 lidx		   = 0;
	int			 lb_last	   = 1;
	int			 uidx		   = 0;
	int			 ub_last	   = 1;
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
		switch(pstate)
		{
			case RANGE_PSTATE_INIT:
				if (isspace(ch))
					continue;
				if(ch == '-')
				{
					fl = RANGE_EMPTY;
					pstate = RANGE_PSTATE_DONE;
					/* read the rest to make sure it's whitespace */
					continue;
				}

				if (ch == '[' || ch == '(')
				{
					if (ch == '[')
						fl |= RANGE_LB_INC;
					pstate = RANGE_PSTATE_PRE_LB;
					continue;
				}
				break;
			case RANGE_PSTATE_PRE_LB:
				/* remove leading whitespace */
				if (isspace(ch))
					continue;
				pstate = RANGE_PSTATE_LB;
				/* fall through */
			case RANGE_PSTATE_LB:
				if (ch == '"' && !escape)
				{
					if (inside_quotes)
					{
						inside_quotes = false;
						pstate = RANGE_PSTATE_SEP;
					}
					else
					{
						inside_quotes = true;
						lb_quoted	  = true;
					}
					continue;
				}
				if (ch == '\\' && !escape && inside_quotes)
				{
					escape = true;
					continue;
				}
				escape = false;

				if (ch != ',')
				{
					lb[lidx++] = ch;
					/* track last significant character */
					if (inside_quotes || !isspace(ch))
						lb_last = lidx;
					continue;
				}

				pstate = RANGE_PSTATE_SEP;
				/* fall through */
			case RANGE_PSTATE_SEP:
				if (isspace(ch))
					continue;
				if (ch == ',')
				{
					pstate = RANGE_PSTATE_PRE_UB;
					continue;
				}
			case RANGE_PSTATE_PRE_UB:
				/* remove leading whitespace */
				if (isspace(ch))
					continue;
				pstate = RANGE_PSTATE_UB;
				/* fall through */
			case RANGE_PSTATE_UB:
				if (ch == '"' && !escape)
				{
					if (inside_quotes)
					{
						inside_quotes = false;
						pstate = RANGE_PSTATE_UB_INC;
					}
					else
					{
						inside_quotes = true;
						lb_quoted	  = true;
					}
					continue;
				}
				if (ch == '\\' && !escape && inside_quotes)
				{
					escape = true;
					continue;
				}
				escape = false;

				if (ch != ']' && ch != ')')
				{
					ub[uidx++] = ch;
					/* track last significant character */
					if (inside_quotes || !isspace(ch))
						ub_last = uidx;
					continue;
				}

				pstate = RANGE_PSTATE_UB_INC;
				/* fall through */
			case RANGE_PSTATE_UB_INC:
				if (isspace(ch))
					continue;
				if (ch == ']' || ch == ')')
				{
					if (ch == ']')
						fl |= RANGE_UB_INC;
					pstate = RANGE_PSTATE_DONE;
					continue;
				}
				break;
			case RANGE_PSTATE_DONE:
				if (isspace(ch))
					continue;
				break;
		}
		elog(ERROR, "syntax error on range input \"%s\", character %d",
			 input_str, i);
	}

	/* remove trailing whitespace */
	lb[lb_last] = '\0';
	ub[ub_last] = '\0';

	if (pstate != RANGE_PSTATE_DONE)
		elog(ERROR, "syntax error on range input: unexpected end of input");

	if (fl & RANGE_EMPTY)
		fl = RANGE_EMPTY;

	if (!lb_quoted && strncmp(lb, "NULL", ilen) == 0)
		elog(ERROR, "NULL range boundaries are not supported");
	if (!ub_quoted && strncmp(ub, "NULL", ilen) == 0)
		elog(ERROR, "NULL range boundaries are not supported");
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

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (lower1.rngtypid != upper1.rngtypid ||
		lower1.rngtypid != lower2.rngtypid ||
		lower1.rngtypid != upper2.rngtypid)
		elog(ERROR, "range types do not match");

	if (empty2)
		return true;
	else if (empty1)
		return false;

	if (range_cmp_bounds(&lower1, &lower2) > 0)
		return false;
	if (range_cmp_bounds(&upper1, &upper2) < 0)
		return false;

	return true;
}

/*
 * datum_compute_size() and datum_write() are modeled after
 * heap_compute_data_size() and heap_fill_tuple().
 */

static Size
datum_compute_size(Size data_length, Datum val, bool typbyval, char typalign,
				   int16 typlen, char typstorage)
{
	if (TYPE_IS_PACKABLE(typlen, typstorage) &&
		VARATT_CAN_MAKE_SHORT(DatumGetPointer(val)))
	{
		/*
		 * we're anticipating converting to a short varlena header, so
		 * adjust length and don't count any alignment
		 */
		data_length += VARATT_CONVERTED_SHORT_SIZE(DatumGetPointer(val));
	}
	else
	{
		data_length = att_align_datum(data_length, typalign, typlen, val);
		data_length = att_addlength_datum(data_length, typlen, val);
	}

	return data_length;
}

static Pointer
datum_write(Pointer ptr, Datum datum, bool typbyval, char typalign,
			int16 typlen, char typstorage)
{
	Size		data_length;

	if (typbyval)
	{
		/* pass-by-value */
		ptr = (char *) att_align_nominal(ptr, typalign);
		store_att_byval(ptr, datum, typlen);
		data_length = typlen;
	}
	else if (typlen == -1)
	{
		/* varlena */
		Pointer		val = DatumGetPointer(datum);

		if (VARATT_IS_EXTERNAL(val))
		{
			/* no alignment, since it's short by definition */
			data_length = VARSIZE_EXTERNAL(val);
			memcpy(ptr, val, data_length);
		}
		else if (VARATT_IS_SHORT(val))
		{
			/* no alignment for short varlenas */
			data_length = VARSIZE_SHORT(val);
			memcpy(ptr, val, data_length);
		}
		else if (TYPE_IS_PACKABLE(typlen, typstorage) &&
				 VARATT_CAN_MAKE_SHORT(val))
		{
			/* convert to short varlena -- no alignment */
			data_length = VARATT_CONVERTED_SHORT_SIZE(val);
			SET_VARSIZE_SHORT(ptr, data_length);
			memcpy(ptr + 1, VARDATA(val), data_length - 1);
		}
		else
		{
			/* full 4-byte header varlena */
			ptr = (char *) att_align_nominal(ptr, typalign);
			data_length = VARSIZE(val);
			memcpy(ptr, val, data_length);
		}
	}
	else if (typlen == -2)
	{
		/* cstring ... never needs alignment */
		Assert(typalign == 'c');
		data_length = strlen(DatumGetCString(datum)) + 1;
		memcpy(ptr, DatumGetPointer(datum), data_length);
	}
	else
	{
		/* fixed-length pass-by-reference */
		ptr = (char *) att_align_nominal(ptr, typalign);
		Assert(typlen > 0);
		data_length = typlen;
		memcpy(ptr, DatumGetPointer(datum), data_length);
	}

	ptr += data_length;

	return ptr;
}