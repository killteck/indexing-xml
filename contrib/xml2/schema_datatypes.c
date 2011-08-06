////////////////////////////////////////////////////////////////////////////////
// author:	Tomas Pospisil, xpospi04@stud.fit.vutbr.cz, killteck@seznam.cz
// project:	Part of Master's Thesis
// date:	25.4.2011
// file:	schema_datatypes.c
// desc:	Declaring new datatypes for DTD, XSD, RNG schema used for
//			XML validations
//
////////////////////////////////////////////////////////////////////////////////

/* postgresql includes */
#include "postgres.h"

#include "executor/spi.h"
#include "fmgr.h"
#include "funcapi.h"
#include "lib/stringinfo.h"
#include "miscadmin.h"
#include "utils/builtins.h"
#include "utils/xml.h"
#include <sys/syslog.h>

#ifdef USE_LIBXML
	/* libxml includes */
	#include <libxml/xpath.h>
	#include <libxml/tree.h>
	#include <libxml/xmlmemory.h>
	#include <libxml/xmlerror.h>
	#include <libxml/parserInternals.h>
	#include <libxml/parser.h>
	#include <libxml/relaxng.h>
	#include <libxml/valid.h>
	#include <libxml/xmlschemas.h>
#endif

// define new SQL errors
#define ERRCODE_NOT_AN_DTD_DOCUMENT			MAKE_SQLSTATE('2', '2', '0', '0', 'U')
#define ERRCODE_NOT_AN_XSD_DOCUMENT			MAKE_SQLSTATE('2', '2', '0', '0', 'V')
#define ERRCODE_NOT_AN_RNG_DOCUMENT			MAKE_SQLSTATE('2', '2', '0', '0', 'W')


// some typedef, which could go into xml.h after acceptance
typedef struct varlena dtdtype;
typedef struct varlena xsdtype;
typedef struct varlena rngtype;

//////////////////////////////////////////////////////////////////////////////////
// Help macros
////////////////////////////////////////////////////////////////////////////////
// DTD
#define DatumGetDTDP(X)		((dtdtype *) PG_DETOAST_DATUM(X))
#define DTDPGetDatum(X)		PointerGetDatum(X)

#define PG_GETARG_DTD_P(n)	DatumGetDTDP(PG_GETARG_DATUM(n))
#define PG_RETURN_DTD_P(x)	PG_RETURN_POINTER(x)

// XSD
#define DatumGetXSDP(X)		((xsdtype *) PG_DETOAST_DATUM(X))
#define XSDPGetDatum(X)			PointerGetDatum(X)

#define PG_GETARG_XSD_P(n)	DatumGetXSDP(PG_GETARG_DATUM(n))
#define PG_RETURN_XSD_P(x)	PG_RETURN_POINTER(x)

// RNG
#define DatumGetRNGP(X)		((rngtype *) PG_DETOAST_DATUM(X))
#define RNGPGetDatum(X)			PointerGetDatum(X)

#define PG_GETARG_RNG_P(n)	DatumGetRNGP(PG_GETARG_DATUM(n))
#define PG_RETURN_RNG_P(x)	PG_RETURN_POINTER(x)

////////////////////////////////////////////////////////////////////////////////
// Declaration of IN/OUT functions
////////////////////////////////////////////////////////////////////////////////
Datum		dtd_in(PG_FUNCTION_ARGS);
Datum		dtd_out(PG_FUNCTION_ARGS);
Datum		xsd_in(PG_FUNCTION_ARGS);
Datum		xsd_out(PG_FUNCTION_ARGS);
Datum		rng_in(PG_FUNCTION_ARGS);
Datum		rng_out(PG_FUNCTION_ARGS);

////////////////////////////////////////////////////////////////////////////////
// Declaration I/O functions for SQL interface
////////////////////////////////////////////////////////////////////////////////
PG_FUNCTION_INFO_V1(dtd_in);
PG_FUNCTION_INFO_V1(dtd_out);
PG_FUNCTION_INFO_V1(xsd_in);
PG_FUNCTION_INFO_V1(xsd_out);
PG_FUNCTION_INFO_V1(rng_in);
PG_FUNCTION_INFO_V1(rng_out);



////////////////////////////////////////////////////////////////////////////////
// Input/Output functions
////////////////////////////////////////////////////////////////////////////////


/*
 * dtd_in uses a plain C string to VARDATA conversion, so for the time being
 * we use the conversion function for the text datatype.
 *
 * This is only acceptable so long as dtdtype and text use the same
 * representation.
 */
