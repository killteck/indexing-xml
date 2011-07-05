

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
	char smaz[] = "<a>ahoj</a>";

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

	return XML_INDEX_LOADER_SUCCES;
}


//Initialize global values
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

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////



// Read the next node (move the stream to the next node) and return an error code if neccesary.
int 
read_next_node(xmlTextReaderPtr reader, xml_index_globals_ptr globals)
{
	int err_val = xmlTextReaderRead(reader);
//TODO better handling
	if(err_val == LIBXML_ERR)
	{
		elog(INFO, "Error reading node, at order = %d", globals->global_order);
	}
	
	return err_val;
}

// Clears the next element record in the element buffer and
// Returns the index to the buffer for the "new" element
// May flush the buffer and reset the index if the buffer is full.
int 
create_new_element(xmlTextReaderPtr reader, xml_index_globals_ptr globals)
{
/*
	int my_ind;

	if(BUFFER_COUNT == BUFFER_SIZE)
	{
		//flush BUFFER reset buffer_count
		FLUSH;
		BUFFER_COUNT = 0;
	}
	my_ind = BUFFER_COUNT;
	BUFFER[my_ind].did = NO_VALUE;
	BUFFER[my_ind].order = NO_VALUE;
	BUFFER[my_ind].size = NO_VALUE;
	if(FREE == TRUE && BUFFER[my_ind].tag_name != NULL && BUFFER_COUNT > BUFFER_SIZE)
	{
		free(BUFFER[my_ind].tag_name);
	}
	BUFFER[my_ind].tag_name = NULL;
	BUFFER[my_ind].depth = NO_VALUE;
	BUFFER[my_ind].child_id = NO_VALUE;
	BUFFER[my_ind].prev_id = NO_VALUE;
	BUFFER[my_ind].first_attr_id = NO_VALUE;
	BUFFER_COUNT++;


	COUNTER++;
	return my_ind;
*/
}

// Executes a preorder traversal of the document tree.
// It processes elements(and in turn attributes and text elements)
// parent_id = parent node's order
// sibling_id = Order of this node's nearest sibling
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
		 my_first_attr_id = -1,
		 my_child_id = -1;

	//Get Order, and Size
	my_order = ++(globals->global_order);
	my_size = 0;

	//get the name of the node
	my_tag_name = xmlTextReaderName(reader);

	//Get Depth
	my_depth = xmlTextReaderDepth(reader);


	if(DEBUG == TRUE)
	{
		elog(INFO, "Parsing %d:%s at depth %d\n", my_order, my_tag_name, my_depth);
	}

	//Process all attributes
//TODO
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
		printf("Found end of %d:%s with no non-attribute children at depth %d returning to %d.\n",my_order, my_tag_name,  my_depth, parent_id);
	}
