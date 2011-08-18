/**
 * File:   xml_index_loader.c
 * Authors: Philip Harding (harding@cs.arizona.edu)
 * XISS/R project : University of Arizona, Computer Science Department
 * http://www.xiss.cs.arizona.edu
 *
 * Description: Implementation of XISS/R in PostgreSQL by Tomas Pospisil
 * Ideas are based on Philip Hardings algorithm with PostgreSQL needs
 * and specific memory menagement
 * http://www.tomaspospisil.com
 *
 * TODO: set index to proper value
 * test external entities as well as error handlings
 * precisly describe parameters
 * add missing erro handling for other types of errors
 */

#include "postgres.h"
#include "xml_index_loader.h"

#include <stdio.h>
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


//Buffers
element_node		element_node_buffer[BUFFER_SIZE];
text_node			text_node_buffer[BUFFER_SIZE];
attribute_node		attribute_node_buffer[BUFFER_SIZE];

/**
 * Entry point of loader
 * @param xml_document
 * @return 0 if all XML shredding is ok, otherwise negative values
 */
int extern
xml_index_entry(const char *xml_document,
		int4 did)
{
	xml_index_globals		globals;
	int preorder_result;
	int length			=	strlen(xml_document);

	//globals.reader
	xmlTextReaderPtr reader		= xmlReaderForMemory(xml_document, length,
			NULL,NULL, 0);

	if (reader == NULL)
	{ // error with loading
		elog(DEBUG1, "Problem with libXML in memory loading of XML document");
		return LIBXML_ERR;
    }

	init_values(&globals);
	globals.global_doc_id = did;

	xmlTextReaderRead(reader);
	// parse and compute whole shredding
	preorder_result = preorder_traverse(NO_VALUE, NO_VALUE, reader, &globals);

	xmlFreeTextReader(reader);    // clean up document in memmory

	flush_element_node_buffer(&globals);
	flush_attribute_node_buffer(&globals);
	flush_text_node_buffer(&globals);

	return XML_INDEX_LOADER_SUCCES;
}

/**
 * Initialize global values
 * @param globals variables used for global handling
 */
void
init_values(xml_index_globals_ptr globals)
{
	globals->global_order					= 0;
	globals->element_node_count				= 0;
	globals->element_node_buffer_count		= 0;
	globals->attribute_node_count			= 0;
	globals->attribute_node_buffer_count	= 0;
	globals->text_node_count				= 0;
	globals->text_node_buffer_count			= 0;
}


/**
 * Read the next node (move the stream to the next node) and return an error code if neccesary.
 * @param reader LibXML stream reader pointer
 * @param globals variables used for global handling
 * @return err value return by xmlTextReaderRead (1 if exist another nodes)
 */
int 
read_next_node(xmlTextReaderPtr reader,
		xml_index_globals_ptr globals)
{
	int err_val = xmlTextReaderRead(reader);
	elog(DEBUG1, "reading next node");

//TODO better handling
	if(err_val == LIBXML_ERR)
	{
		elog(DEBUG1, "Error reading node, at order = %d", globals->global_order);
	}
	
	return err_val;
}

/**
 * Clears the next element record in the element buffer and may flush the buffer
 * and reset the index if the buffer is full.
 * @param globals variables used for global handling
 * @return Returns the index to the buffer for the "new" element
 */
int 
create_new_element(xml_index_globals_ptr globals)
{

	int my_ind;

	if(globals->element_node_buffer_count >= BUFFER_SIZE)
	{
		//flush BUFFER reset buffer_count
		flush_element_node_buffer(globals);
		globals->element_node_buffer_count = 0;
	}

	my_ind = globals->element_node_buffer_count;

	elog(DEBUG1, ">> creating new element at index: %d", my_ind);

	element_node_buffer[my_ind].did = NO_VALUE;
	element_node_buffer[my_ind].order = NO_VALUE;
	element_node_buffer[my_ind].size = NO_VALUE;
	
	if((FREE == TRUE) && (element_node_buffer[my_ind].tag_name != NULL) &&
			(globals->element_node_buffer_count > BUFFER_SIZE))
	{
		free(element_node_buffer[my_ind].tag_name);
	}

	element_node_buffer[my_ind].tag_name = NULL;
	element_node_buffer[my_ind].depth = NO_VALUE;
	element_node_buffer[my_ind].child_id = NO_VALUE;
	element_node_buffer[my_ind].prev_id = NO_VALUE;
	element_node_buffer[my_ind].first_attr_id = NO_VALUE;
	globals->element_node_buffer_count++;


	globals->element_node_count++;
	return my_ind;

}

