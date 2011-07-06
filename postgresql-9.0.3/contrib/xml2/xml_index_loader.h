/* 
 * File:   xml_index_loader.h
 * Author: xpospi04
 *
 */

#ifndef XML_INDEX_LOADER_H
#define	XML_INDEX_LOADER_H

#ifdef	__cplusplus
extern "C" {
#endif

#include "postgres.h"

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

#define DEBUG TRUE				//If TRUE DEBUG mode is turned on


//Error Codes
#define LIBXML_NO_EFFECT 0
#define LIBXML_SUCCESS 1
#define LIBXML_OK 1
#define LIBXML_ERR -1
#define LIBXML_EXIT_ERROR -2
#define LIBXML_ATTRIBUTE_ERROR -4
#define LIBXML_WHITESPACE_ERROR -5
#define MYSQL_ERROR -4
#define INPUT_ERROR -12
#define FILE_IO_ERROR -13
#define DATABASE_ERROR -14
#define TRUE 1
#define FALSE 0
#define NO_VALUE -1


#define XML_ERROR -1
#define XML_NO_ERROR 0

#define REAL_TEXT_NODE 1
#define FAKE_TEXT_NODE -1

#define XML_INDEX_LOADER_SUCCES 0
#define MAX_INT_SIZE 10 //Maximum digits in an Integer

#define DOCUMENT_ROOT "DOCUMENT_ROOT"  //The node name of the document root in the XISS/R database

//As defined by LIBXML
#define ELEMENT_START 1
#define ELEMENT_END 15
#define TEXT_NODE 3
#define CDATA_SEC 4
#define ENTITY_REF 5
#define ENTITY_DEC 6

#define DO_FLUSH TRUE 			//If TRUE write data to database
#define FREE TRUE 				//If true free all malloced memory
#define REPLACE_BAD_CHARS TRUE  //if True replace_bad_chars in misc.c is executed.


#define BUFFER_SIZE 10000		//Size of Buffer for Element, Attribute and Text Node queues



//Structs
typedef struct element_node element_node;
typedef struct element_node *element_node_ptr;
struct element_node{
	int did;
	int order;
	int size;
	char* tag_name;
	int depth;
	int child_id;
	int prev_id;
	int first_attr_id;
	int parent_id;
};


typedef struct attribute_node attribute_node;
typedef struct attribute_node *attribute_node_ptr;
struct attribute_node{
	int did;
	int order;
	int size;
	char* tag_name;
	int depth;
	int parent_id;
	int prev_id;
	char* value;
};


typedef struct text_node text_node;
typedef struct text_node *text_node_ptr;
struct text_node{
	int did;
	int order;
	int size;
	int depth;
	int parent_id;
	int prev_id;
	char* value;
};

typedef struct xml_index_globals xml_index_globals;
typedef struct xml_index_globals *xml_index_globals_ptr;
struct xml_index_globals {
	int global_order;
	int global_doc_id;
	int element_node_count;
	int element_node_buffer_count;
	int attribute_node_count;
	int attribute_node_buffer_count;
	int text_node_count;
	int text_node_buffer_count;
};


//Buffers
element_node		element_node_buffer[BUFFER_SIZE];
text_node			text_node_buffer[BUFFER_SIZE];
attribute_node		attribute_node_buffer[BUFFER_SIZE];

////////////////////////////////////////////////////////////////////////////////

int extern xml_index_entry(const char *xml_document, int length);

static int preorder_traverse(int parent_id, int sibling_id,	xmlTextReaderPtr reader,
		xml_index_globals_ptr globals);

void init_values(xml_index_globals_ptr globals);

int read_next_node(xmlTextReaderPtr reader, xml_index_globals_ptr globals);

int create_new_element(xmlTextReaderPtr reader, xml_index_globals_ptr globals);

int process_attributes(int parent_id, xmlTextReaderPtr reader, xml_index_globals_ptr globals);

int create_new_text_node(xml_index_globals_ptr globals);

int process_text_node(int parent_id, int prev_id, xmlTextReaderPtr reader,
		xml_index_globals_ptr globals);

char* get_text_from_node(xmlTextReaderPtr reader);
char* replace_bad_chars(char* value);
int is_all_whitespace(char * text);

void flush_text_node_buffer(xml_index_globals_ptr globals);
void flush_attribute_node_buffer(xml_index_globals_ptr globals);
void flush_element_node_buffer(xml_index_globals_ptr globals);

#ifdef	__cplusplus
}
#endif

#endif	/* XML_INDEX_LOADER_H */

