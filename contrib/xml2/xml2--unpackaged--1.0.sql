/* contrib/xml2/xml2--unpackaged--1.0.sql */

ALTER EXTENSION xml2 ADD function xslt_process(text,text);
ALTER EXTENSION xml2 ADD function xslt_process(text,text,text);
ALTER EXTENSION xml2 ADD function xpath_table(text,text,text,text,text);
ALTER EXTENSION xml2 ADD function xpath_nodeset(text,text,text);
ALTER EXTENSION xml2 ADD function xpath_nodeset(text,text);
ALTER EXTENSION xml2 ADD function xpath_list(text,text);
ALTER EXTENSION xml2 ADD function xpath_list(text,text,text);
ALTER EXTENSION xml2 ADD function xpath_bool(text,text);
ALTER EXTENSION xml2 ADD function xpath_number(text,text);
ALTER EXTENSION xml2 ADD function xpath_nodeset(text,text,text,text);
ALTER EXTENSION xml2 ADD function xpath_string(text,text);
ALTER EXTENSION xml2 ADD function xml_encode_special_chars(text);
ALTER EXTENSION xml2 ADD function xml_valid(text);
ALTER EXTENSION xml2 ADD function xmlvalidate_xsd(xml, text);
ALTER EXTENSION xml2 ADD function xmlvalidate_xsd(xml, xsd);
ALTER EXTENSION xml2 ADD function xmlvalidate_rng(xml, text);
ALTER EXTENSION xml2 ADD function xmlvalidate_rng(xml, rng);
ALTER EXTENSION xml2 ADD function xmlvalidate_dtd(xml, text);
ALTER EXTENSION xml2 ADD function xmlvalidate_dtd(xml, dtd);
ALTER EXTENSION xml2 ADD function build_xmlindex(xml, text);
ALTER EXTENSION xml2 ADD function build_xmlindex(xml, text, bool);
ALTER EXTENSION xml2 ADD function create_xmlindex_tables();


-- xml_valid is now an alias for core xml_is_well_formed()

CREATE OR REPLACE FUNCTION xml_valid(text) RETURNS bool
AS 'xml_is_well_formed'
LANGUAGE INTERNAL STRICT STABLE;

-- xml_is_well_formed is now in core, not needed in extension.
-- be careful to drop extension's copy not core's.

DROP FUNCTION @extschema@.xml_is_well_formed(text);