/**
 * Executes a preorder traversal of the document tree. It processes elements(and
 * in turn attributes and text elements)
 * @param parent_id parent node's order
 * @param sibling_id Order of this node's nearest sibling
 * @param reader pointer to LibXML stream reader
 * @param globals variables used for global handling
 * @return
 */
int 
preorder_traverse(int parent_id, 
		int sibling_id,
		xmlTextReaderPtr reader,
		xml_index_globals_ptr globals)
{

	int my_ind;
	int size_res;
	int prev_child = NO_VALUE;
	int recent_child = NO_VALUE;
	int err_val;
	int node_type;
	xmlChar* my_tag_name;

	//this elements xiss values
	int my_order = -1,
		 my_size = -1,
		 my_depth = -1,
		 my_first_attr_id = -1;

	//Get Order, and Size
	my_order = ++(globals->global_order);
	my_size = 0;

	//get the name of the node
	my_tag_name = xmlTextReaderName(reader);

	//Get Depth
	my_depth = xmlTextReaderDepth(reader);

	elog(DEBUG1, "--PREORDER-- Parsing %d:%s at depth %d\n", my_order, my_tag_name, my_depth);

	//Process all attributes
	size_res = process_attributes(my_order, reader, globals);

	my_size += size_res;
	if(size_res > 0)
	{
		my_first_attr_id = my_order + size_res;
	}

	//Check whether next node is empty
	if(xmlTextReaderIsEmptyElement(reader) == 1)
	{
		elog(INFO, "Node %d:%s at depth %d, does not have a closing tag.\n", my_order, my_tag_name, my_depth);
		//Possibly implement error code here
	}

	//Get next element or text node
	err_val = read_next_node(reader, globals);

	if(err_val == 0)
	{
		elog(INFO, "Malformed XML: reached end of document without reaching the end tag for the current element\n");
		return(LIBXML_ERR);
	}

	//Type of next node
	node_type = xmlTextReaderNodeType(reader);

	if(node_type == XML_READER_TYPE_END_ELEMENT)
	{
		elog(INFO, "Found end of %d:%s with no non-attribute children at depth %d returning to %d.\n",my_order, my_tag_name,  my_depth, parent_id);
	}

	while(node_type != XML_READER_TYPE_END_ELEMENT)  //While we have unvisited children
	{
		elog(DEBUG1, "while (node_type != ELEMENT_END)");

		switch(node_type)
		{
			case XML_READER_TYPE_TEXT:
			case XML_READER_TYPE_CDATA: //Visit text nodes
				err_val = process_text_node(my_order, prev_child, reader, globals);
				if(err_val == REAL_TEXT_NODE)
				{
					my_size++;
				}
				break;
			case XML_READER_TYPE_ELEMENT: //Recurse on elements
				recent_child = (globals->global_order) + 1; //Next time we have a child it will know this as its nearest sibling
				size_res = preorder_traverse(my_order,  prev_child, reader, globals);
				my_size += size_res;
				prev_child = recent_child;
				if(my_depth == xmlTextReaderDepth(reader) && xmlTextReaderNodeType(reader) == XML_READER_TYPE_END_ELEMENT)
				{
					elog(ERROR, "Node %d:%s at depth %d is done, its child has no closing tag.\n", my_order, my_tag_name, my_depth);
					return(LIBXML_ERR);
				}
				break;
			case XML_READER_TYPE_WHITESPACE:
			case XML_READER_TYPE_NONE:
			case XML_READER_TYPE_SIGNIFICANT_WHITESPACE:	// do nothing with white spaces
				elog(DEBUG1, "no significant element");
				break;
			
			case XML_READER_TYPE_ENTITY_REFERENCE:
			case XML_READER_TYPE_ENTITY:
			case XML_READER_TYPE_PROCESSING_INSTRUCTION:
			case XML_READER_TYPE_COMMENT:
			case XML_READER_TYPE_DOCUMENT:
			case XML_READER_TYPE_DOCUMENT_TYPE:
			case XML_READER_TYPE_DOCUMENT_FRAGMENT:
			case XML_READER_TYPE_NOTATION:
			case XML_READER_TYPE_END_ENTITY:
			case XML_READER_TYPE_XML_DECLARATION:
			default:
				elog(ERROR, "Encounted an node of type %d where it should not be\n", node_type);
				return(LIBXML_ERR);
				//Possibly implement error handling code here
				break;
		}

		err_val = read_next_node(reader, globals);
		if(err_val == 0)
		{
			elog(ERROR, "Malformed XML: reached end of document without reaching "
					"the end tag for the current element\n");
			return(LIBXML_ERR);
		}

		node_type = xmlTextReaderNodeType(reader);
		if(node_type == XML_READER_TYPE_END_ELEMENT)
		{
			elog(DEBUG1,"Found end of %d:%s with %d children at depth %d returning to %d.\n",
					my_order, my_tag_name, my_size, my_depth, parent_id );
		}

		if(my_depth >= xmlTextReaderDepth(reader))
		{
			elog(DEBUG1,"Node %d:%s with %d children at depth %d has no end "
						"tag, now returning to %d\n",my_order, my_tag_name,
						my_size, my_depth, parent_id );		
			break;
		}
	}
	//We have visited each child

	//Create new queue entry for this element, initialized with null or no_value entries
	my_ind = create_new_element(globals);

	element_node_buffer[my_ind].did = globals->global_doc_id;
	element_node_buffer[my_ind].order = my_order;
	element_node_buffer[my_ind].size = my_size;
	element_node_buffer[my_ind].depth = my_depth;
	element_node_buffer[my_ind].first_attr_id = my_first_attr_id;
	element_node_buffer[my_ind].child_id = recent_child;
	element_node_buffer[my_ind].parent_id = parent_id;

	//Tag name
	if(my_tag_name == NULL && my_order == 1  && parent_id == NO_VALUE)
	{
		element_node_buffer[my_ind].tag_name = (char*) strdup(DOCUMENT_ROOT);
		//error
	}
	else
	{
		element_node_buffer[my_ind].tag_name = (char *)my_tag_name;//(char*)strdup(my_tag_name);
	//	if(FREE == TRUE)
	//	{
	//		free(my_tag_name);
	//	}
	}


	elog(DEBUG1, "== CREATE == element[%d] values did:%d, order:%d, size:%d "
				"depth:%d, first_attr_id:%d, , child_id:%d, parent_id:%d",
				my_ind,
				element_node_buffer[my_ind].did,
				element_node_buffer[my_ind].order,
				element_node_buffer[my_ind].size,
				element_node_buffer[my_ind].depth,
				element_node_buffer[my_ind].first_attr_id,
				element_node_buffer[my_ind].child_id,
				element_node_buffer[my_ind].parent_id);	
	
	//Get Previous Sibling
	if(sibling_id != NO_VALUE)
	{
		element_node_buffer[my_ind].prev_id = sibling_id;
	}

	return element_node_buffer[my_ind].size + 1;
}

