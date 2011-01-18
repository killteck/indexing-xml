/*-------------------------------------------------------------------------
 *
 * rangetypes.h
 *	  Declarations for Postgres range types.
 *
 */

#ifndef RANGETYPES_H
#define RANGETYPES_H

#include "fmgr.h"

typedef struct varlena RangeType;

typedef struct
{
	Datum		val;
	Oid			rngtypid;
	bool		infinite;
	bool		lower;
	bool		inclusive;
} RangeBound;

/*
 * fmgr macros for range type objects
 */
#define DatumGetRangeType(X)			((RangeType *) PG_DETOAST_DATUM(X))
#define DatumGetRangeTypeCopy(X)		((RangeType *) PG_DETOAST_DATUM_COPY(X))
#define RangeTypeGetDatum(X)			PointerGetDatum(X)
#define PG_GETARG_RANGE(n)				DatumGetRangeType(PG_GETARG_DATUM(n))
#define PG_GETARG_RANGE_COPY(n)			DatumGetRangeTypeCopy(PG_GETARG_DATUM(n))
#define PG_RETURN_RANGE(x)				return RangeTypeGetDatum(x)

/*
 * prototypes for functions defined in rangetypes.c
 */

/* IO */
extern Datum anyrange_in(PG_FUNCTION_ARGS);
extern Datum anyrange_out(PG_FUNCTION_ARGS);
extern Datum range_in(PG_FUNCTION_ARGS);
extern Datum range_out(PG_FUNCTION_ARGS);
extern Datum range_recv(PG_FUNCTION_ARGS);
extern Datum range_send(PG_FUNCTION_ARGS);

/* constructors */
extern Datum range_make1(PG_FUNCTION_ARGS);
extern Datum range_linf_(PG_FUNCTION_ARGS);
extern Datum range_uinf_(PG_FUNCTION_ARGS);
extern Datum range_linfi(PG_FUNCTION_ARGS);
extern Datum range_uinfi(PG_FUNCTION_ARGS);
extern Datum range(PG_FUNCTION_ARGS);
extern Datum range__(PG_FUNCTION_ARGS);
extern Datum range_i(PG_FUNCTION_ARGS);
extern Datum rangei_(PG_FUNCTION_ARGS);
extern Datum rangeii(PG_FUNCTION_ARGS);

/* range -> subtype */
extern Datum range_lower(PG_FUNCTION_ARGS);
extern Datum range_upper(PG_FUNCTION_ARGS);

/* range -> bool */
extern Datum range_empty(PG_FUNCTION_ARGS);
extern Datum range_lower_inc(PG_FUNCTION_ARGS);
extern Datum range_upper_inc(PG_FUNCTION_ARGS);
extern Datum range_lower_inf(PG_FUNCTION_ARGS);
extern Datum range_upper_inf(PG_FUNCTION_ARGS);

/* range, point -> bool */
extern Datum range_contains_elem(PG_FUNCTION_ARGS);

/* range, range -> bool */
extern Datum range_eq(PG_FUNCTION_ARGS);
extern Datum range_neq(PG_FUNCTION_ARGS);
extern Datum range_contains(PG_FUNCTION_ARGS);
extern Datum range_contained_by(PG_FUNCTION_ARGS);
extern Datum range_before(PG_FUNCTION_ARGS);
extern Datum range_after(PG_FUNCTION_ARGS);
extern Datum range_adjacent(PG_FUNCTION_ARGS);
extern Datum range_overlaps(PG_FUNCTION_ARGS);
extern Datum range_overleft(PG_FUNCTION_ARGS);
extern Datum range_overright(PG_FUNCTION_ARGS);

/* range, range -> range */
extern Datum range_minus(PG_FUNCTION_ARGS);
extern Datum range_union(PG_FUNCTION_ARGS);
extern Datum range_intersect(PG_FUNCTION_ARGS);

/* GiST support */
extern Datum range_gist_consistent(PG_FUNCTION_ARGS);
extern Datum range_gist_union(PG_FUNCTION_ARGS);
extern Datum range_gist_penalty(PG_FUNCTION_ARGS);
extern Datum range_gist_picksplit(PG_FUNCTION_ARGS);
extern Datum range_gist_same(PG_FUNCTION_ARGS);

/* for defining more generic functions */
extern Datum make_range(RangeBound *lower, RangeBound *upper, bool empty);
extern void range_deserialize(RangeType *range, RangeBound *lower,
							  RangeBound *upper, bool *empty);
extern int range_cmp_bounds(RangeBound *b1, RangeBound *b2);
extern RangeType *make_empty_range(Oid rngtypid);

/* for defining a range "canonicalize" function */
extern Datum range_serialize(RangeBound *lower, RangeBound *upper, bool empty);

#endif   /* RANGETYPES_H */
