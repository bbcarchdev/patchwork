## Patchwork: A Quilt engine for serving knowledge graphs
##
## Author: Mo McRoberts <mo.mcroberts@bbc.co.uk>
##
## Copyright (c) 2014-2017 BBC
##
##  Licensed under the Apache License, Version 2.0 (the "License");
##  you may not use this file except in compliance with the License.
##  You may obtain a copy of the License at
##
##      http://www.apache.org/licenses/LICENSE-2.0
##
##  Unless required by applicable law or agreed to in writing, software
##  distributed under the License is distributed on an "AS IS" BASIS,
##  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
##  See the License for the specific language governing permissions and
##  limitations under the License.

ACLOCAL_AMFLAGS = -I m4

DIST_SUBDIRS = m4 docbook-html5 cache db graphstore

SUBDIRS = cache db graphstore .

EXTRA_DIST = LICENSE-2.0 README.md

AM_CPPFLAGS = @AM_CPPFLAGS@ @LIBQUILT_CPPFLAGS@ @LIBSQL_CPPFLAGS@ \
	@LIBSPARQLCLIENT_CPPFLAGS@

quiltmodule_LTLIBRARIES = patchwork.la

patchwork_la_SOURCES = p_patchwork.h \
	module.c request.c home.c index.c item.c query.c

patchwork_la_LDFLAGS = -no-undefined -module -avoid-version

patchwork_la_LIBADD = \
	cache/libcache.la \
	db/libdb.la \
	graphstore/libgraphstore.la

## Maintainer targets

BRANCH ?= develop
DOCS_BRANCH ?= gh-pages

DEVELOP_SUBMODULES = m4
DOCS_SUBMODULES = docbook-html5

checkout:
	@for submod in $(DEVELOP_SUBMODULES) ; do \
		echo "+++ Checking out the latest version of $$submod on $(BRANCH)" >&2 ; \
		cd $$submod && \
		git checkout $(BRANCH) && \
		git pull && \
		$(MAKE) checkout BRANCH=$(BRANCH) || exit $? && \
		cd .. ; \
	done
	@for submod in $(DOCS_SUBMODULES) ; do \
		echo "+++ Checking out the latest version of $$submod on $(DOCS_BRANCH)" >&2 ; \
		cd $$submod && \
		git checkout $(DOCS_BRANCH) && \
		git pull && \
		cd .. ; \
	done