/**
 * Creates new records and fills records for all attributes that are children of
 * the element with <parent_id>. When called, the current item in the XMl doc
 * stream is the element that is parent of these attributes.
 * @param parent_id element primary key
 * @param reader pointer to LibXML stream reader
 * @param globals variables used for global handling
 * @return  Returns the number of attributes processed.
 */
int 
process_attributes(int parent_id,
		xmlTextReaderPtr reader,
		xml_index_globals_ptr globals)
{
	int i, my_ind, err;
	int num_attributes;
	xmlChar * text;
	int last_attr = NO_VALUE;

	num_attributes = xmlTextReaderAttributeCount(reader);
	elog(DEBUG1, "processing attributes it is there %d", num_attributes);	

	if(num_attributes == LIBXML_ERR || num_attributes == 0)
	{
		return 0;
	}

	i = 0;
	for(; i < num_attributes; i++)
	{
		err = xmlTextReaderMoveToAttributeNo(reader, i);
		if(err != LIBXML_SUCCESS)
		{
			elog(DEBUG1,"process_attributes found error");
			//error
			i--;
			break;
		}
		
		my_ind = create_new_attribute(globals);
		attribute_node_buffer[my_ind].did = globals->global_doc_id;
		attribute_node_buffer[my_ind].order = ++(globals->global_order);
		attribute_node_buffer[my_ind].size = 0;
		text = xmlTextReaderName(reader);
		attribute_node_buffer[my_ind].tag_name = (char *)text;
		//if(FREE == TRUE)
		//{
		//	xmlFree(text);
		//}
		attribute_node_buffer[my_ind].depth = xmlTextReaderDepth(reader);
		if(attribute_node_buffer[my_ind].depth == -1)
		{
			//Possible place to implement error handling code
			elog(DEBUG1,"LIBXML_SUCCESS not found error");
			exit(LIBXML_ATTRIBUTE_ERROR);
		}
		attribute_node_buffer[my_ind].parent_id = parent_id;
		attribute_node_buffer[my_ind].prev_id = last_attr;

		err = xmlTextReaderReadAttributeValue(reader);
		if(err == LIBXML_SUCCESS)
		{
			attribute_node_buffer[my_ind].value = (char *)xmlTextReaderValue(reader);
		}

		elog(DEBUG1, "== CREATE == attribute[%d] values depth:%d, did:%d, order:%d, parent_id:%d, "
					"prev_id:%d, size:%d, att_name:%s, value:%s", my_ind,
					attribute_node_buffer[my_ind].depth,
					attribute_node_buffer[my_ind].did,
					attribute_node_buffer[my_ind].order,
					attribute_node_buffer[my_ind].parent_id,
					attribute_node_buffer[my_ind].prev_id,
					attribute_node_buffer[my_ind].size,
					attribute_node_buffer[my_ind].tag_name,
					attribute_node_buffer[my_ind].value);

		last_attr = attribute_node_buffer[my_ind].order;
		err = xmlTextReaderMoveToElement(reader);
		if(err == LIBXML_ERR || err == LIBXML_NO_EFFECT)
		{
			//Possible place to implement error handling code
			exit(LIBXML_ATTRIBUTE_ERROR);
		}
		err = xmlTextReaderMoveToNextAttribute(reader);
		if(err == LIBXML_ERR)
		{
			//Possible place to implement error handling code
			exit(LIBXML_ATTRIBUTE_ERROR);
		}		 
	}
	return i;
}

