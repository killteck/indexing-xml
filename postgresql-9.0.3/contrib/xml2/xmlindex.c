////////////////////////////////////////////////////////////////////////////////
// author:	Tomas Pospisil, xpospi04@stud.fit.vutbr.cz, killteck@seznam.cz
// project:	Indexing native XML datatype as was proposed on tomaspospisil.com
// file:	xmlindex.c
// date:	2.4.2011
// desc:	Model of index structure
//
////////////////////////////////////////////////////////////////////////////////
#include "postgres.h"
#include "xml_index_loader.h"

//#define USE_LIBXML
#ifdef USE_LIBXML
	#include <libxml/chvalid.h>
	#include <libxml/parser.h>
	#include <libxml/tree.h>
	#include <libxml/uri.h>
	#include <libxml/xmlerror.h>
	#include <libxml/xmlwriter.h>
	#include <libxml/xpath.h>
	#include <libxml/xpathInternals.h>
	#include <libxml/xmlerror.h>
	#include <libxml/xmlreader.h>
#endif   /* USE_LIBXML */

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
#include <assert.h>


//Level of debugging we want to do, set equal to DEBUG
int debug_level;


/* externally accessible functions */

Datum	build_xmlindex(PG_FUNCTION_ARGS);
Datum	create_xmlindex_tables(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(build_xmlindex);
PG_FUNCTION_INFO_V1(create_xmlindex_tables);


Datum
create_xmlindex_tables(PG_FUNCTION_ARGS)
{
	StringInfoData query;

	elog(INFO, "create tables start");

	initStringInfo(&query);
	appendStringInfo(&query,
			"CREATE TABLE xml_documents_table "
							"(did serial not null, "
							"name text, "
							"value xml); "
			"CREATE TABLE attribute_table "
							"(name text, "
							"did int not null, "
							"nid int not null, "
							"size int not null, "
							"depth int, "
							"parent_id int, "
							"prev_id int, "
							"value text); "
			"CREATE TABLE element_table "
							"(name text, "
							"did int not null, "
							"nid int not null, "
							"size int, "
							"depth int, "
							"parent_id int, "
							"prev_id int, "
							"child_id int, "
							"attr_id int, "
							"PRIMARY KEY (did,nid));"
			"CREATE TABLE text_table "
							"(did int not null, "
							"nid int not null, "
							"depth int not null, "
							"parent_id int, "
							"prev_id int, "
							"value text, "
							"PRIMARY KEY  (nid, did));"
			);

	SPI_connect();

	if (SPI_execute(query.data, false, 0) == SPI_ERROR_ARGUMENT)
		ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("invalid query")));

	SPI_finish();


	elog(INFO, "create tables end");
	PG_RETURN_BOOL(true);
}

Datum
build_xmlindex(PG_FUNCTION_ARGS)
{
    xmltype     *xmldata    = NULL;
    char        *xmldataint = NULL;
	int			xmldatalen	= -1;
	int			loader_return;

#ifdef USE_LIBXML
	elog(INFO, "build_xmlindex started");

	xmldata     = PG_GETARG_XML_P(0);
    xmldataint  = VARDATA(xmldata);
	xmldatalen  = VARSIZE(xmldata) - VARHDRSZ;
	xmldataint[xmldatalen] = 0;
	
	elog(INFO, "data sended as argument %s, size %d", xmldataint, xmldatalen);
	//initialize LibXML structures, if allready done -> do nothing
    pg_xml_init();
	xmlInitParser();

	loader_return = xml_index_entry(xmldataint, xmldatalen);


	elog(INFO, "build_xmlindex ended");
	PG_RETURN_BOOL(true);
#else
    NO_XML_SUPPORT();
    PG_RETURN_BOOL (false);
#endif
}



