/**
 * file:	xmlindex.c
 * date:	2.4.2011
 * author:	Tomas Pospisil, xpospi04@stud.fit.vutbr.cz, killteck@seznam.cz
 * project:	Indexing native XML datatype as was proposed on tomaspospisil.com
 *
 * desc:	Model of index structure see http://www.tomaspospisil.com
 *

 * TODO: I will cut off my ears for stringbuilder created SQL commands
 *		create tests on HEAD git revision
 *		test if range index on attribute_table is needed
 */

#include "postgres.h"
#include "xml_index_loader.h"

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
#include "snowball/libstemmer/header.h"
#include <assert.h>

//Level of debugging we want to do, set equal to DEBUG
int debug_level = 4;

/* externally accessible functions */
Datum	build_xmlindex(PG_FUNCTION_ARGS);
Datum	create_xmlindex_tables(PG_FUNCTION_ARGS);

PG_FUNCTION_INFO_V1(build_xmlindex);
PG_FUNCTION_INFO_V1(create_xmlindex_tables);

/* ordinary internal (static) functions */
int4 insert_xmldata_into_table(xmltype* xmldata, text *name, bool insert_original);
bool create_indexes_on_tables(void);

/*
 * Internal function which add XML data into xml_documents_table and return
 * ID of just inserted data
 * @param xmldata
 * @param name name of XML document
 * @param insert_original indicate if user want to store original XML document as well
 * @return SQL int (value from serial sequence)
 */
int4
insert_xmldata_into_table(xmltype* xmldata, text *name, bool insert_original)
{
	int4 result = -1;	

	// for select result
	TupleDesc	tupdesc;
	HeapTuple	row;
	SPITupleTable	*tuptable;
	char			*rowIdStr;	// data are returned as string

	Oid oids[2];
	Datum data[2];

	oids[0] = TEXTOID;
	oids[1] = XMLOID;

	data[0] = PointerGetDatum(name);
	data[1] = XmlPGetDatum(xmldata);

	SPI_connect();

	if (insert_original)
	{ // insert original XML document to xml_documents_table
		if (SPI_execute_with_args("INSERT INTO xml_documents_table(name, value) VALUES ($1, $2)",
				2, oids, data, NULL, false, 1) != SPI_OK_INSERT)
		{ // insert
			ereport(ERROR,
					(errcode(ERRCODE_DATA_EXCEPTION),
					 errmsg("Can not insert values into xml_documents_table")));
		}
	} else
	{ // do not include original XML document
		if (SPI_execute_with_args("INSERT INTO xml_documents_table(name) VALUES ($1)",
				1, oids, data, NULL, false, 1) != SPI_OK_INSERT)
		{ // insert
			ereport(ERROR,
					(errcode(ERRCODE_DATA_EXCEPTION),
					 errmsg("Can not insert values into xml_documents_table")));
		}
	}

//	SPI_finish();	// works, but I'm not sure if it is not necessary
//	SPI_connect();

	if (SPI_execute("SELECT currval('xml_documents_table_did_seq')",
			true, 0) != SPI_OK_SELECT)
	{
		ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("Can not get ID of lastly inserted XML document")));
	}
	
	if (SPI_tuptable != NULL)
    {
		tupdesc = SPI_tuptable->tupdesc;
		tuptable = SPI_tuptable;
		row = tuptable->vals[0];

		rowIdStr = SPI_getvalue(row, tupdesc, 1);
		result = atoi(rowIdStr);
		
		if (debug_level > 1)
		{
			elog(INFO, "ID int of inserted row %d", result);
		}
	}

	SPI_finish();
	return result;
}

/*
 * Create indexes on shreded data
 * @return true if succed
 */
bool
create_indexes_on_tables(void)
{
	bool result = false;

	SPI_connect();

	if (SPI_execute("CREATE INDEX attr_tab_all_index ON attribute_table (name, did, pre_order); "					
					"CREATE INDEX did_tab_name_index ON xml_documents_table (name); "
					"CREATE INDEX elem_tab_all_index ON element_table (name, did, pre_order, size); "
					"CREATE INDEX elem_tab_range_index ON element_table USING gist (range(pre_order, (pre_order+size)));"
					"CREATE INDEX text_tab_index ON text_table (parent_id,did);"
					,
					false, 0) == SPI_ERROR_PARAM)
	{
		ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("Can not get ID of lastly inserted XML document")));
	}

	result = true;
	SPI_finish();

	return result;
}

/*
 * Create tables as storage of shreded data
 * @param none
 * @return true/false
 */
Datum
create_xmlindex_tables(PG_FUNCTION_ARGS)
{
	StringInfoData query;

	initStringInfo(&query);
	appendStringInfo(&query,
			"CREATE TABLE xml_documents_table "
							"(did serial not null, "
							"name text, "
							"value xml,"
							"xdb_sequence int default 0); "
			"CREATE TABLE attribute_table "
							"(name text, "
							"did int not null, "
							"pre_order int not null, "
							"size int not null, "
							"depth int, "
							"parent_id int, "
							"prev_id int, "
							"value text,"
							"PRIMARY KEY (did,pre_order)); "
			"CREATE TABLE element_table "
							"(name text, "
							"did int not null, "
							"pre_order int not null, "
							"size int not null, "
							"depth int, "
							"parent_id int, "
							"prev_id int, "
							"child_id int, "
							"attr_id int, "
							"PRIMARY KEY (did,pre_order,size));"
			"CREATE TABLE text_table "
							"(did int not null, "
							"pre_order int not null, "
							"depth int not null, "
							"parent_id int, "
							"prev_id int, "
							"value text, "
							"PRIMARY KEY  (pre_order, did));"
			);

	SPI_connect();

	if (SPI_execute(query.data, false, 0) == SPI_ERROR_ARGUMENT)
	{
		ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("invalid query")));
	}

// TODO add foreign key to xml_documents_table

	SPI_finish();

	create_indexes_on_tables();

	PG_RETURN_BOOL(true);
}

/*
 * Entry point for native XML support, shred XML document into tables and
 * create indexes for future XQuery support
 * @param
 * @return
 */
Datum
build_xmlindex(PG_FUNCTION_ARGS)
{

	xmltype		*xmldata	= NULL;
	text		*xml_name	= NULL;
	int			loader_return = 0;				// false
	int4		did;							// document ID
	bool		store_original_xml	= false;	// indication for INSERTs


#ifdef USE_LIBXML
	if (debug_level > 1)
	{
		elog(INFO, "build_xmlindex started");
	}

	xmldata		= PG_GETARG_XML_P(0);
	xml_name	= PG_GETARG_TEXT_P(1);

	if (!PG_ARGISNULL(2))
	{
		store_original_xml	= PG_GETARG_BOOL(2);
	}
	
	//initialize LibXML structures, if allready done -> do nothing
    pg_xml_init();
	xmlInitParser();

	did = insert_xmldata_into_table(xmldata, xml_name, store_original_xml);
	loader_return = xml_index_entry(
			DatumGetCString(DirectFunctionCall1(xml_out,
				XmlPGetDatum(xmldata))), 
			did);
	if (debug_level > 1)
	{
		elog(INFO, "build_xmlindex ended");
	}
	
	if (loader_return == XML_INDEX_LOADER_SUCCES)
	{
		PG_RETURN_BOOL(true);
	} else
	{
		PG_RETURN_BOOL(false);
	}
#else
    NO_XML_SUPPORT();
    PG_RETURN_BOOL (false);
#endif
}