/**
 * Clears the next text record in the text buffer and may flush the buffer and
 * reset the index if the buffer is full.
 * @param globals variables used for global handling
 * @return the index to the buffer for the "new" text node
 */
int
create_new_text_node(xml_index_globals_ptr globals)
{
	int my_ind;

	if(globals->text_node_buffer_count >= BUFFER_SIZE)
	{
		//flush buffer reset buffer_count
		flush_text_node_buffer(globals);
		globals->text_node_buffer_count = 0;
	}

	my_ind = globals->text_node_buffer_count;
	elog(DEBUG1, "creating new text node at index: %d", my_ind);

	text_node_buffer[my_ind].did = NO_VALUE;
	text_node_buffer[my_ind].order = NO_VALUE;
	text_node_buffer[my_ind].size = NO_VALUE;
	text_node_buffer[my_ind].depth = NO_VALUE;
	text_node_buffer[my_ind].parent_id = NO_VALUE;
	text_node_buffer[my_ind].prev_id = NO_VALUE;

	if((FREE == TRUE) && (text_node_buffer[my_ind].value != NULL) &&
			(globals->text_node_buffer_count > BUFFER_SIZE))
	{
		free(text_node_buffer[my_ind].value);
	}

	text_node_buffer[my_ind].value = NULL;
	(globals->text_node_buffer_count)++;

	(globals->text_node_count)++;

	return my_ind;
}

