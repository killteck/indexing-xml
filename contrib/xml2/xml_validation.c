////////////////////////////////////////////////////////////////////////////////
// author:	Tomas Pospisil, xpospi04@stud.fit.vutbr.cz, killteck@seznam.cz
// project:	Part of Master's Thesis
// file:	xml_validation.c
// date:	25.4.2011
// desc:	XML validation functions with support DTD, XSD, RNG schema
//
////////////////////////////////////////////////////////////////////////////////

/* postgresql includes */
#include "postgres.h"

#include "catalog/namespace.h"
#include "catalog/pg_type.h"
#include "commands/dbcommands.h"
#include "executor/executor.h"
#include "executor/spi.h"
#include "fmgr.h"
#include "lib/stringinfo.h"
#include "libpq/pqformat.h"
#include "mb/pg_wchar.h"
#include "miscadmin.h"
#include "nodes/execnodes.h"
#include "nodes/nodeFuncs.h"
#include "utils/array.h"
#include "utils/builtins.h"
#include "utils/date.h"
#include "utils/datetime.h"
#include "utils/lsyscache.h"
#include "utils/memutils.h"
#include "utils/syscache.h"
#include "utils/xml.h"


#ifdef USE_LIBXML
	#include <libxml/chvalid.h>
	#include <libxml/parser.h>
	#include <libxml/tree.h>
	#include <libxml/uri.h>
	#include <libxml/xmlerror.h>
	#include <libxml/xmlwriter.h>
	#include <libxml/xpath.h>
	#include <libxml/xpathInternals.h>
	#include <libxml/xmlreader.h>
	#include <libxml/relaxng.h>

	// global error handling buffer
	static StringInfo xml_error_buf = NULL;
#endif   /* USE_LIBXML */

// define new SQL errors
//#define ERRCODE_NOT_AN_DTD_DOCUMENT			MAKE_SQLSTATE('2', '2', '0', '0', 'U')

////////////////////////////////////////////////////////////////////////////////
// Declaration functions for SQL interface
////////////////////////////////////////////////////////////////////////////////
PG_FUNCTION_INFO_V1 (xmlvalidate_dtd);
PG_FUNCTION_INFO_V1 (xmlvalidate_xsd);
PG_FUNCTION_INFO_V1 (xmlvalidate_rng);

//////////////////////////////////////////////////////////
// externaly accessable function headers
//////////////////////////////////////////////////////////
extern Datum xmlvalidate_xsd(PG_FUNCTION_ARGS);
extern Datum xmlvalidate_rng(PG_FUNCTION_ARGS);
extern Datum xmlvalidate_dtd(PG_FUNCTION_ARGS);

// internal helper function for translating LibXML error messages to
// SQL errors
static void xml_error_handler(void *ctxt, const char *msg,...);

/*
 * Error handler for libxml error messages
 */
static void
xml_error_handler(void *ctxt, const char *msg,...)
{
	/* Append the formatted text to xml_err_buf */
	for (;;)
	{
		va_list		args;
		bool		success;

		/* Try to format the data. */
		va_start(args, msg);
		success = appendStringInfoVA(xml_error_buf, msg, args);
		va_end(args);

		if (success)
			break;

		/* Double the buffer size and try again. */
		enlargeStringInfo(xml_error_buf, xml_error_buf->maxlen);
	}
}

/**
 * Validation function take first argument as XML type, then check if is
 * well formated. If so, do the same for XSD document. If both success, check
 * XML document against XSD schema restrictions.
 * @return true if pass, false otherwise
 */
