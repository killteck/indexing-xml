/*-------------------------------------------------------------------------
 *
 * rangetypes_gist.c
 *	  GiST support for range types.
 *
 * Copyright (c) 2006-2011, PostgreSQL Global Development Group
 *
 *
 * IDENTIFICATION
 *	  src/backend/utils/adt/rangetypes_gist.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/gist.h"
#include "access/skey.h"
#include "utils/lsyscache.h"
#include "utils/rangetypes.h"

#define RANGESTRAT_EQ					1
#define RANGESTRAT_NE					2
#define RANGESTRAT_OVERLAPS				3
#define RANGESTRAT_CONTAINS_ELEM		4
#define RANGESTRAT_ELEM_CONTAINED_BY	5
#define RANGESTRAT_CONTAINS				6
#define RANGESTRAT_CONTAINED_BY			7
#define RANGESTRAT_BEFORE				8
#define RANGESTRAT_AFTER				9
#define RANGESTRAT_OVERLEFT				10
#define RANGESTRAT_OVERRIGHT			11
#define RANGESTRAT_ADJACENT				12

static RangeType *range_super_union(RangeType *r1, RangeType *r2);
static bool range_gist_consistent_int(StrategyNumber strategy, RangeType *key,
									  RangeType *query);
static bool range_gist_consistent_leaf(StrategyNumber strategy, RangeType *key,
									   RangeType *query);
static int sort_item_cmp(const void *a, const void *b);

/*
 * Auxiliary structure for picksplit method.
 */
typedef struct
{
	int			 index;
	RangeType	*data;
} PickSplitSortItem;


/* GiST support */
Datum
range_gist_consistent(PG_FUNCTION_ARGS)
{
	GISTENTRY			*entry	  = (GISTENTRY *) PG_GETARG_POINTER(0);
	Datum				 dquery	  = PG_GETARG_DATUM(1);
	StrategyNumber		 strategy = (StrategyNumber) PG_GETARG_UINT16(2);
	/* Oid subtype = PG_GETARG_OID(3); */
	bool				*recheck  = (bool *) PG_GETARG_POINTER(4);
	RangeType			*key	  = DatumGetRangeType(entry->key);
	RangeType			*query;

	RangeBound	lower;
	RangeBound	upper;
	bool		empty;
	Oid			rngtypid;

	*recheck = false;
	range_deserialize(key, &lower, &upper, &empty);
	rngtypid = lower.rngtypid;

	switch(strategy)
	{
		RangeBound lower;
		RangeBound upper;

		case RANGESTRAT_CONTAINS_ELEM:
		case RANGESTRAT_ELEM_CONTAINED_BY:
			lower.rngtypid	= rngtypid;
			lower.inclusive = true;
			lower.val		= dquery;
			lower.lower		= true;
			lower.infinite	= false;
			upper.rngtypid	= rngtypid;
			upper.inclusive = true;
			upper.val		= dquery;
			upper.lower		= false;
			upper.infinite	= false;
			query			= DatumGetRangeType(
				make_range(&lower, &upper, false));
			break;
		default:
			query			= DatumGetRangeType(dquery);
			break;
	}

	if(GIST_LEAF(entry))
		PG_RETURN_BOOL(range_gist_consistent_leaf(strategy, key, query));
	else
		PG_RETURN_BOOL(range_gist_consistent_int(strategy, key, query));
}

Datum
range_gist_union(PG_FUNCTION_ARGS)
{
	GistEntryVector		*entryvec = (GistEntryVector *) PG_GETARG_POINTER(0);
	GISTENTRY			*ent	  = entryvec->vector;
	RangeType			*tmp_range;
	RangeType			*result_range;
	int					 numentries;
	int					 i;

	numentries	 = entryvec->n;
	tmp_range	 = DatumGetRangeType(ent[0].key);
	result_range = tmp_range;

	if (numentries == 1)
		PG_RETURN_RANGE(tmp_range);

	for (i = 1; i < numentries; i++)
	{
		tmp_range	 = DatumGetRangeType(ent[i].key);
		result_range = range_super_union(result_range, tmp_range);
	}

	PG_RETURN_RANGE(result_range);
}

