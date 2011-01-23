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

#include "utils/rangetypes.h"

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
range_gist_compress(PG_FUNCTION_ARGS)
{
	PG_RETURN_VOID(); //TODO
}

Datum
range_gist_decompress(PG_FUNCTION_ARGS)
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