Datum
xmlvalidate_xsd(PG_FUNCTION_ARGS)
{
#ifdef USE_LIBXML

	text        *data   = NULL;
	unsigned char   *xsd    = NULL;
	xmlChar     *utf8xsd    = NULL;
	xmltype     *xmldata    = NULL;
	char        *xmldataint = NULL;
	xmlChar     *xmldatastr = NULL;
	bool        result  = false;
	int         lenxml  = -1;       // length of xml data
	int         lenxsd  = -1;       // length of xsd data
	xmlDocPtr               doc = NULL;
	int ret = -1;

	xmlSchemaParserCtxtPtr  ctxt    = NULL;
	xmlSchemaPtr            schema  = NULL;
	xmlSchemaValidCtxtPtr   validctxt = NULL;


    // creating xmlChar * from internal XML type
    xmldata     = PG_GETARG_XML_P(0);
    xmldataint  = VARDATA(xmldata);
    lenxml      = VARSIZE(xmldata) - VARHDRSZ;
    xmldatastr  = (xmlChar *) palloc((lenxml + 1) * sizeof(xmlChar));
    memcpy(xmldatastr, xmldataint, lenxml);
    xmldatastr[lenxml] = '\0';

    // creating xmlChar* from text representation of XSD
    data = PG_GETARG_TEXT_P(1);
    lenxsd = VARSIZE(data) - VARHDRSZ;
    xsd = (unsigned char*)text_to_cstring(data);

    //encode XML to internal representation with UTF-8, only one used in LibXML
	utf8xsd = pg_do_encoding_conversion(xsd,
										   lenxsd,
										   GetDatabaseEncoding(),
										   PG_UTF8);

    //initialize LibXML structures, if allready done -> do nothing
    pg_xml_init();
	xmlInitParser();

    doc = xmlReadMemory((const char*)xmldatastr, lenxml, "include.xml", NULL, 0);

     if (doc == NULL) {
		 ereport(ERROR,
				(errcode(ERRCODE_INVALID_XML_DOCUMENT),
				 errmsg("Failed to parse XML document")));
        PG_RETURN_BOOL (false);
    }

    ctxt = xmlSchemaNewMemParserCtxt((const char*)xsd, lenxsd);

    if (ctxt == NULL)
    { // unable to create parser context
		ereport(ERROR,
				(errcode(ERRCODE_INVALID_XML_DOCUMENT),
				 errmsg("Error with creating schema, check if XSD schema is valid")));
        PG_RETURN_BOOL (false);
    }

    schema = xmlSchemaParse(ctxt);  // parse schema
    xmlSchemaFreeParserCtxt(ctxt);  // realease parser context

    validctxt = xmlSchemaNewValidCtxt(schema);
    if (validctxt == NULL)
    { // cant create validation context
        xmlSchemaFree(schema);
        elog(ERROR, "Cant create validation context");
        PG_RETURN_BOOL (false);
    }

    // set errors to SQL errors
	xmlSchemaSetValidErrors(validctxt,
			    xml_error_handler,
			    NULL,
			    stderr);


    ret = xmlSchemaValidateDoc(validctxt, doc);
    if (ret == 0)
    {
        elog(INFO, "Validates");
        result = true;
    } else if (ret > 0)
    {
        elog(INFO, "Dont validates");
        result = false;
    } else
    {
        elog(INFO, "Validation generated an internal error");
        result = false;
    }

    xmlSchemaFree(schema);
    xmlSchemaFreeValidCtxt(validctxt);


    xmlFreeDoc(doc);    // clean up document in memmory
    xmlCleanupParser(); // clean up stream parser


	PG_RETURN_BOOL (result);
#else
    NO_XML_SUPPORT();
    PG_RETURN_BOOL (false);
#endif
}

/**
 * Validation function take first argument as XML type, then check if is
 * well formated. If so, do the same for RNG document. If both success, check
 * XML document against RNG schema restrictions.
 * @return true if pass, false otherwise
 */
Datum
xmlvalidate_rng(PG_FUNCTION_ARGS)
{
#ifdef USE_LIBXML

    text        *data   = NULL;
	char        *rng    = NULL;
	xmlChar     *utf8rng    = NULL;
    xmltype     *xmldata    = NULL;
    char        *xmldataint = NULL;
    xmlChar     *xmldatastr = NULL;
    bool        result  = false;
    int         lenxml  = -1;       // length of xml data
    int         lenrng  = -1;       // length of xsd data
    xmlDocPtr               doc = NULL;
    int ret = -1;

    xmlRelaxNGParserCtxtPtr ctxt    = NULL;
    xmlRelaxNGPtr           schema  = NULL;
    xmlRelaxNGValidCtxtPtr   validctxt = NULL;


    // creating xmlChar * from internal xmltype of stored XML
    xmldata     = PG_GETARG_XML_P(0);
    xmldataint  = VARDATA(xmldata);
    lenxml      = VARSIZE(xmldata) - VARHDRSZ;
    xmldatastr  = (xmlChar *) palloc((lenxml + 1) * sizeof(xmlChar));
	memcpy(xmldatastr, xmldataint, lenxml);
	xmldatastr[lenxml] = '\0';

    // creating xmlChar* from text representation of XSD
    data = PG_GETARG_TEXT_P(1);
	lenrng = VARSIZE(data) - VARHDRSZ;
	rng = text_to_cstring(data);

    //encode XML to internal representation with UTF-8, only one used in LibXML
	utf8rng = pg_do_encoding_conversion((unsigned char*)rng,
										   lenrng,
										   GetDatabaseEncoding(),
										   PG_UTF8);

    //initialize LibXML structures, if allready done -> do nothing
    pg_xml_init();
	xmlInitParser();

    doc = xmlReadMemory((const char *)xmldatastr, lenxml, "include.xml", NULL, 0);

     if (doc == NULL) {
        elog(ERROR, "Failed to parse XML document");
        PG_RETURN_BOOL (false);
    }

    ctxt = xmlRelaxNGNewMemParserCtxt(rng, lenrng);

    if (ctxt == NULL)
    { // unable to create parser context
        elog(ERROR, "Error with creating schema, check if RelaxNG schema is valid");
        PG_RETURN_BOOL (false);
    }

    schema = xmlRelaxNGParse(ctxt);  // parse schema
    xmlRelaxNGFreeParserCtxt(ctxt);  // realease parser context

    validctxt = xmlRelaxNGNewValidCtxt(schema);
    if (validctxt == NULL)
    { // cant create validation context
        xmlRelaxNGFree(schema);
        elog(ERROR, "Cant create validation context");
        PG_RETURN_BOOL (false);
    }

    // set errors to SQL errors
	xmlRelaxNGSetValidErrors(validctxt,
			    xml_error_handler,
			    NULL,
			    0);

    ret = xmlRelaxNGValidateDoc(validctxt, doc);
    if (ret == 0)
    {
        elog(INFO, "Validates");
        result = true;
    } else if (ret > 0)
    {
        elog(INFO, "Dont validates");
        result = false;
    } else
    {
        elog(INFO, "Validation generated an internal error");
        result = false;
    }

    xmlRelaxNGFree(schema);
    xmlRelaxNGFreeValidCtxt(validctxt);

    xmlFreeDoc(doc);    // clean up document in memmory
    xmlCleanupParser(); // clean up stream parser

	PG_RETURN_BOOL (result);
#else
    NO_XML_SUPPORT();
    PG_RETURN_BOOL (false);
#endif
}

