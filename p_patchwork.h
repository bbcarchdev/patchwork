/* This engine processes requests for coreference graphs populated
 * by Twine's "spindle" post-processing module.
 *
 * Author: Mo McRoberts <mo.mcroberts@bbc.co.uk>
 *
 * Copyright (c) 2014-2017 BBC
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

#ifndef P_PATCHWORK_H_
# define P_PATCHWORK_H_                1

# define _BSD_SOURCE                   1
# define QUILT_LEGACY_REQUEST_STRUCT   1

# include <stdio.h>
# include <stdlib.h>
# include <string.h>
# include <ctype.h>
# include <errno.h>
# include <libsparqlclient.h>
# include <libawsclient.h>
# include <libsql.h>

# include "libquilt.h"

# define QUILT_PLUGIN_NAME              "patchwork"

# define PATCHWORK_THRESHOLD            40

# define DEFAULT_PATCHWORK_FETCH_LIMIT	( 2 * 1024 )

# define PATCHWORK_ABOUT_MAX            6

# define MIME_NQUADS                    "application/n-quads"

/* Namespaces */
# define NS_RDF                         "http://www.w3.org/1999/02/22-rdf-syntax-ns#"
# define NS_XSD                         "http://www.w3.org/2001/XMLSchema#"
# define NS_RDFS                        "http://www.w3.org/2000/01/rdf-schema#"
# define NS_FOAF                        "http://xmlns.com/foaf/0.1/"
# define NS_POWDER                      "http://www.w3.org/2007/05/powder-s#"
# define NS_MRSS                        "http://search.yahoo.com/mrss/"
# define NS_OWL                         "http://www.w3.org/2002/07/owl#"
# define NS_SPINDLE                     "http://bbcarchdev.github.io/ns/spindle#"
# define NS_OLO                         "http://purl.org/ontology/olo/core#"
# define NS_DCTERMS                     "http://purl.org/dc/terms/"
# define NS_GEO                         "http://www.w3.org/2003/01/geo/wgs84_pos#"
# define NS_DCMITYPE                    "http://purl.org/dc/dcmitype/"
# define NS_MIME                        "http://purl.org/NET/mediatypes/"
# define NS_ODRL                        "http://www.w3.org/ns/odrl/2/"
# define NS_XHTML                       "http://www.w3.org/1999/xhtml/vocab#"
# define NS_VOID                        "http://rdfs.org/ns/void#"
# define NS_FORMATS                     "http://www.w3.org/ns/formats/"
# define NS_EVENT                       "http://purl.org/NET/c4dm/event.owl#"
# define NS_CRM                         "http://www.cidoc-crm.org/cidoc-crm/"
# define NS_SKOS                        "http://www.w3.org/2004/02/skos/core#"
# define NS_FRBR                        "http://purl.org/vocab/frbr/core#"
# define NS_OSD                         "http://a9.com/-/spec/opensearch/1.1/"
# define NS_OLO                         "http://purl.org/ontology/olo/core#"

typedef struct patchwork_struct PATCHWORK;

typedef enum
{
	QM_DEFAULT = 0,
	QM_AUTOCOMPLETE = 1
} PATCHWORKQMODE;

struct patchwork_struct
{
	struct
	{
		AWSS3BUCKET *bucket;
		char *path;
		int s3_verbose;
		size_t s3_fetch_limit;
	} cache;	  
	SQL *db;
	int db_version;
	int threshold;
	struct index_struct *indices;
	struct mediamatch_struct *mediamatch;
};

struct index_struct
{
	char *uri;
	char *title;
	char *qclass;
};

struct query_struct
{
	/* Query mode */
	PATCHWORKQMODE mode;
	/* What's the base URI for this result-set? */
	char *base;
	/* What's the actual URI for this page of results? */
	char *resource;
	/* Is this an explicit search for something, or just an index of items? */
	int explicit;
	/* Query within a collection */
	const char *collection;
	/* Find things about these topics... */
	const char *const *about;
	/* Should 'about' queries be AND (1) or OR (0)? */
	int aboutmode;
	QUILTCANON *rcanon;
	/* Item class query */
	const char *qclass;
	/* Item text query */
	const char *text;
	const char *lang;
	/* Related media query */
	const char *media;
	const char *const *audience;
	const char *type;
	/* Query bounds */
	int limit;
	int offset;
	/* Set after a query has been processed if there are more results */
	int more;
	/* Score threshold */
	int score;
	/* Minimum and maximum durations of media */
	int duration_min;
	int duration_max;
};

struct mediamatch_struct
{
	const char *name;
	const char *uri;
};

struct patchwork_dynamic_endpoint
{
	const char *path;
	size_t pathlen;
	int (*process)(QUILTREQ *req, struct patchwork_dynamic_endpoint *endpoint);
};

extern PATCHWORK *patchwork;

int patchwork_process(QUILTREQ *request);

int patchwork_index(QUILTREQ *req, const char *qclass);
int patchwork_home(QUILTREQ *req);
int patchwork_item(QUILTREQ *req);
int patchwork_item_related(QUILTREQ *request, const char *id);
int patchwork_lookup(QUILTREQ *req, const char *uri);

int patchwork_add_concrete(QUILTREQ *request);

int patchwork_array_contains(const char *const *array, const char *value);

/* Initialise a query structure */
int patchwork_query_init(struct query_struct *dest);
/* Free resources used by a query structure */
int patchwork_query_free(struct query_struct *query);
/* Populate an empty query_struct from a QUILTREQ */
int patchwork_query_request(struct query_struct *dest, QUILTREQ *req, const char *qclass);
/* Perform a query */
int patchwork_query(QUILTREQ *request, struct query_struct *query);
/* Generate query metadata */
int patchwork_query_meta(QUILTREQ *request, struct query_struct *query);
/* Add OpenSearch metadata to roots */
int patchwork_query_osd(QUILTREQ *request);
/* Determine what collections something is part of */
int patchwork_membership(QUILTREQ *request, const char *id);

/* SQL back-end */
int patchwork_db_init(void);

int patchwork_query_db(QUILTREQ *request, struct query_struct *query);
int patchwork_lookup_db(QUILTREQ *request, const char *target);
int patchwork_audiences_db(QUILTREQ *request, struct query_struct *query);
int patchwork_membership_db(QUILTREQ *request, const char *id);
int patchwork_item_db(QUILTREQ *request, const char *id);

/* SPARQL back-end */
int patchwork_query_sparql(QUILTREQ *request, struct query_struct *query);
int patchwork_lookup_sparql(QUILTREQ *request, const char *target);
int patchwork_item_sparql(QUILTREQ *req, const char *id);

/* Caches */
int patchwork_cache_init(void);

/* S3 cache back-end */
int patchwork_item_s3(QUILTREQ *req, const char *id);

/* File cache back-end */
int patchwork_item_file(QUILTREQ *request, const char *id);

#endif /*!P_PATCHWORK_H_*/