/**
 * Clears the next attribute record in the attribute buffer and may Flush the
 * Buffer and reset the index if the buffer is full.
 * @param globals variables used for global handling
 * @return the index to the buffer for the "new" attribute
 */
int
create_new_attribute(xml_index_globals_ptr globals)
{
	int my_ind;						 //

	if(globals->attribute_node_buffer_count >= BUFFER_SIZE)  //If the buffer is full, flush it
	{
		//flush buffer; reset buffer_count;
		flush_attribute_node_buffer(globals);
		globals->attribute_node_buffer_count = 0;
	}
	
	my_ind = globals->attribute_node_buffer_count;
	elog(DEBUG1, "creating new attribute at index: %d", my_ind);	

	attribute_node_buffer[my_ind].did = NO_VALUE;
	attribute_node_buffer[my_ind].order = NO_VALUE;
	attribute_node_buffer[my_ind].size = NO_VALUE;
	if(FREE == TRUE && attribute_node_buffer[my_ind].tag_name != NULL &&
			globals->attribute_node_buffer_count > BUFFER_SIZE)
	{
		free(attribute_node_buffer[my_ind].tag_name);
	}
	attribute_node_buffer[my_ind].tag_name = NULL;
	attribute_node_buffer[my_ind].depth = NO_VALUE;
	attribute_node_buffer[my_ind].parent_id = NO_VALUE;
	attribute_node_buffer[my_ind].prev_id = NO_VALUE;
	if(FREE == TRUE && attribute_node_buffer[my_ind].value != NULL &&
			globals->attribute_node_buffer_count > BUFFER_SIZE)
	{
		free(attribute_node_buffer[my_ind].value);
	}
	attribute_node_buffer[my_ind].value = NULL;
	globals->attribute_node_buffer_count++;

	globals->attribute_node_count++;
	return my_ind;
}

/**
 * Called when parsing an element if it has a text node. Current item in XMl doc
 * stream is the element that is the parent of this text node
 * @param parent_id element primary key
 * @param prev_id previous text node, sibling
 * @param reader pointer to LibXML stream reader
 * @param globals variables used for global handling
 * @return
 */
int
process_text_node(int parent_id, 
		int prev_id,
		xmlTextReaderPtr reader,
		xml_index_globals_ptr globals)
{
	int my_ind;

	char* value = get_text_from_node(reader);

	// If the text node is nothing but white space, returning a value of FAKE_TEXT_NODE
	// will cause this text node to be ignored.  Disable this if statement if you want
	// to include text nodes that are only white space.
	if(is_all_whitespace(value))
	{
		if(FREE == TRUE)
		{
			free(value);
		}
		return FAKE_TEXT_NODE;
	}

	my_ind = create_new_text_node(globals);

	text_node_buffer[my_ind].did = globals->global_doc_id;
	text_node_buffer[my_ind].order = ++(globals->global_order);
	text_node_buffer[my_ind].size = 0;

	text_node_buffer[my_ind].depth = xmlTextReaderDepth(reader);
	if(text_node_buffer[my_ind].depth == -1)
	{
		//Implement possible error handling code here
	}
	text_node_buffer[my_ind].prev_id = prev_id;
	text_node_buffer[my_ind].parent_id = parent_id;

	 //Replace any characters that the DBMS has problems with.
	text_node_buffer[my_ind].value = value;
	elog(DEBUG1, "== CREATE == text node[%d] depth:%d, did:%d, order:%d, parent_id:%d, "
			"prev_id:%d, size:%d, value:%s", my_ind,
			text_node_buffer[my_ind].depth,
			text_node_buffer[my_ind].did,
			text_node_buffer[my_ind].order,
			text_node_buffer[my_ind].parent_id,
			text_node_buffer[my_ind].prev_id,
			text_node_buffer[my_ind].size,
			text_node_buffer[my_ind].value);
	return REAL_TEXT_NODE;
}