Datum
range_gist_compress(PG_FUNCTION_ARGS)
{
	GISTENTRY  *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
	PG_RETURN_POINTER(entry);
}

Datum
range_gist_decompress(PG_FUNCTION_ARGS)
{
	GISTENTRY  *entry = (GISTENTRY *) PG_GETARG_POINTER(0);
	PG_RETURN_POINTER(entry);
}

Datum
range_gist_penalty(PG_FUNCTION_ARGS)
{
	GISTENTRY	*origentry = (GISTENTRY *) PG_GETARG_POINTER(0);
	GISTENTRY	*newentry  = (GISTENTRY *) PG_GETARG_POINTER(1);
	float		*penalty   = (float *) PG_GETARG_POINTER(2);
	RangeType	*orig	   = DatumGetRangeType(origentry->key);
	RangeType	*new	   = DatumGetRangeType(newentry->key);
	RangeType	*s_union   = range_super_union(orig, new);

	regproc		subtype_float;
	RangeBound	lower1, lower2;
	RangeBound	upper1, upper2;
	bool		empty1, empty2;
	float		length1, length2;

	range_deserialize(orig, &lower1, &upper1, &empty1);
	range_deserialize(s_union, &lower2, &upper2, &empty2);

	subtype_float = get_range_subtype_float(lower1.rngtypid);

	if (empty1)
		length1 = 0.0;
	else if (lower1.infinite || upper1.infinite)
		length1 = 1.0/0.0;
	else
	{
		double l = DatumGetFloat8(OidFunctionCall1(subtype_float, lower1.val));
		double u = DatumGetFloat8(OidFunctionCall1(subtype_float, upper1.val));
		length1 = u - l;
	}

	if (empty2)
		length2 = 0.0;
	else if (lower2.infinite || upper2.infinite)
		length2 = 1.0/0.0;
	else
	{
		double l = DatumGetFloat8(OidFunctionCall1(subtype_float, lower2.val));
		double u = DatumGetFloat8(OidFunctionCall1(subtype_float, upper2.val));
		length2 = u - l;
	}

	*penalty = (float) (length2 - length1);
	PG_RETURN_POINTER(penalty);
}

/*
 * The GiST PickSplit method for ranges
 * Algorithm based on sorting. Incoming array of periods is sorting using
 * period_compare function. After that first half of periods goes to the left
 * datum, and the second half of periods goes to the right datum.
 */
