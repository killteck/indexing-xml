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
	Oid			rngdtype;		/* OID of difference type, if not 'subtype' */
	regproc		rngalign;		/* canonicalize range, or 0 */
	regproc		rnginput;		/* optional input parser */
	regproc		rngoutput;		/* optional output function */
	regproc		rngsubcmp;		/* compare values of type 'subtype' */
	regproc		rngsubplus;		/* add values of type 'subtype' */
	regproc		rngsubminus;	/* subtract values of type 'subtype' */
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
#define Natts_pg_range					7
#define Anum_pg_range_rngtypid			1
#define Anum_pg_range_rngsubtype		2
#define Anum_pg_range_rngdtype			3
#define Anum_pg_range_rngalign			4
#define Anum_pg_range_rnginput			5
#define Anum_pg_range_rngoutput			6
#define Anum_pg_range_rngsubcmp			7
#define Anum_pg_range_rngsubplus		8
#define Anum_pg_range_rngsubminus		9

/* ----------------
 *		pg_range has no initial contents
 * ----------------
 */

/*
 * prototypes for functions in pg_range.c
 */

#endif   /* PG_RANGE_H */
