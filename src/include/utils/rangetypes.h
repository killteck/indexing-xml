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
extern Datum anyrange_recv(PG_FUNCTION_ARGS);
extern Datum anyrange_send(PG_FUNCTION_ARGS);

/* range -> subtype */
extern Datum anyrange_lbound(PG_FUNCTION_ARGS);
extern Datum anyrange_ubound(PG_FUNCTION_ARGS);

/* range -> bool */
extern Datum anyrange_empty(PG_FUNCTION_ARGS);
extern Datum anyrange_lb_null(PG_FUNCTION_ARGS);
extern Datum anyrange_ub_null(PG_FUNCTION_ARGS);
extern Datum anyrange_lb_inf(PG_FUNCTION_ARGS);
extern Datum anyrange_ub_inf(PG_FUNCTION_ARGS);

/* range, range -> bool */
extern Datum anyrange_eq(PG_FUNCTION_ARGS);
extern Datum anyrange_neq(PG_FUNCTION_ARGS);
extern Datum anyrange_contains(PG_FUNCTION_ARGS);
extern Datum anyrange_contained_by(PG_FUNCTION_ARGS);
extern Datum anyrange_before(PG_FUNCTION_ARGS);
extern Datum anyrange_after(PG_FUNCTION_ARGS);
extern Datum anyrange_overlaps(PG_FUNCTION_ARGS);
extern Datum anyrange_overleft(PG_FUNCTION_ARGS);
extern Datum anyrange_overright(PG_FUNCTION_ARGS);

/* range, range -> range */
extern Datum anyrange_minus(PG_FUNCTION_ARGS);
extern Datum anyrange_union(PG_FUNCTION_ARGS);
extern Datum anyrange_intersect(PG_FUNCTION_ARGS);

/* GiST support */
extern Datum anyrange_gist_consistent(PG_FUNCTION_ARGS);
extern Datum anyrange_gist_union(PG_FUNCTION_ARGS);
extern Datum anyrange_gist_penalty(PG_FUNCTION_ARGS);
extern Datum anyrange_gist_picksplit(PG_FUNCTION_ARGS);
extern Datum anyrange_gist_same(PG_FUNCTION_ARGS);

/* for defining more generic functions */
extern Datum make_range(Oid rngtypoid, char flags, Datum lbound, Datum ubound);
extern void anyrange_deserialize(RangeType *range, Oid *rngtypoid, char *flags,
								 Datum *lbound, Datum *ubound);

/* for defining a range "canonicalize" function */
extern Datum anyrange_serialize(Oid rngtypoid, char flags, Datum lbound,
								Datum ubound);

#endif   /* RANGETYPES_H */