/**
 * Validation function take first argument as XML type, then check if is
 * well formated. If so, do the same for DTD document. If both success, check
 * XML document against DTD schema restrictions.
 * @return true if pass, false otherwise
 */
Datum xmlvalidate_dtd(PG_FUNCTION_ARGS)
{
#ifdef USE_LIBXML
	int ret = -1;
	xmlValidCtxtPtr validctxt   = NULL;
	xmlDtdPtr       dtd         = NULL;
    bool			result		= false;
    text			*data		= NULL;
	char			*dtdstr		= NULL;
    xmltype			*xmldata	= NULL;
    char			*xmldataint = NULL;
    xmlChar			*xmldatastr = NULL;
    int				lenxml  = -1;       // length of xml data
    xmlDocPtr               doc = NULL;

	 // creating xmlChar * from internal xmltype of stored XML
    xmldata     = PG_GETARG_XML_P(0);
    xmldataint  = VARDATA(xmldata);
    lenxml      = VARSIZE(xmldata) - VARHDRSZ;
    xmldatastr  = (xmlChar *) palloc((lenxml + 1) * sizeof(xmlChar));
	memcpy(xmldatastr, xmldataint, lenxml);
	xmldatastr[lenxml] = '\0';

    // creating xmlChar* from text representation of DTD
    data = PG_GETARG_TEXT_P(1);
	dtdstr = text_to_cstring(data);

    //initialize LibXML structures, if allready done -> do nothing
    pg_xml_init();
	xmlInitParser();

    doc = xmlReadMemory((const char *)xmldatastr, lenxml, "include.xml", NULL, 0);

     if (doc == NULL) {
        elog(ERROR, "Failed to parse XML document");
        PG_RETURN_BOOL (false);
    }

	// create DTD from memory, must use XML_CHAR_ENCODING_NONE 
	dtd = xmlIOParseDTD(NULL,
				xmlParserInputBufferCreateMem(dtdstr, strlen(dtdstr),
						 XML_CHAR_ENCODING_NONE),
				XML_CHAR_ENCODING_NONE);

    if (dtd == NULL)
    { // unable to create parser context
        elog(ERROR, "Error with creating DTD schema, check if schema is valid");
        PG_RETURN_BOOL (false);
    }

	validctxt = xmlNewValidCtxt();
	if (validctxt == NULL)
	{ // cant create validation context
		elog(INFO ,"cant create validation context");
		xmlFreeDtd(dtd);
		PG_RETURN_BOOL (false);
	}

	ret = xmlValidateDtd(validctxt, doc, dtd);
	if (ret == 0)
    {
        elog(INFO, "Validates");
        result = true;
    } else if (ret > 0)
    {
        elog(INFO, "Dont validates");
        result = false;
    } else
    {
        elog(INFO, "Validation generated an internal error");
        result = false;
    }

	xmlFreeDtd(dtd);
	xmlFreeValidCtxt(validctxt);

	xmlFreeDoc(doc);    // clean up document in memmory
    xmlCleanupParser(); // clean up stream parser
	PG_RETURN_BOOL (result);
#else
    NO_XML_SUPPORT();
    PG_RETURN_BOOL (false);
#endif
	}