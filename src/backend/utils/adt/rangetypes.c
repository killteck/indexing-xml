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
#include "fmgr.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"


static void anyrange_parse(const char *input_str,  char *flags,
						   char **lbound_str, char **ubound_str);
static char *anyrange_deparse(char flags, char *lbound_str, char *ubound_str);
static Datum anyrange_serialize(int16 typlen, char flags, Datum lbound,
								Datum ubound);
static void anyrange_deserialize(Datum range, int16 typlen, char *flags,
								 Datum *lbound, Datum *ubound);

/* Basic I/O support */

Datum
anyrange_in(PG_FUNCTION_ARGS)
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

	Oid			subtype		= get_range_subtype(rngtypoid);
	regproc		canonical	= get_range_canonical(rngtypoid);

	regproc		subInput;
	FmgrInfo	subInputFn;
	Oid			ioParam;

	/* parse */
	anyrange_parse(input_str, &flags, &lbound_str, &ubound_str);

	/* input */
	getTypeInputInfo(subtype, &subInput, &ioParam);
	fmgr_info(subInput, &subInputFn);

	lbound = InputFunctionCall(&subInputFn, lbound_str, ioParam, typmod);
	ubound = InputFunctionCall(&subInputFn, ubound_str, ioParam, typmod);

	/* serialize */
	range = anyrange_serialize(flags, lbound, ubound, get_typlen(subtype));

	/* canonicalize */
	if (OidIsValid(canonical))
		range = OidFunctionCall1(canonical, range);

	PG_RETURN_DATUM(range);
}

Datum
anyrange_out(PG_FUNCTION_ARGS)
{
	Datum		range = PG_GETARG_DATUM(0);

	/* need to know the type of the input */
	Oid			rngtypoid = get_fn_expr_argtype(fcinfo->flinfo, 0);
	Oid			subtype	  = get_range_subtype(rngtypoid);

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
	anyrange_deserialize(range, get_typlen(subtype), &flags, &lbound, &ubound);

	/* output */
	getTypeOutputInfo(subtype, &subOutput, &isVarlena);
	fmgr_info(subOutput, &subOutputFn);
	lbound_str = OutputFunctionCall(&subOutputFn, lbound);
	ubound_str = OutputFunctionCall(&subOutputFn, ubound);

	/* deparse */
	output_str = anyrange_deparse(flags, lbound_str, ubound_str);

	PG_RETURN_CSTRING(output_str);
}

Datum
anyrange_recv(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}

Datum
anyrange_send(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID();
}

/* STATIC FUNCTIONS */

static void
anyrange_parse(const char *input_str,  char *flags, char **lbound_str,
			   char **ubound_str)
{
	return;
}

static char *
anyrange_deparse(char flags, char *lbound_str, char *ubound_str)
{
	return NULL;
}

static Datum
anyrange_serialize(int16 typlen, char flags, Datum lbound, Datum ubound)
{
	return 0;
}

static void
anyrange_deserialize(Datum range, int16 typlen, char *flags, Datum *lbound,
					 Datum *ubound)
{
	return;
}
