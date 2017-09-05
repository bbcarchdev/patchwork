## Patchwork: A Quilt engine for serving knowledge graphs
##
## Author: Mo McRoberts <mo.mcroberts@bbc.co.uk>
##
## Copyright (c) 2014-2016 BBC
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

DIST_SUBDIRS = m4 docbook-html5

EXTRA_DIST = LICENSE-2.0 README.md

AM_CPPFLAGS = @AM_CPPFLAGS@ @LIBQUILT_CPPFLAGS@ @LIBSQL_CPPFLAGS@

quiltmodule_LTLIBRARIES = patchwork.la

patchwork_la_SOURCES = p_patchwork.h \
	module.c request.c home.c index.c item.c sparql.c db.c s3.c \
	file.c query.c

patchwork_la_LDFLAGS = -no-undefined -module -avoid-version

patchwork_la_LIBADD = @LIBQUILT_LOCAL_LIBS@ @LIBQUILT_LIBS@ \
	@LIBAWSCLIENT_LOCAL_LIBS@ @LIBAWSCLIENT_LIBS@ \
	@LIBSQL_LOCAL_LIBS@ @LIBSQL_LIBS@

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