/*
	while(node_type != ELEMENT_END)  //While we have unvisited children
	{
		if(node_type == TEXT_NODE || node_type == CDATA_SEC) //Visit text nodes
		{
//TODO

			err_val = process_text_node(my_order, prev_child);
			if(err_val == REAL_TEXT_NODE)
			{
				my_size++;
			}

		}
		else if(node_type == ELEMENT_START) //Recurse on elements
		{
			recent_child = global_order + 1; //Next time we have a child it will know this as its nearest sibling
			size_res = preorder_traverse(my_order,  prev_child);


			my_size += size_res;
			prev_child = recent_child;
			if(my_depth == xmlTextReaderDepth(my_xml_doc) && xmlTextReaderNodeType(my_xml_doc) == ELEMENT_END)
			{
				if(DEBUG == TRUE)
				{
					printf("Node %d:%s at depth %d is done, its child has no closing tag.\n", my_order, my_tag_name, my_depth);
				}
				break;
			}
		}
		else
		{
			fprintf(errfile, "Encounted an node of type %d where it should not be\n", node_type);
			exit(LIBXML_ERR);
			//Possibly implement error handling code here

		}
		err_val = read_next_node();
		if(err_val == 0)
		{
			fprintf(errfile, "Malformed XML: reached end of document without reaching the end tag for the current element\n");
			exit(LIBXML_ERR);
		}
		if(my_depth >= xmlTextReaderDepth(my_xml_doc))
		{
			if(DEBUG == TRUE)
			{
				printf("Node %d:%s with %d children at depth %d has no end tag, now returning to %d\n",my_order, my_tag_name, my_size, my_depth, parent_id );
			}
			break;
		}



		node_type = xmlTextReaderNodeType(my_xml_doc);
		if(DEBUG == TRUE && node_type == ELEMENT_END)
		{
			printf("Found end of %d:%s with %d children at depth %d returning to %d.\n",my_order, my_tag_name, my_size, my_depth, parent_id );
		}

	}
	//We have visited each child

	//Create new queue entry for this element, initialized with null or no_value entries
	my_ind = create_new_element();



	BUFFER[my_ind].did = global_doc_id;
	BUFFER[my_ind].order = my_order;
	BUFFER[my_ind].size = my_size;
	BUFFER[my_ind].depth = my_depth;
	BUFFER[my_ind].first_attr_id = my_first_attr_id;
	BUFFER[my_ind].child_id = recent_child;
	BUFFER[my_ind].parent_id = parent_id;
	//Tag name
	if(my_tag_name == NULL && my_order == 1  && parent_id == NO_VALUE)
	{
		BUFFER[my_ind].tag_name = (char*) strdup(DOCUMENT_ROOT);
		//error
	}
	else
	{
		BUFFER[my_ind].tag_name = my_tag_name;//(char*)strdup(my_tag_name);
	//	if(FREE == TRUE)
	//	{
	//		free(my_tag_name);
	//	}
	}


	//Get Previous Sibling
	if(sibling_id != NO_VALUE)
	{
		BUFFER[my_ind].prev_id = sibling_id;
	}



	return BUFFER[my_ind].size + 1;
*/
}

// Creates new records and fills records for all attributes that
// are children of the element with <parent_id>.
// When called, the current item in the XMl doc stream is
// the element that is parent of these attributes.
// Returns the number of attributes processed.
int 
process_attributes(int parent_id, xmlTextReaderPtr reader,
		xml_index_globals_ptr globals)
{


	int i,j, my_ind, err;
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
			//error
			i--;
			break;
		}

		/*
		my_ind = create_new_attribute();
		BUFFER[my_ind].did = global_doc_id;
		BUFFER[my_ind].order = ++global_order;
		BUFFER[my_ind].size = 0;
		text = xmlTextReaderName(my_xml_doc);
		BUFFER[my_ind].tag_name = text;
		//if(FREE == TRUE)
		//{
		//	xmlFree(text);
		//}
		BUFFER[my_ind].depth = xmlTextReaderDepth(my_xml_doc);
		if(BUFFER[my_ind].depth == -1)
		{
			//Possible place to implement error handling code
			exit(LIBXML_ATTRIBUTE_ERROR);
		}
		BUFFER[my_ind].parent_id = parent_id;
		BUFFER[my_ind].prev_id = last_attr;

		err = xmlTextReaderReadAttributeValue(my_xml_doc);
		if(err == LIBXML_SUCCESS)
		{
			BUFFER[my_ind].value = xmlTextReaderValue(my_xml_doc);
		}

		last_attr = BUFFER[my_ind].order;
		err = xmlTextReaderMoveToElement(my_xml_doc);
		if(err == LIBXML_ERR || err == LIBXML_NO_EFFECT)
		{
			//Possible place to implement error handling code
			exit(LIBXML_ATTRIBUTE_ERROR);
		}
		err = xmlTextReaderMoveToNextAttribute(my_xml_doc);
		if(err == LIBXML_ERR)
		{
			//Possible place to implement error handling code
			exit(LIBXML_ATTRIBUTE_ERROR);
		}
		 * */
	}
	return i;
}