/**
 * Retrieves the proper text from a text node
 * @param reader pointer to LibXML stream reader
 * @return text data between <tag> some text </tag>, with white spaces
 */
char*
get_text_from_node(xmlTextReaderPtr reader)
{
	int node_type;
	char * text;
	char * text2;

	// called when we want to check and make sure we have text for an element.
	// returns NULL if an error is encountered or the value otherwise.
	if(xmlTextReaderIsEmptyElement(reader))
	{
		return NULL;
	}

	node_type = xmlTextReaderNodeType(reader);
	if(node_type !=  XML_READER_TYPE_TEXT)
	{
		if(node_type == XML_READER_TYPE_CDATA)  //If we have CDATA
		{
			if(xmlTextReaderIsEmptyElement(reader))
			{
				return NULL;
			}
			text = (char *)xmlTextReaderValue(reader);
			text2 = (char*) malloc(strlen(text) + 1 + 10);
			//One plus the length of the string + the length of ![[CDATA]]
			sprintf(text2, "![CDATA[%s]]", text);
			if(FREE == TRUE)
			{
				free(text);
			}
			return text2;
		}
		return NULL;
	}
	else
	{
		text = (char *)xmlTextReaderValue(reader);
//TODO could be safer with strndup
		text2 = (char*)strdup(text);
		if(FREE == TRUE)
		{
			free(text);
		}
		return text2;
	}

	return NULL;
}

/**
 * Cheking for white spaces
 * @param text 
 * @return Returns TRUE if a text is all white space, FALSE otherwise
 */
int
is_all_whitespace(char* text)
{
	int len, i;

	len = strlen(text);
	for(i = 0; i < len; i++)
	{
		if(!isspace(text[i]))
		{
			return FALSE;
		}
	}
	return TRUE;
}

/**
 * Depricated
 * Replaces ' with \' if macro REPLACE_BAD_CHARS is set to TRUE
 * @param globals variables used for global handling
 */
char*
replace_bad_chars(char* value)
{
	char *temp;
	if(REPLACE_BAD_CHARS != TRUE)
	{
		return "";
	}
	if(value == NULL)
	{
		return "";
	}


	while( (temp = strchr(value, (int)'\'')) != NULL)
	{
		*temp = ' ';

	}
	return value;
}

/**
 * Flush the element buffer to element_table
 * @param globals variables used for global handling
 */