Datum
dtd_in(PG_FUNCTION_ARGS)
{
#ifdef USE_LIBXML
	char	   *s = PG_GETARG_CSTRING(0);
	dtdtype    *vardata;
	xmlDtdPtr       dtd         = NULL;

	//
	// Parse the data to check if it is well-formed XML data.  Assume that
	// ERROR occurred if parsing failed.
	//
    dtd = xmlIOParseDTD(NULL,
                xmlParserInputBufferCreateMem(s, strlen(s),
					     XML_CHAR_ENCODING_NONE),
						 XML_CHAR_ENCODING_NONE);

    if (dtd == NULL)
    { // schema itself is not valid
        ereport(ERROR,
				(errcode(ERRCODE_NOT_AN_DTD_DOCUMENT),
				 errmsg("invalid DTD content")));
		return -1;
    } else
	{ // schema is valid, store in DB
		vardata = (dtdtype *) cstring_to_text(s);
		xmlFreeDtd(dtd);
	}	

	PG_RETURN_DTD_P(vardata);
#else
	NO_XML_SUPPORT();
	return 0;
#endif
}

Datum
dtd_out(PG_FUNCTION_ARGS)
{
	PG_RETURN_CSTRING(text_to_cstring((text *) PG_GETARG_POINTER(0)));
}



/*
 * xsd_in uses a plain C string to VARDATA conversion, so for the time being
 * we use the conversion function for the text datatype.
 *
 * This is only acceptable so long as dtdtype and text use the same
 * representation.
 */
Datum
xsd_in(PG_FUNCTION_ARGS)
{
#ifdef USE_LIBXML
	char					*s			= PG_GETARG_CSTRING(0);
	xsdtype					*vardata	= (xsdtype *) cstring_to_text(s);
    xmlSchemaParserCtxtPtr  ctxt		= NULL;
    xmlSchemaPtr            schema		= NULL;


    ctxt = xmlSchemaNewMemParserCtxt(s, strlen(s));

    if (ctxt == NULL)
    { // unable to create parser context
		 ereport(ERROR,
				(errcode(ERRCODE_NOT_AN_XSD_DOCUMENT),
				 errmsg("Error during handling XSD document")));
        return -1;
    }

    schema = xmlSchemaParse(ctxt);  // parse schema

    xmlSchemaFreeParserCtxt(ctxt);  // realeas parser context

#ifdef DEBUG_VALID_XSD
    xmlSchemaDump(stdout, schema);
#endif

    if (schema == NULL)
    { // schema itself is not valid
		 ereport(ERROR,
				(errcode(ERRCODE_NOT_AN_XSD_DOCUMENT),
				 errmsg("Invalid XSD content")));
        return -1;
    }

	xmlSchemaFree(schema);
	PG_RETURN_XSD_P(vardata);
#else
	NO_XML_SUPPORT();
	return 0;
#endif
}

Datum
xsd_out(PG_FUNCTION_ARGS)
{
	PG_RETURN_CSTRING(text_to_cstring((text *) PG_GETARG_POINTER(0)));
}



/*
 * rng_in uses a plain C string to VARDATA conversion, so for the time being
 * we use the conversion function for the text datatype.
 *
 * This is only acceptable so long as dtdtype and text use the same
 * representation.
 */
Datum
rng_in(PG_FUNCTION_ARGS)
{
#ifdef USE_LIBXML
	char					*s			= PG_GETARG_CSTRING(0);
	rngtype					*vardata	= (rngtype *) cstring_to_text(s);
	xmlRelaxNGParserCtxtPtr ctxt    = NULL;
    xmlRelaxNGPtr           schema  = NULL;

    ctxt = xmlRelaxNGNewMemParserCtxt(s, strlen(s));

    if (ctxt == NULL)
    { // unable to create parser context
		 ereport(ERROR,
				(errcode(ERRCODE_NOT_AN_XSD_DOCUMENT),
				 errmsg("Error during RNG handling")));
        return -1;
    }

    schema = xmlRelaxNGParse(ctxt);  // parse schema

    xmlRelaxNGFreeParserCtxt(ctxt);  // realeas parser context

    if (schema == NULL)
    { // schema itself is not valid
		 ereport(ERROR,
				(errcode(ERRCODE_INVALID_XML_CONTENT),
				 errmsg("Invalid RNG content")));
        return -1;
    }

	 xmlRelaxNGFree(schema);
	PG_RETURN_RNG_P(vardata);
#else
	NO_XML_SUPPORT();
	return 0;
#endif
}

Datum
rng_out(PG_FUNCTION_ARGS)
{
	PG_RETURN_CSTRING(text_to_cstring((text *) PG_GETARG_POINTER(0)));
}