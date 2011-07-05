/* $PostgreSQL: pgsql/contrib/xml2/uninstall_pgxml.sql,v 1.4 2007/11/13 04:24:29 momjian Exp $ */

-- Adjust this setting to control where the objects get dropped.
SET search_path = public;

DROP FUNCTION xslt_process(text,text);

DROP FUNCTION xslt_process(text,text,text);

DROP FUNCTION xpath_table(text,text,text,text,text);

DROP FUNCTION xpath_nodeset(text,text,text);

DROP FUNCTION xpath_nodeset(text,text);

DROP FUNCTION xpath_list(text,text);

DROP FUNCTION xpath_list(text,text,text);

DROP FUNCTION xpath_bool(text,text);

DROP FUNCTION xpath_number(text,text);

DROP FUNCTION xpath_nodeset(text,text,text,text);

DROP FUNCTION xpath_string(text,text);

DROP FUNCTION xml_encode_special_chars(text);

-- deprecated old name for xml_is_well_formed
DROP FUNCTION xml_valid(text);

DROP FUNCTION xml_is_well_formed(text);

DROP TYPE dtd CASCADE;

DROP TYPE xsd CASCADE;

DROP TYPE rng CASCADE;

DROP FUNCTION xmlvalidate_xsd(xml, text);

DROP FUNCTION xmlvalidate_rng(xml, text);

DROP FUNCTION xmlvalidate_dtd(xml, text);

-- -----------------------------------------------------------------------------
-- XML indexing functions
-- Tomas Pospisil, GSoC project
--
DROP FUNCTION build_xmlindex(xml);

DROP FUNCTION create_xmlindex_tables();

DROP TABLE attribute_table;
DROP TABLE element_table;
DROP TABLE text_table;
DROP TABLE xml_documents_table;