void
flush_element_node_buffer(xml_index_globals_ptr globals)
{
	int			i;			// iterator index
	int			spi_result;	// result of SPI calls
	char		nulls[9];	// c string with 'n' on position, where is NULL value
	SPIPlanPtr	pplan;		// prepared plan
	Oid			oids[9];	// type OIDs of values
	Datum		data[9];	// one row of values

	oids[0] = INT4OID;
	oids[1] = INT4OID;
	oids[2] = INT4OID;
	oids[3] = TEXTOID;
	oids[4] = INT4OID;
	oids[5] = INT4OID;
	oids[6] = INT4OID;
	oids[7] = INT4OID;
	oids[8] = INT4OID;

	elog(DEBUG1, "flushing element_nodes");	

	if ((DO_FLUSH == TRUE) && (globals->element_node_buffer_count > 0))
	{
		SPI_connect();

		pplan = SPI_prepare("INSERT INTO xml_element_nodes(did, pre_order, size, "
						"name, depth, child_id, prev_id, attr_id, parent_id) "
				"VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9)", 9, oids);

		if (pplan != NULL)
		{

			for(i = 0; i < globals->element_node_buffer_count; i++)
			{
				strncpy(nulls, "         ", 9);		// spaces indicates not null values

				data[0] = Int32GetDatum(element_node_buffer[i].did);
				data[1] = Int32GetDatum(element_node_buffer[i].order);
				data[2] = Int32GetDatum(element_node_buffer[i].size);
				data[3] = PointerGetDatum(cstring_to_text(element_node_buffer[i].tag_name));
				data[4] = Int32GetDatum(element_node_buffer[i].depth);
				data[5] = Int32GetDatum(element_node_buffer[i].child_id);
				data[6] = Int32GetDatum(element_node_buffer[i].prev_id);
				data[7] = Int32GetDatum(element_node_buffer[i].first_attr_id);
				data[8] = Int32GetDatum(element_node_buffer[i].parent_id);


				if (element_node_buffer[i].child_id == -1)
				{ // -1 indicate null value, then set it to nulls string
					nulls[5] = 'n';
				}
				if (element_node_buffer[i].prev_id == -1)
				{ // -1 indicate null value, then set it to nulls string
					nulls[6] = 'n';
				}
				if (element_node_buffer[i].first_attr_id == -1)
				{
					nulls[7] = 'n';
				}
				if (element_node_buffer[i].parent_id == -1)
				{
					nulls[8] = 'n';
				}

				if ((spi_result = SPI_execute_plan(pplan, data, nulls, false, 1)) != SPI_processed)
				{
					if (spi_result == SPI_ERROR_ARGUMENT) {
						elog(DEBUG1, "xml2/xml_index_loader.flush_element_node_buffer spi error argument");
					} else if (spi_result == SPI_ERROR_PARAM) {
						elog(DEBUG1, "xml2/xml_index_loader.flush_element_node_buffer spi error param");
					}
				}
			}
		} else
		{
			elog(DEBUG1, "xml2/xml_index_loader.flush_element_node_buffer node value or pplan is null !!");
		}

		SPI_finish();
	}
}


/**
 * Flush the attribute buffer to attribute_table
 * @param globals variables used for global handling
 */
void
flush_attribute_node_buffer(xml_index_globals_ptr globals)
{
	int			i;			// iterator index
	int			spi_result;	// result of SPI calls
	char		nulls[8];	// c string with 'n' on position, where is NULL value
	SPIPlanPtr	pplan;		// prepared plan
	Oid			oids[8];	// type OIDs of values
	Datum		data[8];	// one row of values

	oids[0] = INT4OID;
	oids[1] = INT4OID;
	oids[2] = INT4OID;
	oids[3] = TEXTOID;
	oids[4] = INT4OID;
	oids[5] = INT4OID;
	oids[6] = INT4OID;
	oids[7] = TEXTOID;

	elog(DEBUG1, "flushing attribute_nodes");

	if ((DO_FLUSH == TRUE)  && (globals->attribute_node_buffer_count > 0))
	{

		SPI_connect();

		pplan = SPI_prepare("INSERT INTO xml_attribute_nodes(did, pre_order, "
				"size, name, depth, parent_id, prev_id, value) "
				"VALUES ($1, $2, $3, $4, $5, $6, $7, $8)", 8, oids);

		if (pplan != NULL)
		{

			for(i = 0; i < globals->attribute_node_buffer_count; i++)
			{
				strncpy(nulls, "        ", 8);		// spaces indicates not null values

				data[0] = Int32GetDatum(attribute_node_buffer[i].did);
				data[1] = Int32GetDatum(attribute_node_buffer[i].order);
				data[2] = Int32GetDatum(attribute_node_buffer[i].size);
				data[3] = PointerGetDatum(cstring_to_text(attribute_node_buffer[i].tag_name));
				data[4] = Int32GetDatum(attribute_node_buffer[i].depth);
				data[5] = Int32GetDatum(attribute_node_buffer[i].parent_id);
				data[6] = Int32GetDatum(attribute_node_buffer[i].prev_id);
				data[7] = PointerGetDatum(cstring_to_text(attribute_node_buffer[i].value));

				if (attribute_node_buffer[i].parent_id == -1)
				{ // -1 indicate null value, then set it to nulls string
					nulls[5] = 'n';
				}
				if (attribute_node_buffer[i].prev_id == -1)
				{ // -1 indicate null value, then set it to nulls string
					nulls[6] = 'n';
				}
				if (attribute_node_buffer[i].value == NULL)
				{
					nulls[7] = 'n';
				}

				if ((spi_result = SPI_execute_plan(pplan, data, nulls, false, 1)) != SPI_processed)
				{
					if (spi_result == SPI_ERROR_ARGUMENT) {
						elog(DEBUG1, "xml2/xml_index_loader.flush_attribute_node_buffer spi error argument");
					} else if (spi_result == SPI_ERROR_PARAM) {
						elog(DEBUG1, "xml2/xml_index_loader.flush_attribute_node_buffer spi error param");
					}
				}
			}
		} else
		{
			elog(DEBUG1, "xml2/xml_index_loader.flush_attribute_node_buffer node value or pplan is null !!");
		}		

		SPI_finish();
	}

	elog(DEBUG1, "flushed attribute_nodes");
}

