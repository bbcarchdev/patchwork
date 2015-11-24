/* This engine processes requests for coreference graphs populated
 * by Twine's "spindle" post-processing module.
 *
 * Author: Mo McRoberts <mo.mcroberts@bbc.co.uk>
 *
 * Copyright (c) 2014-2015 BBC
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "p_spindle.h"

static int spindle_request_is_query_(QUILTREQ *req);
static const char *spindle_request_is_lookup_(QUILTREQ *req);
static int spindle_request_is_partition_(QUILTREQ *req, char **qclass);
static int spindle_request_is_item_(QUILTREQ *req);

int
spindle_process(QUILTREQ *request)
{
	char *qclass;
	int r;
	const char *uri;

	/* Process a request and determine how it should be handled.
	 *
	 * In order of preference:
	 *
	 * - Requests for partitions (look-up against our static list)
	 * - Requests for items (pattern match)
	 * - (Future) Requests for endpoints that are generated on the fly
	 *   (such as /audiences)
	 * - URI lookup queries
	 * - Queries at the index, if no path parameters
	 */
	
	qclass = NULL;
	if(spindle_request_is_partition_(request, &qclass))
	{
		r = spindle_index(request, qclass);
		free(qclass);
		return r;
	}
	if(spindle_request_is_item_(request))
	{
		return spindle_item(request);
	}
	uri = spindle_request_is_lookup_(request);
	if(uri)
	{
		return spindle_lookup(request, uri);
	}
	if(spindle_request_is_query_(request))
	{
		return spindle_index(request, NULL);
	}
	if(request->home)
	{
		return spindle_home(request);
	}
	return 404;
}

/* Add information to the model about relationship between the concrete and
 * abstract documents
 */
int
spindle_add_concrete(QUILTREQ *request)
{
	const char *s;
	char *subject, *abstract, *concrete, *typebuf;
	librdf_statement *st;
	int explicit;

	explicit = (request->ext != NULL);
	abstract = quilt_canon_str(request->canonical, (explicit ? QCO_ABSTRACT : QCO_REQUEST));
	concrete = quilt_canon_str(request->canonical, (explicit ? QCO_REQUEST : QCO_CONCRETE));
	subject = quilt_canon_str(request->canonical, QCO_NOEXT|QCO_FRAGMENT);
	
	/* abstract foaf:primaryTopic subject */
	st = quilt_st_create_uri(abstract, NS_FOAF "primaryTopic", subject);
	librdf_model_add_statement(request->model, st);
	librdf_free_statement(st);	

	/* abstract dct:hasFormat concrete */
	st = quilt_st_create_uri(abstract, NS_DCTERMS "hasFormat", concrete);
	librdf_model_add_statement(request->model, st);
	librdf_free_statement(st);

	/* concrete rdf:type ... */
	st = quilt_st_create_uri(concrete, NS_RDF "type", NS_DCMITYPE "Text");
	librdf_model_add_statement(request->model, st);
	librdf_free_statement(st);	
	s = NULL;
	if(!strcmp(request->type, "text/turtle"))
	{
		s = NS_FORMATS "Turtle";
	}
	else if(!strcmp(request->type, "application/rdf+xml"))
	{
		s = NS_FORMATS "RDF_XML";
	}
	else if(!strcmp(request->type, "text/rdf+n3"))
	{
		s = NS_FORMATS "N3";
	}
	if(s)
	{
		st = quilt_st_create_uri(concrete, NS_RDF "type", s);
		librdf_model_add_statement(request->model, st);
		librdf_free_statement(st);
	}

	typebuf = (char *) malloc(strlen(NS_MIME) + strlen(request->type) + 1);
	if(typebuf)
	{
		strcpy(typebuf, NS_MIME);
		strcat(typebuf, request->type);
		st = quilt_st_create_uri(concrete, NS_DCTERMS "format", typebuf);
		librdf_model_add_statement(request->model, st);
		librdf_free_statement(st);
		free(typebuf);
	}

	free(abstract);
	free(concrete);

	return 200;
}

/* Is this a request constituting a query for something against the index?
 *
 * Note that this only applies at the root - if we already know it's a
 * non-home index then the query will be performed automatically
 */
static int
spindle_request_is_query_(QUILTREQ *request)
{
	if(!request->home)
	{
		return 0;
	}
	if(quilt_request_getparam(request, "q") ||
	   quilt_request_getparam(request, "media") ||
	   quilt_request_getparam(request, "for") ||
	   quilt_request_getparam(request, "type"))
	{
		request->index = 1;
		request->home = 0;
		return 1;
	}
	return 0;
}

/* Is this a request for a (potential) item? */
static int
spindle_request_is_item_(QUILTREQ *request)
{
	size_t l;
	const char *t;

	for(t = request->path; *t == '/'; t++);
	if(!*t)
	{
		return 0;
	}
	for(l = 0; isalnum(*t); t++)
	{
		l++;
	}
	if(*t && *t != '/')
	{
		return 0;
	}
	if(l != 32)
	{
		return 0;
	}
	return 1;
}

/* Is this a request for a class partition? If so, return the URI string */
static int
spindle_request_is_partition_(QUILTREQ *request, char **qclass)
{
	const char *t;
	size_t c;
	
	*qclass = NULL;
	/* First check to determine whether there's a match against the list */
	for(c = 0; spindle_indices[c].uri; c++)
	{
		if(!strcmp(request->path, spindle_indices[c].uri))
		{
			if(spindle_indices[c].qclass)
			{
				*qclass = (char *) calloc(1, 32 + strlen(spindle_indices[c].qclass));
				if(spindle_db)
				{
					strcpy(*qclass, spindle_indices[c].qclass);
				}
				else
				{
					sprintf(*qclass, "FILTER ( ?class = <%s> )", spindle_indices[c].qclass);
				}
			}
			request->indextitle = spindle_indices[c].title;		   
			request->index = 1;
			request->home = 0;
			quilt_canon_add_path(request->canonical, spindle_indices[c].uri);
			return 1;
		}
	}
	/* Check for an explicit ?class=... parameter at the root */
	t = quilt_request_getparam(request, "class");
	if(t && request->home)
	{
		quilt_canon_set_param(request->canonical, "class", t);
		*qclass = (char *) calloc(1, 32 + strlen(t));
		if(spindle_db)
		{
			strcpy(*qclass, t);
		}
		else
		{
			sprintf(*qclass, "FILTER ( ?class = <%s> )", t);
		}
		if(!request->indextitle)
		{
			request->indextitle = t;
		}
		request->index = 1;
		request->home = 0;
		return 1;
	}
	return 0;
}

static const char *
spindle_request_is_lookup_(QUILTREQ *request)
{
	if(!request->home)
	{
		return NULL;
	}
	return quilt_request_getparam(request, "uri");
}
