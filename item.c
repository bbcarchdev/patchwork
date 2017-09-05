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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "p_patchwork.h"

static int patchwork_item_is_collection_(QUILTREQ *req);
static int patchwork_item_postprocess_(QUILTREQ *req);

/* Given an item's URI, attempt to redirect to it */
int
patchwork_lookup(QUILTREQ *request, const char *target)
{
	quilt_canon_set_param(request->canonical, "uri", target);
	if(patchwork_db)
	{
		return patchwork_lookup_db(request, target);
	}
	return patchwork_lookup_sparql(request, target);
}

/* Fetch an item */
int
patchwork_item(QUILTREQ *request)
{
	int r;

	if(patchwork_bucket)
	{
		r = patchwork_item_s3(request);
	}
	else if(patchwork_cachepath)
	{
		r = patchwork_item_file(request);
	}
	else
	{
		r = patchwork_item_sparql(request);
	}
	if(r != 200 && patchwork_db)
	{
		/* If no data was retrieved from caches, synthesise it
		 * from the database (#106)
		 */
		r = patchwork_item_db(request);
	}
	if(r != 200)
	{
		return r;
	}
	r = patchwork_item_postprocess_(request);
	if(r != 200)
	{
		return r;
	}
	r = patchwork_membership(request);
	if(r != 200)
	{
		return r;
	}
	r = patchwork_item_related(request);
	if(r != 200)
	{
		return r;
	}
	r = patchwork_add_concrete(request);
	if(r != 200)
	{
		return r;
	}
	/* Return 200 to auto-serialise */
	return 200;
}

/* Fetch additional metdata about an item
 * (invoked automatically by patchwork_item())
 */
int
patchwork_item_related(QUILTREQ *request)
{
	struct query_struct query;
	int r;

	patchwork_query_init(&query);
	if(patchwork_item_is_collection_(request))
	{
		query.collection = request->subject;
		r = patchwork_query_request(&query, request, NULL);
		if(r != 200)
		{
			return r;
		}
		r = patchwork_query(request, &query);
		if(r != 200)
		{
			return r;
		}
		r = patchwork_query_meta(request, &query);
		if(r != 200)
		{
			return r;
		}
		r = patchwork_query_osd(request);
		if(r != 200)
		{
			return r;
		}
		return 200;
	}
	query.related = request->subject;
	r = patchwork_query(request, &query);
	if(r != 200)
	{
		return r;
	}
	return 200;
}

static int
patchwork_item_postprocess_(QUILTREQ *request)
{
	char *uri, *abstracturi;
	librdf_world *world;
	librdf_model *model;
	librdf_node *abstract, *graph, *node, *sameas, *coref;
	librdf_uri *corefuri;
	librdf_statement *query, *st, *newst;
	librdf_stream *stream;

	world = quilt_librdf_world();
	model = quilt_request_model(request);
	graph = quilt_request_graph(request);
	/* Move anything in the abstract document graph to the concrete graph */
	abstracturi = quilt_canon_str(request->canonical, QCO_ABSTRACT);	
	abstract = quilt_node_create_uri(abstracturi);
	if(!librdf_node_equals(abstract, graph))
	{
		stream = librdf_model_context_as_stream(model, abstract);
		librdf_model_context_add_statements(model, graph, stream);
		librdf_free_stream(stream);
		librdf_model_context_remove_statements(model, abstract);
	}
	librdf_free_node(abstract);
	free(abstracturi);
	/* Find any ?s owl:sameAs <subject> triples and flip them around */
	uri = quilt_canon_str(request->canonical, QCO_SUBJECT);
	node = quilt_node_create_uri(uri);
	sameas = quilt_node_create_uri(NS_OWL "sameAs");
	query = librdf_new_statement_from_nodes(world, NULL, sameas, NULL);
	stream = librdf_model_find_statements(model, query);
	for(; stream && !librdf_stream_end(stream); librdf_stream_next(stream))
	{
	    st = librdf_stream_get_object(stream);
		coref = librdf_statement_get_subject(st);
		if(librdf_node_is_resource(coref))
		{
			corefuri = librdf_node_get_uri(coref);
			newst = quilt_st_create_uri(uri, NS_OWL "sameAs", (const char *) librdf_uri_as_string(corefuri));
			librdf_model_context_add_statement(model, graph, newst);
			librdf_free_statement(newst);
		}
	}
	librdf_free_stream(stream);
	librdf_free_statement(query);
	free(uri);
	return 200;
}

static int
patchwork_item_is_collection_(QUILTREQ *req)
{
	librdf_statement *query;
	librdf_stream *stream;
	int r;
	char *uri;
	
	uri = quilt_canon_str(req->canonical, QCO_SUBJECT);	
	/* Look for <subject> a dmcitype:Collection */
	query = quilt_st_create_uri(uri, NS_RDF "type", NS_DCMITYPE "Collection");
	free(uri);
	stream = librdf_model_find_statements(req->model, query);
	if(!stream)
	{
		librdf_free_statement(query);
		return 0;
	}
	r = librdf_stream_end(stream) ? 0 : 1;
	librdf_free_stream(stream);
	librdf_free_statement(query);
	return r;
}
