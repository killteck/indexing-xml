/*-------------------------------------------------------------------------
 *
 * rangetypes.h
 *	  Declarations for Postgres range types.
 *
 */

#ifndef RANGETYPES_H
#define RANGETYPES_H

#include "fmgr.h"

/* flags */
#define RANGE_EMPTY		0x01
#define RANGE_LB_NULL	0x02
#define RANGE_LB_INF	0x04
#define RANGE_UB_NULL	0x08
#define RANGE_UB_INF	0x10

#define RANGE_HAS_LBOUND(flags) (!(flags & (RANGE_EMPTY |   \
											RANGE_LB_NULL |	\
											RANGE_LB_INF)))

#define RANGE_HAS_UBOUND(flags) (!(flags & (RANGE_EMPTY |	\
											RANGE_UB_NULL |	\
											RANGE_UB_INF)))

/*
 * prototypes for functions defined in rangetypes.c
 */
extern Datum anyrange_in(PG_FUNCTION_ARGS);
extern Datum anyrange_out(PG_FUNCTION_ARGS);
extern Datum anyrange_recv(PG_FUNCTION_ARGS);
extern Datum anyrange_send(PG_FUNCTION_ARGS);
extern Datum make_range(Oid rngtypoid, char flags, Datum lbound, Datum ubound);
extern Datum anyrange_serialize(Oid rngtypoid, char flags, Datum lbound,
								Datum ubound);
extern void anyrange_deserialize(Datum range, Oid *rngtypoid, char *flags,
								 Datum *lbound, Datum *ubound);

#endif   /* RANGETYPES_H */
