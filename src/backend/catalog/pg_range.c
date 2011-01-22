/*-------------------------------------------------------------------------
 *
 * pg_range.c
 *	  routines to support manipulation of the pg_range relation
 *
 * Copyright (c) 2006-2010, PostgreSQL Global Development Group
 *
 *
 * IDENTIFICATION
 *	  src/backend/catalog/pg_range.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "access/genam.h"
#include "access/heapam.h"
#include "catalog/dependency.h"
#include "catalog/indexing.h"
#include "catalog/pg_proc.h"
#include "catalog/pg_range.h"
#include "catalog/pg_type.h"
#include "utils/fmgroids.h"
#include "utils/tqual.h"

/*
 * RangeCreate
 *		Create an entry in pg_range.
 */
void
RangeCreate(Oid rangeTypeOid, Oid rangeSubType, regproc rangeSubtypeCmp,
			regproc rangeCanonical, regproc rangeSubFloat)
{
	Relation			pg_range;
	Datum				values[Natts_pg_range];
	bool				nulls[Natts_pg_range];
	HeapTuple			tup;
	ObjectAddress		myself;
	ObjectAddress		referenced;

	pg_range = heap_open(RangeRelationId, RowExclusiveLock);

	memset(nulls, 0, Natts_pg_range * sizeof(bool));

	values[Anum_pg_range_rngtypid - 1]	   = ObjectIdGetDatum(rangeTypeOid);
	values[Anum_pg_range_rngsubtype - 1]   = ObjectIdGetDatum(rangeSubType);
	values[Anum_pg_range_rngsubcmp - 1]	   = ObjectIdGetDatum(rangeSubtypeCmp);
	values[Anum_pg_range_rngcanonical - 1] = ObjectIdGetDatum(rangeCanonical);
	values[Anum_pg_range_rngsubfloat - 1]  = ObjectIdGetDatum(rangeSubFloat);

	tup = heap_form_tuple(RelationGetDescr(pg_range), values, nulls);
	simple_heap_insert(pg_range, tup);
	CatalogUpdateIndexes(pg_range, tup);
	heap_freetuple(tup);

	/* record dependencies */

	myself.classId	   = TypeRelationId;
	myself.objectId	   = rangeTypeOid;
	myself.objectSubId = 0;

	referenced.classId	   = TypeRelationId;
	referenced.objectId	   = rangeSubType;
	referenced.objectSubId = 0;
	recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);

	referenced.classId	   = ProcedureRelationId;
	referenced.objectId	   = rangeSubtypeCmp;
	referenced.objectSubId = 0;
	recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);

	if (OidIsValid(rangeCanonical))
	{
		referenced.classId	   = ProcedureRelationId;
		referenced.objectId	   = rangeCanonical;
		referenced.objectSubId = 0;
		recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);
	}

	if (OidIsValid(rangeSubFloat))
	{
		referenced.classId	   = ProcedureRelationId;
		referenced.objectId	   = rangeSubFloat;
		referenced.objectSubId = 0;
		recordDependencyOn(&myself, &referenced, DEPENDENCY_NORMAL);
	}

	heap_close(pg_range, RowExclusiveLock);
}


/*
 * RangeDelete
 *		Remove the pg_range entry.
 */
void
RangeDelete(Oid rangeTypeOid)
{
	Relation	pg_range;
	ScanKeyData key[1];
	SysScanDesc scan;
	HeapTuple	tup;

	pg_range = heap_open(RangeRelationId, RowExclusiveLock);

	ScanKeyInit(&key[0],
				Anum_pg_range_rngtypid,
				BTEqualStrategyNumber, F_OIDEQ,
				ObjectIdGetDatum(rangeTypeOid));

	scan = systable_beginscan(pg_range, RangeTypidIndexId, true,
							  SnapshotNow, 1, key);

	while (HeapTupleIsValid(tup = systable_getnext(scan)))
	{
		simple_heap_delete(pg_range, &tup->t_self);
	}

	systable_endscan(scan);

	heap_close(pg_range, RowExclusiveLock);
}
