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


/**
 * Entry point of loader
 * @param xml_document
 * @return true/false if all XML shredding
 */
int extern
xml_index_entry(const char *xml_document, int length)
{
	xml_index_globals		globals;
	int preorder_result;

	//globals.reader
	xmlTextReaderPtr reader		= xmlReaderForMemory(xml_document, length,
			NULL,NULL, 0);

	if (reader == NULL)
	{ // error with loading
		elog(INFO, "HUGE problem with libXML in memory loading of XML document");
		return LIBXML_ERR;
    }

	init_values(&globals);

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
	globals->global_doc_id					= 0;
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
read_next_node(xmlTextReaderPtr reader, xml_index_globals_ptr globals)
{
	int err_val = xmlTextReaderRead(reader);

	elog(INFO, "reading next node");

//TODO better handling
	if(err_val == LIBXML_ERR)
	{
		elog(INFO, "Error reading node, at order = %d", globals->global_order);
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

	if (DEBUG == TRUE)
		elog(INFO, "creating new element at index: %d", my_ind);

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


	globals->element_node_buffer_count++;
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
preorder_traverse(int parent_id, int sibling_id, xmlTextReaderPtr reader,
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


	if(DEBUG == TRUE)
	{
		elog(INFO, "--PREORDER-- Parsing %d:%s at depth %d\n", my_order, my_tag_name, my_depth);
	}

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
		if(DEBUG == TRUE)
		{
			elog(INFO, "Node %d:%s at depth %d, does not have a closing tag.\n", my_order, my_tag_name, my_depth);
		}
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

	if(DEBUG == TRUE && node_type == ELEMENT_END)
	{
		elog(INFO, "Found end of %d:%s with no non-attribute children at depth %d returning to %d.\n",my_order, my_tag_name,  my_depth, parent_id);
	}

	while(node_type != ELEMENT_END)  //While we have unvisited children
	{
		elog(INFO, "while (node_type != ELEMENT_END)");

		if(node_type == TEXT_NODE || node_type == CDATA_SEC) //Visit text nodes
		{
			elog(INFO, "je to text node");

			err_val = process_text_node(my_order, prev_child, reader, globals);
			if(err_val == REAL_TEXT_NODE)
			{
				my_size++;
			}

		} else if(node_type == ELEMENT_START) //Recurse on elements
		{
			elog(INFO, "je to element_start");

			recent_child = (globals->global_order) + 1; //Next time we have a child it will know this as its nearest sibling
			size_res = preorder_traverse(my_order,  prev_child, reader, globals);


			my_size += size_res;
			prev_child = recent_child;
			if(my_depth == xmlTextReaderDepth(reader) && xmlTextReaderNodeType(reader) == ELEMENT_END)
			{
				if(DEBUG)
				{
					elog(INFO, "Node %d:%s at depth %d is done, its child has no closing tag.\n", my_order, my_tag_name, my_depth);
				}
				break;
			}
		} else
		{
			elog(INFO, "Encounted an node of type %d where it should not be\n", node_type);
			return(LIBXML_ERR);
			//Possibly implement error handling code here

		}
		
		err_val = read_next_node(reader, globals);

		if(err_val == 0)
		{
			elog(INFO, "Malformed XML: reached end of document without reaching "
					"the end tag for the current element\n");
			return(LIBXML_ERR);
		}

		elog(INFO, "je to v pisi reader:%d  X my_depth:%d, is",
				xmlTextReaderDepth(reader), my_depth);

		node_type = xmlTextReaderNodeType(reader);
		if(DEBUG == TRUE && node_type == ELEMENT_END)
		{
			elog(INFO,"Found end of %d:%s with %d children at depth %d returning to %d.\n",
					my_order, my_tag_name, my_size, my_depth, parent_id );
		}

		if(my_depth >= xmlTextReaderDepth(reader))
		{
			if(DEBUG == TRUE)
			{
				elog(INFO,"Node %d:%s with %d children at depth %d has no end "
						"tag, now returning to %d\n",my_order, my_tag_name,
						my_size, my_depth, parent_id );
			}
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

	if (DEBUG == true)
	{
		elog(INFO, "== CREATE == element[%d] values did:%d, order:%d, size:%d "
				"depth:%d, first_attr_id:%d, , child_id:%d, parent_id:%d",
				my_ind,
				element_node_buffer[my_ind].did,
				element_node_buffer[my_ind].order,
				element_node_buffer[my_ind].size,
				element_node_buffer[my_ind].depth,
				element_node_buffer[my_ind].first_attr_id,
				element_node_buffer[my_ind].child_id,
				element_node_buffer[my_ind].parent_id);
	}
	
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
process_attributes(int parent_id, xmlTextReaderPtr reader,
		xml_index_globals_ptr globals)
{


	int i, my_ind, err;
	int num_attributes;
	xmlChar * text;
	int last_attr = NO_VALUE;

	num_attributes = xmlTextReaderAttributeCount(reader);

	elog(INFO, "processing attributes it is there %d", num_attributes);

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
			elog(INFO,"LIBXML_SUCCESS not found error");
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
			elog(INFO,"LIBXML_SUCCESS not found error"); 
			exit(LIBXML_ATTRIBUTE_ERROR);
		}
		attribute_node_buffer[my_ind].parent_id = parent_id;
		attribute_node_buffer[my_ind].prev_id = last_attr;

		err = xmlTextReaderReadAttributeValue(reader);
		if(err == LIBXML_SUCCESS)
		{
			attribute_node_buffer[my_ind].value = (char *)xmlTextReaderValue(reader);
		}

		if (DEBUG)
		{
			elog(INFO, "== CREATE == attribute[%d] values depth:%d, did:%d, order:%d, parent_id:%d, "
					"prev_id:%d, size:%d, att_name:%s, value:%s", my_ind,
					attribute_node_buffer[my_ind].depth,
					attribute_node_buffer[my_ind].did,
					attribute_node_buffer[my_ind].order,
					attribute_node_buffer[my_ind].parent_id,
					attribute_node_buffer[my_ind].prev_id,
					attribute_node_buffer[my_ind].size,
					attribute_node_buffer[my_ind].tag_name,
					attribute_node_buffer[my_ind].value);
		}

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

	elog(INFO, "creating new text node at index: %d", my_ind);

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

	elog(INFO, "creating new attribute at index: %d", my_ind);

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
process_text_node(int parent_id, int prev_id, xmlTextReaderPtr reader,
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
	text_node_buffer[my_ind].value = replace_bad_chars(value);
	elog(INFO, "== CREATE == text node[%d] depth:%d, did:%d, order:%d, parent_id:%d, "
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
	if(node_type !=  TEXT_NODE)
	{
		if(node_type == CDATA_SEC)  //If we have CDATA
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
	int i, name_length;

	StringInfoData query;

	elog(INFO, "flushing element_nodes");

	initStringInfo(&query);
	appendStringInfo(&query,
						"INSERT INTO element_table(did, nid, size, name, depth, child_id, prev_id, attr_id, parent_id) VALUES ");

	if ((DO_FLUSH == TRUE) && ((globals->element_node_buffer_count-1) > 0))
	{
		for(i = 0; i < (globals->element_node_buffer_count-1); i++)
		{
			if(element_node_buffer[i].tag_name != NULL)
			{
				name_length = strlen(element_node_buffer[i].tag_name);
			}
			else
			{
				name_length = 7;
			}

			appendStringInfo(&query,
							" (%d, %d, %d, '%s', %d, %d, %d, %d, %d)",
							element_node_buffer[i].did,
							element_node_buffer[i].order,
							element_node_buffer[i].size,
							element_node_buffer[i].tag_name,
							element_node_buffer[i].depth,
							element_node_buffer[i].child_id,
							element_node_buffer[i].prev_id,
							element_node_buffer[i].first_attr_id,
							element_node_buffer[i].parent_id
				);

			if ((i+1) < (globals->element_node_buffer_count-1))
			{
				appendStringInfo(&query, ",");
			} else
			{
				appendStringInfo(&query, ";");
			}
		}

		elog(INFO, "flush element: %s\n", query.data);

		SPI_connect();

		if (SPI_execute(query.data, false, 0) == SPI_ERROR_ARGUMENT)
			ereport(ERROR,
					(errcode(ERRCODE_DATA_EXCEPTION),
					 errmsg("invalid query")));

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
	int i, val_len;
	StringInfoData query;

	elog(INFO, "flushing attribute_nodes");

	initStringInfo(&query);
	appendStringInfo(&query,
						"INSERT INTO attribute_table(did, nid, size, name, depth, parent_id, prev_id, value) VALUES ");

	if ((DO_FLUSH == TRUE)  && (globals->attribute_node_buffer_count > 0))
	{		
		for(i = 0; i < globals->attribute_node_buffer_count; i++)
		{
			if(attribute_node_buffer[i].value != NULL)
			{
				val_len = strlen(attribute_node_buffer[i].value);
			}
			else
			{
				val_len = 7;
			}

			appendStringInfo(&query,
							" (%d, %d, %d, '%s', %d, %d, %d, '%s')",
							attribute_node_buffer[i].did,
							attribute_node_buffer[i].order,
							attribute_node_buffer[i].size,
							attribute_node_buffer[i].tag_name,
							attribute_node_buffer[i].depth,
							attribute_node_buffer[i].parent_id,
							attribute_node_buffer[i].prev_id,
							attribute_node_buffer[i].value
				);

			if ((i+1) < globals->attribute_node_buffer_count)
			{
				appendStringInfo(&query, ",");
			} else
			{
				appendStringInfo(&query, ";");
			}
		}

		elog(INFO, "flush attributes: %s\n", query.data);

		SPI_connect();

		if (SPI_execute(query.data, false, 0) == SPI_ERROR_ARGUMENT)
			ereport(ERROR,
					(errcode(ERRCODE_DATA_EXCEPTION),
					 errmsg("invalid query")));

		SPI_finish();
	}

	elog(INFO, "flushed attribute_nodes");
}

/**
 * Flush the text buffer to text_table
 * @param globals variables used for global handling
 */
void
flush_text_node_buffer(xml_index_globals_ptr globals)
{
	int i, val_len;
	StringInfoData query;

	elog(INFO, "flushing text_nodes");

	initStringInfo(&query);
	appendStringInfo(&query,
						"INSERT INTO text_table(did, nid, depth, parent_id, prev_id, value) VALUES ");

	if ((DO_FLUSH == TRUE) && (globals->text_node_buffer_count > 0))
	{
		for(i = 0; i < globals->text_node_buffer_count; i++)
		{
			if (text_node_buffer[i].value != NULL)
			{
				appendStringInfo(&query,
							" (%d, %d, %d, %d, %d, '%s')",
						text_node_buffer[i].did,
						text_node_buffer[i].order,
						text_node_buffer[i].depth,
						text_node_buffer[i].parent_id,
						text_node_buffer[i].prev_id,
						text_node_buffer[i].value
				);
			} else
			{
				appendStringInfo(&query,
							" (%d, %d, %d, %d, %d, NULL)",
						text_node_buffer[i].did,
						text_node_buffer[i].order,
						text_node_buffer[i].depth,
						text_node_buffer[i].parent_id,
						text_node_buffer[i].prev_id
				);
			}
				if ((i+1) < globals->text_node_buffer_count)
				{
					appendStringInfo(&query, ",");
				} else
				{
					appendStringInfo(&query, ";");
				}

			if(text_node_buffer[i].value != NULL)
			{
				val_len = strlen(text_node_buffer[i].value);
			} else
			{ // TODO figure out what is mean
				val_len = 7;
			}
		}

		elog(INFO, "flush text nodes: %s\n", query.data);

		SPI_connect();

		if (SPI_execute(query.data, false, 0) == SPI_ERROR_ARGUMENT)
			ereport(ERROR,
					(errcode(ERRCODE_DATA_EXCEPTION),
					 errmsg("invalid query")));

		SPI_finish();
	}
	elog(INFO, "flushed text_nodes");
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