Datum
range_gist_picksplit(PG_FUNCTION_ARGS)
{
	GistEntryVector		*entryvec = (GistEntryVector *) PG_GETARG_POINTER(0);
	GIST_SPLITVEC		*v		  = (GIST_SPLITVEC *) PG_GETARG_POINTER(1);
	OffsetNumber		 i;
	RangeType			*pred_left;
	RangeType			*pred_right;
	PickSplitSortItem	*sortItems;
	int					 nbytes;
	OffsetNumber		 split_idx;
	OffsetNumber		*left;
	OffsetNumber		*right;
	OffsetNumber		 maxoff;

	maxoff = entryvec->n - 1;
	nbytes = (maxoff + 1) * sizeof(OffsetNumber);
	sortItems = (PickSplitSortItem *)palloc(maxoff * sizeof(PickSplitSortItem));
	v->spl_left = (OffsetNumber *) palloc(nbytes);
	v->spl_right = (OffsetNumber *) palloc(nbytes);

	/*
	 * Preparing auxiliary array and sorting.
	 */
	for (i = FirstOffsetNumber; i <= maxoff; i = OffsetNumberNext(i))
	{
		sortItems[i - 1].index = i;
		sortItems[i - 1].data = DatumGetRangeType(entryvec->vector[i].key);
	}
	qsort(sortItems, maxoff, sizeof(PickSplitSortItem), sort_item_cmp);
	split_idx = maxoff / 2;

	left = v->spl_left;
	v->spl_nleft = 0;
	right = v->spl_right;
	v->spl_nright = 0;

	/*
	 * First half of segs goes to the left datum.
	 */
	pred_left = DatumGetRangeType(sortItems[0].data);
	*left++ = sortItems[0].index;
	v->spl_nleft++;
	for (i = 1; i < split_idx; i++)
	{
		pred_left = range_super_union(pred_left,
									  DatumGetRangeType(sortItems[i].data));
		*left++ = sortItems[i].index;
		v->spl_nleft++;
	}

	/*
	 * Second half of segs goes to the right datum.
	 */
	pred_right = DatumGetRangeType(sortItems[split_idx].data);
	*right++ = sortItems[split_idx].index;
	v->spl_nright++;
	for (i = split_idx + 1; i < maxoff; i++)
	{
		pred_right = range_super_union(pred_right,
									   DatumGetRangeType(sortItems[i].data));
		*right++ = sortItems[i].index;
		v->spl_nright++;
	}

	*left = *right = FirstOffsetNumber; /* sentinel value, see dosplit() */

	v->spl_ldatum = RangeTypeGetDatum(pred_left);
	v->spl_rdatum = RangeTypeGetDatum(pred_right);

	PG_RETURN_POINTER(v);
}

Datum
range_gist_same(PG_FUNCTION_ARGS)
{
	Datum r1 = PG_GETARG_DATUM(0);
	Datum r2 = PG_GETARG_DATUM(1);
	bool *result = (bool *) PG_GETARG_POINTER(2);

	*result = DatumGetBool(DirectFunctionCall2(range_eq, r1, r2));
	PG_RETURN_POINTER(result);
}

/*
 *----------------------------------------------------------
 * STATIC FUNCTIONS
 *----------------------------------------------------------
 */

/* return the smallest range that contains r1 and r2 */
static RangeType *
range_super_union(RangeType *r1, RangeType *r2)
{
	RangeBound	 lower1, lower2;
	RangeBound	 upper1, upper2;
	bool		 empty1, empty2;
	RangeBound	*result_lower;
	RangeBound	*result_upper;

	range_deserialize(r1, &lower1, &upper1, &empty1);
	range_deserialize(r2, &lower2, &upper2, &empty2);

	if (empty1)
		return r2;
	if (empty2)
		return r1;

	if (range_cmp_bounds(&lower1, &lower2) < 0)
		result_lower = &lower1;
	else
		result_lower = &lower2;

	if (range_cmp_bounds(&upper1, &upper2) > 0)
		result_upper = &upper1;
	else
		result_upper = &upper2;

	/* optimization to avoid constructing a new range */
	if (result_lower == &lower1 && result_upper == &upper1)
		return r1;
	if (result_lower == &lower2 && result_upper == &upper2)
		return r2;

	return DatumGetRangeType(make_range(result_lower, result_upper, false));
}

