/**
 * file:	xmlindex.c
 * date:	2.4.2011
 * author:	Tomas Pospisil, xpospi04@stud.fit.vutbr.cz, killteck@seznam.cz
 * project:	Indexing native XML datatype as was proposed on tomaspospisil.com
 *
 * desc:	Model of index structure see http://www.tomaspospisil.com
 *
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
insert_xmldata_into_table(xmltype* xmldata, 
		text *name,
		bool insert_original)
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
		if (SPI_execute_with_args("INSERT INTO xml_documents(name, value) VALUES ($1, $2)",
				2, oids, data, NULL, false, 1) != SPI_OK_INSERT)
		{ // insert
			ereport(ERROR,
					(errcode(ERRCODE_DATA_EXCEPTION),
					 errmsg("Can not insert values into xml_documents")));
		}
	} else
	{ // do not include original XML document
		if (SPI_execute_with_args("INSERT INTO xml_documents(name) VALUES ($1)",
				1, oids, data, NULL, false, 1) != SPI_OK_INSERT)
		{ // insert
			ereport(ERROR,
					(errcode(ERRCODE_DATA_EXCEPTION),
					 errmsg("Can not insert values into xml_documents")));
		}
	}

	if (SPI_execute("SELECT currval('xml_documents_did_seq')",
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
				
		elog(DEBUG1, "ID int of inserted row %d", result);		
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

	if (SPI_execute("CREATE INDEX attr_tab_all_index ON xml_attribute_nodes (name, did, pre_order); "
					"CREATE INDEX did_tab_name_index ON xml_documents (name); "
					"CREATE INDEX elem_tab_all_index ON xml_element_nodes (name, did, pre_order, size); "
					"CREATE INDEX elem_tab_range_index ON xml_element_nodes USING gist (rangeii(pre_order, (pre_order+size)));"
					"CREATE INDEX text_tab_index ON xml_text_nodes (parent_id,did);"	// order is correct
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
			"CREATE TABLE xml_documents "
							"(did serial not null, "
							"name text, "
							"value xml,"
							"xdb_sequence int default 0,"
							"PRIMARY KEY (did)); "
			"CREATE TABLE xml_attribute_nodes "
							"(name text, "
							"did int not null, "
							"pre_order int not null, "
							"size int not null, "
							"depth int, "
							"parent_id int, "
							"prev_id int, "
							"value text,"
							"PRIMARY KEY (did,pre_order)); "
			"CREATE TABLE xml_element_nodes "
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
			"CREATE TABLE xml_text_nodes "
							"(did int not null, "
							"pre_order int not null, "
							"depth int not null, "
							"parent_id int, "
							"prev_id int, "
							"value text, "
							"PRIMARY KEY  (pre_order, did));"
			"ALTER TABLE xml_attribute_nodes ADD FOREIGN KEY (did) REFERENCES xml_documents;"
			"ALTER TABLE xml_element_nodes ADD FOREIGN KEY (did) REFERENCES xml_documents;"
			"ALTER TABLE xml_text_nodes ADD FOREIGN KEY (did) REFERENCES xml_documents;"
			"COMMENT ON TABLE xml_documents IS 'Description of XML document';"
			"COMMENT ON TABLE xml_element_nodes IS 'Elements encoded with pre order tree traversal';"
			"COMMENT ON TABLE xml_attribute_nodes IS 'Attributes for element';"
			"COMMENT ON TABLE xml_text_nodes IS 'Text values placed somewhere in XML document';"
			"COMMENT ON COLUMN xml_documents.did IS 'Document ID';"
			"COMMENT ON COLUMN xml_documents.value IS 'Place for original XML document';"
			"COMMENT ON COLUMN xml_documents.xdb_sequence IS 'ID of sequence, required by XDM defintion';"
			"COMMENT ON COLUMN xml_element_nodes.did IS 'Document ID';"
			"COMMENT ON COLUMN xml_element_nodes.name IS 'Name of element';"
			"COMMENT ON COLUMN xml_element_nodes.pre_order IS 'Pre order code by XML tree traverse';"
			"COMMENT ON COLUMN xml_element_nodes.size IS 'Number of elemenets/attributes/texts in subtree';"
			"COMMENT ON COLUMN xml_element_nodes.depth IS 'Level of element in tree, starting from 0';"
			"COMMENT ON COLUMN xml_element_nodes.parent_id IS 'Parent node ID';"
			"COMMENT ON COLUMN xml_element_nodes.prev_id IS 'ID of element on same level';"
			"COMMENT ON COLUMN xml_element_nodes.child_id IS 'ID of children element';"
			"COMMENT ON COLUMN xml_element_nodes.attr_id IS 'ID of first attribute';"
			"COMMENT ON COLUMN xml_attribute_nodes.did IS 'Document ID';"
			"COMMENT ON COLUMN xml_attribute_nodes.name IS 'Name of attribute';"
			"COMMENT ON COLUMN xml_attribute_nodes.pre_order IS 'Pre order code by XML tree traverse';"
			"COMMENT ON COLUMN xml_attribute_nodes.size IS 'Number of attributes in current element';"
			"COMMENT ON COLUMN xml_attribute_nodes.depth IS 'Level of element in tree, starting from 0';"
			"COMMENT ON COLUMN xml_attribute_nodes.parent_id IS 'Parent node ID';"
			"COMMENT ON COLUMN xml_attribute_nodes.prev_id IS 'ID of attribute in same element';"
			"COMMENT ON COLUMN xml_attribute_nodes.value IS 'Value stored in attribute';"
			"COMMENT ON COLUMN xml_text_nodes.did IS 'Document ID';"
			"COMMENT ON COLUMN xml_text_nodes.pre_order IS 'Pre order code by XML tree traverse';"
			"COMMENT ON COLUMN xml_text_nodes.depth IS 'Level of element in tree, starting from 0';"
			"COMMENT ON COLUMN xml_text_nodes.parent_id IS 'Parent node ID';"
			"COMMENT ON COLUMN xml_text_nodes.prev_id IS 'ID of element on same level';"
			"COMMENT ON COLUMN xml_text_nodes.value IS 'Value stored in text node';"
			);

	SPI_connect();

	if (SPI_execute(query.data, false, 0) == SPI_ERROR_ARGUMENT)
	{
		ereport(ERROR,
				(errcode(ERRCODE_DATA_EXCEPTION),
				 errmsg("invalid query")));
	}

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

	elog(DEBUG1, "build_xmlindex started");

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

	elog(DEBUG1, "build_xmlindex ended");
	
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
