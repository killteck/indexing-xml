/*-------------------------------------------------------------------------
 *
 * pg_range.h
 *	  definition of the system "range" relation (pg_range)
 *	  along with the relation's initial contents.
 *
 *
 * Copyright (c) 2006-2010, PostgreSQL Global Development Group
 *
 * src/include/catalog/pg_range.h
 *
 * NOTES
 *	  the genbki.pl script reads this file and generates .bki
 *	  information from the DATA() statements.
 *
 *	  XXX do NOT break up DATA() statements into multiple lines!
 *		  the scripts are not as smart as you might think...
 *
 *-------------------------------------------------------------------------
 */
#ifndef PG_RANGE_H
#define PG_RANGE_H

#include "catalog/genbki.h"

/* ----------------
 *		pg_range definition.  cpp turns this into
 *		typedef struct FormData_pg_range
 * ----------------
 */
#define RangeRelationId	3541

CATALOG(pg_range,3541) BKI_WITHOUT_OIDS
{
	Oid			rngtypid;		/* OID of owning range type */
	Oid			rngsubtype;		/* OID of range's subtype */
	regproc		rngsubcmp;		/* compare values of type 'subtype' */
	regproc		rngcanonical;	/* canonicalize range, or 0 */
	regproc		rngsubfloat;	/* represent subtype as a float8 (for GiST) */
} FormData_pg_range;

/* ----------------
 *		Form_pg_range corresponds to a pointer to a tuple with
 *		the format of pg_range relation.
 * ----------------
 */
typedef FormData_pg_range *Form_pg_range;

/* ----------------
 *		compiler constants for pg_range
 * ----------------
 */
#define Natts_pg_range					5
#define Anum_pg_range_rngtypid			1
#define Anum_pg_range_rngsubtype		2
#define Anum_pg_range_rngsubcmp			3
#define Anum_pg_range_rngcanonical		4
#define Anum_pg_range_rngsubfloat		5

/*
 * prototypes for functions in pg_range.c
 */

void RangeCreate(Oid rangeTypeOid, Oid rangeSubType,
				 regproc rangeSubtypeCmp, regproc rangeCanonical,
				 regproc rangeSubFloat);
void RangeDelete(Oid rangeTypeOid);

/* ----------------
 *		initial contents of pg_range
 * ----------------
 */

#endif   /* PG_RANGE_H */
