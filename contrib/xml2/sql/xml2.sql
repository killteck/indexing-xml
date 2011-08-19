CREATE EXTENSION xml2;

select query_to_xml('select 1 as x',true,false,'');

select xslt_process( query_to_xml('select x from generate_series(1,5) as
x',true,false,'')::text,
$$<xsl:stylesheet version="1.0"
               xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:output method="xml" indent="yes" />
<xsl:template match="*">
  <xsl:copy>
     <xsl:copy-of select="@*" />
     <xsl:apply-templates />
  </xsl:copy>
</xsl:template>
<xsl:template match="comment()|processing-instruction()">
  <xsl:copy />
</xsl:template>
</xsl:stylesheet>
$$::text);

CREATE TABLE xpath_test (id integer NOT NULL, t xml);
INSERT INTO xpath_test VALUES (1, '<doc><int>1</int></doc>');
SELECT * FROM xpath_table('id', 't', 'xpath_test', '/doc/int', 'true')
as t(id int4);
SELECT * FROM xpath_table('id', 't', 'xpath_test', '/doc/int', 'true')
as t(id int4, doc int4);

DROP TABLE xpath_test;
CREATE TABLE xpath_test (id integer NOT NULL, t text);
INSERT INTO xpath_test VALUES (1, '<doc><int>1</int></doc>');
SELECT * FROM xpath_table('id', 't', 'xpath_test', '/doc/int', 'true')
as t(id int4);
SELECT * FROM xpath_table('id', 't', 'xpath_test', '/doc/int', 'true')
as t(id int4, doc int4);

create table articles (article_id integer, article_xml xml, date_entered date);
insert into articles (article_id, article_xml, date_entered)
values (2, '<article><author>test</author><pages>37</pages></article>', now());
SELECT * FROM
xpath_table('article_id',
            'article_xml',
            'articles',
            '/article/author|/article/pages|/article/title',
            'date_entered > ''2003-01-01'' ')
AS t(article_id integer, author text, page_count integer, title text);

-- this used to fail when invoked a second time
select xslt_process('<aaa/>',$$<xsl:stylesheet version="1.0"
xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:template match="@*|node()">
      <xsl:copy>
         <xsl:apply-templates select="@*|node()"/>
      </xsl:copy>
   </xsl:template>
</xsl:stylesheet>$$)::xml;

select xslt_process('<aaa/>',$$<xsl:stylesheet version="1.0"
xmlns:xsl="http://www.w3.org/1999/XSL/Transform">
<xsl:template match="@*|node()">
      <xsl:copy>
         <xsl:apply-templates select="@*|node()"/>
      </xsl:copy>
   </xsl:template>
</xsl:stylesheet>$$)::xml;

create table t1 (id integer, xml_data xml);
insert into t1 (id, xml_data)
values
(1, '<attributes><attribute name="attr_1">Some
Value</attribute></attributes>');

create index idx_xpath on t1 ( xpath_string
('/attributes/attribute[@name="attr_1"]/text()', xml_data::text));

SELECT xslt_process('<employee><name>cim</name><age>30</age><pay>400</pay></employee>'::text, $$<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:output method="xml" omit-xml-declaration="yes" indent="yes"/>
  <xsl:strip-space elements="*"/>
  <xsl:param name="n1"/>
  <xsl:param name="n2"/>
  <xsl:param name="n3"/>
  <xsl:param name="n4"/>
  <xsl:param name="n5" select="'me'"/>
  <xsl:template match="*">
    <xsl:element name="samples">
      <xsl:element name="sample">
        <xsl:value-of select="$n1"/>
      </xsl:element>
      <xsl:element name="sample">
        <xsl:value-of select="$n2"/>
      </xsl:element>
      <xsl:element name="sample">
        <xsl:value-of select="$n3"/>
      </xsl:element>
      <xsl:element name="sample">
        <xsl:value-of select="$n4"/>
      </xsl:element>
      <xsl:element name="sample">
        <xsl:value-of select="$n5"/>
      </xsl:element>
      <xsl:element name="sample">
        <xsl:value-of select="$n6"/>
      </xsl:element>
      <xsl:element name="sample">
        <xsl:value-of select="$n7"/>
      </xsl:element>
      <xsl:element name="sample">
        <xsl:value-of select="$n8"/>
      </xsl:element>
      <xsl:element name="sample">
        <xsl:value-of select="$n9"/>
      </xsl:element>
      <xsl:element name="sample">
        <xsl:value-of select="$n10"/>
      </xsl:element>
      <xsl:element name="sample">
        <xsl:value-of select="$n11"/>
      </xsl:element>
      <xsl:element name="sample">
        <xsl:value-of select="$n12"/>
      </xsl:element>
    </xsl:element>
  </xsl:template>
</xsl:stylesheet>$$::text, 'n1="v1",n2="v2",n3="v3",n4="v4",n5="v5",n6="v6",n7="v7",n8="v8",n9="v9",n10="v10",n11="v11",n12="v12"'::text);

-- basic test for shredding
SELECT build_xmlindex('<?xml version="1.0" encoding="utf-8"?>
<books>
	<book>
		<author>
			<firstname>Quanzhong</firstname>
			<lastname>Li</lastname>
		</author>
		<title>XML indexing</title>
		<price unit = "USD">120</price>
	</book>
	<book>
		<author>
			<firstname>Bongki</firstname>
			<lastname>Moon</lastname>
		</author>
		<title>javelina javelina</title>
		<price unit = "USD" test = "AM">100</price>
	</book>
</books>', 'bookshelf', true);

SELECT * FROM xml_documents xd WHERE xd.name = 'bookshelf';
SELECT * FROM xml_attribute_nodes WHERE did = 1;
SELECT * FROM xml_text_nodes WHERE did = 1;

CREATE OR REPLACE FUNCTION generate_xmls()
RETURNS integer AS $$
DECLARE
	node text = '<inner> in value </inner>';
BEGIN

	FOR i IN 1..100 LOOP
	node = '<a>a value</a>' || node || '<b>b value</b>';
	PERFORM build_xmlindex(('<envelope>' || node || '</envelope>')::xml, 'regress test' || i, true);
	END LOOP;

	return 0;
END;
  $$ LANGUAGE 'plpgsql';

-- generate xmls and shred them
SELECT generate_xmls();

-- execute XPath query on longest XML document (non shreded) DocumentID == 100
SELECT unnest(xpath('/envelope/a',
		(select value from xml_documents where did = 100)))
	AS regular_xpath;

-- Xpath to SQL translation with same result as above DocumentID == 100
-- do not use range index
SELECT elements.did as Document_ID, elements.pre_order as Node_ID,
	xmlforest(tv.value as a) as xpath_to_sql
FROM
	(SELECT		
		et1.pre_order, et1.did
	FROM
		xml_element_nodes et0, xml_element_nodes et1
	WHERE
		et0.name = 'envelope' AND
		et1.name = 'a' AND
		et0.DID = et1.DID AND
		et1.parent_id = et0.pre_order AND
		et0.did = 100
	ORDER BY et1.pre_order
	) AS elements
LEFT JOIN xml_text_nodes tv ON (tv.parent_id = elements.pre_order AND tv.did = 100);

-- range index tests
set enable_seqscan = f;
set enable_bitmapscan = f;
set enable_indexscan = t;

SELECT COUNT(*) FROM xml_element_nodes WHERE range(pre_order, (pre_order+size)) @> range(1,100);
SELECT COUNT(*) FROM xml_element_nodes WHERE range(pre_order, (pre_order+size)) <@ range(1,100);

-- searching for * xml subtree with /envelope/a in all xml documents
SELECT elements.did as Document_ID, elements.pre_order as Node_ID,
	xmlforest(tv.value as a) as xpath_to_sql
FROM
	(SELECT
		et1.pre_order, et1.did
	FROM
		xml_element_nodes et0, xml_element_nodes et1
	WHERE
		et0.name = 'envelope' AND
		et1.name = 'a' AND
		et0.did = et1.did AND
		et1.parent_id = et0.pre_order
	) AS elements
LEFT JOIN xml_text_nodes tv ON (tv.parent_id = elements.pre_order AND
	tv.did = elements.did)
ORDER BY tv.did, tv.pre_order;

SELECT did as Document_ID, 
	 unnest(xpath('/envelope/a', value )) AS regular_xpath
FROM xml_documents
WHERE xpath_exists('/envelope/a', value) = true
ORDER BY did;

-- test XPath to SQL with range benefit
SELECT et.*
FROM
	(SELECT
			et1.pre_order, et1.size, et1.did
		FROM
			xml_element_nodes et0, xml_element_nodes et1
		WHERE
			et0.name = 'books' AND
			et1.name = 'book' AND
			et0.did = et1.did) AS sub
LEFT JOIN xml_element_nodes et ON (range(et.pre_order, et.pre_order+et.size) <@ 
	range(sub.pre_order, (sub.pre_order + sub.size)) AND sub.did = et.did)
ORDER BY et.did, et.pre_order;

SELECT
	et2.*
FROM
	xml_element_nodes et0, xml_element_nodes et1, xml_element_nodes et2
WHERE
	et0.name = 'books' AND
	et1.name = 'book' AND
	et0.did = et1.did AND
	et2.did = et1.did AND
	range(et1.pre_order, et1.pre_order+et1.size) @>
		range(et2.pre_order, (et2.pre_order + et2.size))
ORDER BY et2.did, et2.pre_order;