/**
 * Flush the text buffer to text_table
 * @param globals variables used for global handling
 */
void
flush_text_node_buffer(xml_index_globals_ptr globals)
{
	int			i;			// iterator index
	int			spi_result;	// result of SPI calls
	char		nulls[6];	// c string with 'n' on position, where is NULL value
	SPIPlanPtr	pplan;		// prepared plan
	Oid			oids[6];	// type OIDs of values
	Datum		data[6];	// one row of values

	oids[0] = INT4OID;
	oids[1] = INT4OID;
	oids[2] = INT4OID;
	oids[3] = INT4OID;
	oids[4] = INT4OID;
	oids[5] = TEXTOID;

	if ((DO_FLUSH == TRUE) && (globals->text_node_buffer_count > 0))
	{

		SPI_connect();

		pplan = SPI_prepare("INSERT INTO xml_text_nodes(did, pre_order, depth, parent_id, "
				"prev_id, value) VALUES ($1, $2, $3, $4, $5, $6)", 6, oids);

		for(i = 0; i < globals->text_node_buffer_count; ++i)
		{			

			strncpy(nulls, "      ", 6);		// spaces indicates not null values

			if ((text_node_buffer[i].value != NULL) && (pplan != NULL))
			{
				data[0] = Int32GetDatum(text_node_buffer[i].did);
				data[1] = Int32GetDatum(text_node_buffer[i].order);
				data[2] = Int32GetDatum(text_node_buffer[i].depth);
				data[3] = Int32GetDatum(text_node_buffer[i].parent_id);
				data[4] = Int32GetDatum(text_node_buffer[i].prev_id);
				data[5] = PointerGetDatum(cstring_to_text(text_node_buffer[i].value));				

				if (text_node_buffer[i].parent_id == -1)
				{ // -1 indicate null value, then set it to nulls string
					nulls[3] = 'n';
				}
				if (text_node_buffer[i].prev_id == -1)
				{ // -1 indicate null value, then set it to nulls string
					nulls[4] = 'n';
				}

				if ((spi_result = SPI_execute_plan(pplan, data, nulls, false, 1)) != SPI_processed)
				{
					if (spi_result == SPI_ERROR_ARGUMENT) {
						elog(DEBUG1, "xml2/xml_index_loader.flush_text_node_buffer spi error argument");
					} else if (spi_result == SPI_ERROR_PARAM) {
						elog(DEBUG1, "xml2/xml_index_loader.flush_text_node_buffer spi error param");
					}
				}
			} else
			{
				elog(DEBUG1, "xml2/xml_index_loader.flush_text_node_buffer node value or pplan is null !!");
			}
		}

		SPI_finish();
	}

	elog(DEBUG1, "flushed text_nodes");
}
/**
 * Prints a report 
 * @param globals
 */
void
report(xml_index_globals_ptr globals)
{
	elog(INFO, "Final report for loading XML into database");
	elog(INFO, "total number of nodes = %d", globals->global_order);
	elog(INFO, "total number of elements = %d", globals->element_node_count);
	elog(INFO, "total number attribute = %d", globals->attribute_node_count);
	elog(INFO, "total number of text nodes = %d", globals->text_node_count);
}