static bool
range_gist_consistent_int(StrategyNumber strategy, RangeType *key,
						  RangeType *query)
{
	Datum (*proc)(PG_FUNCTION_ARGS);

	RangeBound	lower1, lower2;
	RangeBound	upper1, upper2;
	bool		empty1, empty2;

	bool retval;
	bool negate = false;

	range_deserialize(key, &lower1, &upper1, &empty1);
	range_deserialize(query, &lower2, &upper2, &empty2);

	switch (strategy)
	{
		case RANGESTRAT_EQ:
			proc   = range_contains;
			break;
		case RANGESTRAT_NE:
			return true;
			break;
		case RANGESTRAT_OVERLAPS:
			proc   = range_overlaps;
			break;
		case RANGESTRAT_CONTAINS_ELEM:
		case RANGESTRAT_CONTAINS:
			proc   = range_contains;
			break;
		case RANGESTRAT_ELEM_CONTAINED_BY:
		case RANGESTRAT_CONTAINED_BY:
			if (empty1)
				return true;
			return true; //TODO
			proc   = range_overlaps;
			break;
		case RANGESTRAT_BEFORE:
			if (empty1)
				return false;
			proc   = range_overright;
			negate = true;
			break;
		case RANGESTRAT_AFTER:
			if (empty1)
				return false;
			proc   = range_overleft;
			negate = true;
			break;
		case RANGESTRAT_OVERLEFT:
			if (empty1)
				return false;
			proc   = range_after;
			negate = true;
			break;
		case RANGESTRAT_OVERRIGHT:
			if (empty1)
				return false;
			proc = range_before;
			negate = true;
			break;
		case RANGESTRAT_ADJACENT:
			if (empty1 || empty2)
				return false;
			if (DatumGetBool(
					DirectFunctionCall2(range_adjacent,
										RangeTypeGetDatum(key),
										RangeTypeGetDatum(query))))
				return true;
			proc = range_overlaps;
			break;
	}

	retval = DatumGetBool(DirectFunctionCall2(proc, RangeTypeGetDatum(key),
											  RangeTypeGetDatum(query)));

	if (negate)
		retval = !retval;

	PG_RETURN_BOOL(retval);
}

static bool
range_gist_consistent_leaf(StrategyNumber strategy, RangeType *key,
						   RangeType *query)
{
	Datum (*proc)(PG_FUNCTION_ARGS);

	RangeBound	lower1, lower2;
	RangeBound	upper1, upper2;
	bool		empty1, empty2;

	range_deserialize(key, &lower1, &upper1, &empty1);
	range_deserialize(query, &lower2, &upper2, &empty2);

	switch (strategy)
	{
		case RANGESTRAT_EQ:
			proc = range_eq;
			break;
		case RANGESTRAT_NE:
			proc = range_ne;
			break;
		case RANGESTRAT_OVERLAPS:
			proc = range_overlaps;
			break;
		case RANGESTRAT_CONTAINS_ELEM:
		case RANGESTRAT_CONTAINS:
			proc = range_contains;
			break;
		case RANGESTRAT_ELEM_CONTAINED_BY:
		case RANGESTRAT_CONTAINED_BY:
			proc = range_contained_by;
			break;
		case RANGESTRAT_BEFORE:
			if (empty1 || empty2)
				return false;
			proc = range_before;
			break;
		case RANGESTRAT_AFTER:
			if (empty1 || empty2)
				return false;
			proc = range_after;
			break;
		case RANGESTRAT_OVERLEFT:
			if (empty1 || empty2)
				return false;
			proc = range_overleft;
			break;
		case RANGESTRAT_OVERRIGHT:
			if (empty1 || empty2)
				return false;
			proc = range_overright;
			break;
		case RANGESTRAT_ADJACENT:
			if (empty1 || empty2)
				return false;
			proc = range_adjacent;
			break;
	}

	return DatumGetBool(DirectFunctionCall2(proc, RangeTypeGetDatum(key),
											RangeTypeGetDatum(query)));
}

/*
 * Compare function for PickSplitSortItem. This is actually the
 * interesting part of the picksplit algorithm.
 *
 * We want to push all of the empty ranges to one side, and all of the
 * unbounded ranges to the other side. That's because empty ranges are
 * rarely going to match search criteria, and unbounded ranges
 * frequently will. Normal bounded intervals will be in the
 * middle. Arbitrarily, we choose to push the unbounded intervals
 * right and the empty intervals left.
 */
static int
sort_item_cmp(const void *a, const void *b)
{
	PickSplitSortItem *i1 = (PickSplitSortItem *)a;
	PickSplitSortItem *i2 = (PickSplitSortItem *)b;
	return DatumGetInt32(DirectFunctionCall2(range_cmp,
											 RangeTypeGetDatum(i1->data),
											 RangeTypeGetDatum(i2->data)));
}
