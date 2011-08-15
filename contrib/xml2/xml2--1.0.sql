/* contrib/xml2/xml2--1.0.sql */

--SQL for XML parser

-- deprecated old name for xml_is_well_formed
CREATE FUNCTION xml_valid(text) RETURNS bool
AS 'xml_is_well_formed'
LANGUAGE INTERNAL STRICT STABLE;

CREATE FUNCTION xml_encode_special_chars(text) RETURNS text
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION xpath_string(text,text) RETURNS text
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION xpath_nodeset(text,text,text,text) RETURNS text
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION xpath_number(text,text) RETURNS float4
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION xpath_bool(text,text) RETURNS boolean
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

-- List function

CREATE FUNCTION xpath_list(text,text,text) RETURNS text
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

CREATE FUNCTION xpath_list(text,text) RETURNS text
AS 'SELECT xpath_list($1,$2,'','')'
LANGUAGE SQL STRICT IMMUTABLE;

-- Wrapper functions for nodeset where no tags needed

CREATE FUNCTION xpath_nodeset(text,text)
RETURNS text
AS 'SELECT xpath_nodeset($1,$2,'''','''')'
LANGUAGE SQL STRICT IMMUTABLE;

CREATE FUNCTION xpath_nodeset(text,text,text)
RETURNS text
AS 'SELECT xpath_nodeset($1,$2,'''',$3)'
LANGUAGE SQL STRICT IMMUTABLE;

-- Table function

CREATE FUNCTION xpath_table(text,text,text,text,text)
RETURNS setof record
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT STABLE;

-- XSLT functions

CREATE FUNCTION xslt_process(text,text,text)
RETURNS text
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT VOLATILE;

-- the function checks for the correct argument count
CREATE FUNCTION xslt_process(text,text)
RETURNS text
AS 'MODULE_PATHNAME'
LANGUAGE C STRICT IMMUTABLE;

-- -----------------------------------------------------------------------------
-- SQL script for creating new DTD, RNG, XSD data types
-- part of Tomas Pospisil Master's Thesis
--

-- DTD data type
CREATE TYPE dtd;

CREATE FUNCTION dtd_in(cstring)
    RETURNS dtd
    AS 'MODULE_PATHNAME'
    LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION dtd_out(dtd)
    RETURNS cstring
    AS 'MODULE_PATHNAME'
    LANGUAGE C IMMUTABLE STRICT;

CREATE TYPE dtd (
	internallength = variable,
	alignment = int4,
	storage = extended,
	input = dtd_in,
	output = dtd_out);

-- XSD data type
CREATE TYPE xsd;

CREATE FUNCTION xsd_in(cstring)
    RETURNS xsd
    AS 'MODULE_PATHNAME'
    LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION xsd_out(xsd)
    RETURNS cstring
    AS 'MODULE_PATHNAME'
    LANGUAGE C IMMUTABLE STRICT;

CREATE TYPE xsd (
	internallength = variable,
	alignment = int4,
	storage = extended,
	input = xsd_in,
	output = xsd_out);

-- RNG data type
CREATE TYPE rng;

CREATE FUNCTION rng_in(cstring)
    RETURNS rng
    AS 'MODULE_PATHNAME'
    LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION rng_out(rng)
    RETURNS cstring
    AS 'MODULE_PATHNAME'
    LANGUAGE C IMMUTABLE STRICT;

CREATE TYPE rng (
	internallength = variable,
	alignment = int4,
	storage = extended,
	input = rng_in,
	output = rng_out);

-- -----------------------------------------------------------------------------
-- XML validation functions
-- Part of Tomas Pospisil, Master Thesis
--

CREATE FUNCTION xmlvalidate_xsd(xml, text) RETURNS boolean
    AS 'MODULE_PATHNAME', 'xmlvalidate_xsd'
    LANGUAGE C STRICT;

CREATE FUNCTION xmlvalidate_xsd(xml, xsd) RETURNS boolean
    AS 'MODULE_PATHNAME', 'xmlvalidate_xsd'
    LANGUAGE C STRICT;

CREATE FUNCTION xmlvalidate_rng(xml, text) RETURNS boolean
    AS 'MODULE_PATHNAME', 'xmlvalidate_rng'
    LANGUAGE C STRICT;

CREATE FUNCTION xmlvalidate_rng(xml, rng) RETURNS boolean
    AS 'MODULE_PATHNAME', 'xmlvalidate_rng'
    LANGUAGE C STRICT;

CREATE FUNCTION xmlvalidate_dtd(xml, text) RETURNS boolean
    AS 'MODULE_PATHNAME', 'xmlvalidate_dtd'
    LANGUAGE C STRICT;

CREATE FUNCTION xmlvalidate_dtd(xml, dtd) RETURNS boolean
    AS 'MODULE_PATHNAME', 'xmlvalidate_dtd'
    LANGUAGE C STRICT;

-- -----------------------------------------------------------------------------
-- XML indexing functions
-- Tomas Pospisil, GSoC project
--

CREATE FUNCTION build_xmlindex(xml, text) RETURNS boolean
    AS 'MODULE_PATHNAME', 'build_xmlindex'
    LANGUAGE C STRICT;

CREATE FUNCTION build_xmlindex(xml, text, bool) RETURNS boolean
    AS 'MODULE_PATHNAME', 'build_xmlindex'
    LANGUAGE C STRICT;

CREATE FUNCTION create_xmlindex_tables() RETURNS void
    AS 'MODULE_PATHNAME', 'create_xmlindex_tables'
    LANGUAGE C STRICT VOLATILE;

SELECT create_xmlindex_tables();
