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

# include <rulebase/librulebase.h>

#include "p_patchwork.h"

static int patchwork_item_id_(QUILTREQ *request, char *idbuf);
static int patchwork_item_is_collection_(QUILTREQ *req, const char *id);
static int patchwork_item_postprocess_(QUILTREQ *req, const char *id);
static int str_has_prefix_from_list_(const char *const *list, const char *str);

/* Given an item's URI, attempt to redirect to it */
int
patchwork_lookup(QUILTREQ *request, const char *target)
{
	quilt_canon_set_param(request->canonical, "uri", target);
	if(patchwork->db)
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
	char idbuf[36], *uri;

	r = patchwork_item_id_(request, idbuf);
	if(r)
	{
		return r;
	}
	/* XXX Check for a sub-graph */
	/* Set the canonical URI & subject */
	quilt_canon_add_path(request->canonical, idbuf);
	quilt_canon_set_fragment(request->canonical, "#id");
	uri = quilt_canon_str(request->canonical, QCO_SUBJECT);
	quilt_request_set_subject_uristr(request, uri);
	quilt_logf(LOG_DEBUG, QUILT_PLUGIN_NAME ": item: canonical URI is <%s>\n", uri);
	free(uri);

	if(patchwork->cache.bucket)
	{
		r = patchwork_item_s3(request, idbuf);
	}
	else if(patchwork->cache.path)
	{
		r = patchwork_item_file(request, idbuf);
	}
	else
	{
		r = patchwork_item_sparql(request, idbuf);
	}
	if(r != 200 && patchwork->db)
	{
		/* If no data was retrieved from caches, synthesise it
		 * from the database (#106)
		 */
		r = patchwork_item_db(request, idbuf);
	}
	if(r != 200)
	{
		return r;
	}
	r = patchwork_item_postprocess_(request, idbuf);
	if(r != 200)
	{
		return r;
	}
	r = patchwork_membership(request, idbuf);
	if(r != 200)
	{
		return r;
	}
	r = patchwork_item_related(request, idbuf);
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
patchwork_item_related(QUILTREQ *request, const char *id)
{
	struct query_struct query;
	int r;
	const char *about[2];

	patchwork_query_init(&query);
	about[0] = id;
	about[1] = NULL;
	query.about = about;
	if(patchwork_item_is_collection_(request, id))
	{
		quilt_logf(LOG_DEBUG, QUILT_PLUGIN_NAME ": item: is collection and subject is <%s>\n", request->subject);
		query.collection = request->subject; // seems to be missing "#id" when evaluated later - does it have it here?
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
	r = patchwork_query(request, &query);
	if(r != 200)
	{
		return r;
	}
	return 200;
}

static int
patchwork_item_postprocess_(QUILTREQ *request, const char *id)
{
	char *uri, *abstracturi;
	unsigned char *corefuri_str, *ctx_str;
	/* statement context prefix whitelist */
	const char *const *whitelist;
	librdf_world *world;
	librdf_model *model;
	librdf_node *abstract, *graph, *node, *sameas, *coref, *ctx;
	librdf_statement *query, *st, *newst;
	librdf_stream *stream;

	(void) id;

	quilt_logf(LOG_DEBUG, QUILT_PLUGIN_NAME ": patchwork_item_postprocess_(): post-processing item\n");

	world = quilt_librdf_world();
	model = quilt_request_model(request);
	graph = quilt_request_graph(request);
	quilt_logf(LOG_DEBUG, QUILT_PLUGIN_NAME ": patchwork_item_postprocess_(): graph context has uri <%s>\n", librdf_uri_as_string(librdf_node_get_uri(graph)));
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

	/* If appropriate, strip triples from graphs not in given whitelist */
	/* imitate dataset partitioning by whitelisting statement context prefixes */
	whitelist = quilt_request_getparam_multi(request, "allow");
	if(whitelist && *whitelist)
	{
		librdf_iterator *contexts;
		librdf_node *context;
		char *context_uri_str;

		quilt_logf(LOG_DEBUG, QUILT_PLUGIN_NAME ": patchwork_item_postprocess_(): dumping model before...\n");
		librdf_model_print(model, stderr);
		contexts = librdf_model_get_contexts(model);
		while(!librdf_iterator_end(contexts))
		{
			context = librdf_iterator_get_object(contexts);
			context_uri_str = (char *) librdf_uri_as_string(librdf_node_get_uri(context));
			quilt_logf(LOG_DEBUG, QUILT_PLUGIN_NAME ": patchwork_item_postprocess_(): found context <%s>\n", context_uri_str);
			if(!str_has_prefix_from_list_(whitelist, (const char *) context_uri_str))
			{
				quilt_logf(LOG_DEBUG, QUILT_PLUGIN_NAME ": patchwork_item_postprocess_(): stripping context <%s>\n", context_uri_str);
				librdf_model_context_remove_statements(model, context);
			}
			librdf_iterator_next(contexts);
		}
		librdf_free_iterator(contexts);
		quilt_logf(LOG_DEBUG, QUILT_PLUGIN_NAME ": patchwork_item_postprocess_(): dumping model after...\n");
		librdf_model_print(model, stderr);

		/* TODO: here, we need to call the part of spindle that does the processing pipeline, but ignore all of the cache and db stuff */
		RULEBASE *rules;
		PROXYENTRY proxy = {0};
		rules = rulebase_create(world, model, NULL, NULL, "http://localhost/" /* root */, 0 /* multigraph */);
		proxy_entry_init(&proxy, rules, "http://localhost/65983a1410ef49e2a3e591f90291a2e0#id" /* localname */, graph);
		/* Update proxy classes */
		quilt_logf(LOG_DEBUG, QUILT_PLUGIN_NAME ": updating classes\n");
		if(rulebase_class_update_entry(&proxy) < 0)
		{
			return -1;
		}
		/* Update proxy properties */
		quilt_logf(LOG_DEBUG, QUILT_PLUGIN_NAME ": updating properties\n");
		rulebase_prop_update_entry(&proxy, NS_RDFS "label" /* titlepred */, NULL, NULL);
		proxy_entry_dump(&proxy);
		proxy_entry_cleanup(&proxy);

		librdf_model_print(model, stderr);
		rulebase_destroy(rules);
	}

	/* Find any ?s owl:sameAs <subject> triples and flip them around */
	uri = quilt_canon_str(request->canonical, QCO_SUBJECT);
	node = quilt_node_create_uri(uri);
	sameas = quilt_node_create_uri(NS_OWL "sameAs");
	query = librdf_new_statement_from_nodes(world, NULL, sameas, node);
	stream = librdf_model_find_statements(model, query);
	for(; stream && !librdf_stream_end(stream); librdf_stream_next(stream))
	{
	    st = librdf_stream_get_object(stream);
		coref = librdf_statement_get_subject(st);
		if(librdf_node_is_resource(coref))
		{
			ctx = librdf_stream_get_context2(stream);
			ctx_str = librdf_uri_as_string(librdf_node_get_uri(ctx));
			corefuri_str = librdf_uri_as_string(librdf_node_get_uri(coref));
			quilt_logf(LOG_DEBUG, QUILT_PLUGIN_NAME ": patchwork_item_postprocess_(): flipping source triple <%s> owl:sameAs <%s> with context <%s>\n", corefuri_str, uri, ctx_str);
			newst = quilt_st_create_uri(uri, NS_OWL "sameAs", (const char *) corefuri_str);
			librdf_model_context_add_statement(model, ctx, newst);
			librdf_free_statement(newst);
		}
	}
	librdf_free_stream(stream);
	librdf_free_statement(query);
	free(uri);
	return 200;
}

static int
str_has_prefix_from_list_(const char *const *list, const char *str)
{
	for(; list && *list; list++)
	{
		if(strncmp(str, *list, strlen(*list)) == 0)
		{
			return 1;
		}
	}
	return 0;
}

static int
patchwork_item_is_collection_(QUILTREQ *req, const char *id)
{
	librdf_statement *query;
	librdf_stream *stream;
	int r;
	char *uri;

	(void) id;

	uri = quilt_canon_str(req->canonical, QCO_SUBJECT);
	quilt_logf(LOG_DEBUG, QUILT_PLUGIN_NAME ": item: looking to see if <%s> is a dcmitype:Collection\n", uri);
	/* Look for <subject> a dmcitype:Collection */
	/* XXX should be config-driven */
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

/* Given a request, determine the UUID of the item being requested */
static int
patchwork_item_id_(QUILTREQ *request, char *idbuf)
{
	const char *seg;
	char *p;

	seg = quilt_request_consume(request);
	if(!seg)
	{
		return 404;
	}

	for(p = idbuf; *seg; seg++)
	{
		if(*seg == '-')
		{
			continue;
		}
		if(isalnum(*seg))
		{
			*p = tolower(*seg);
			p++;
			continue;
		}
		return 404;
	}
	*p = 0;
	if(strlen(idbuf) != 32)
	{
		return 404;
	}
	return 0;
}
