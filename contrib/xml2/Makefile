# contrib/xml2/Makefile

MODULE_big = pgxml
OBJS = xpath.o xslt_proc.o schema_datatypes.o  xmlindex.o xml_index_loader.o xml_validation.o

EXTENSION = xml2
DATA = xml2--1.0.sql xml2--unpackaged--1.0.sql

REGRESS = xml2

SHLIB_LINK += $(filter -lxslt, $(LIBS)) $(filter -lxml2, $(LIBS))

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/xml2
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
