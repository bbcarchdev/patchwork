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

static char *patchwork_query_subjtitle_(QUILTREQ *request, const char *abstract, const char *primary, const char *secondary);
static int patchwork_query_title_(QUILTREQ *request, const char *abstract, struct query_struct *query);


/* Initialise a query_struct */
int
patchwork_query_init(struct query_struct *dest)
{
	memset(dest, 0, sizeof(struct query_struct));
	dest->score = -1;
	return 0;
}

/* Populate an empty query_struct from a QUILTREQ */
int
patchwork_query_request(struct query_struct *dest, QUILTREQ *request, const char *qclass)
{
	const char *t;

	/* Textual query */
	t = quilt_request_getparam(request, "q");
	if(t && t[0])
	{
		dest->explicit = 1;
		quilt_canon_set_param(request->canonical, "q", t);
		dest->text = t;
		dest->lang = quilt_request_getparam(request, "lang");
	}
	/* Filter by entity collection */
	t = quilt_request_getparam(request, "collection");
	if(t && t[0])
	{
		dest->explicit = 1;
		quilt_canon_set_param(request->canonical, "collection", t);
		dest->collection = t;
	}
	/* Filter by entity class */
	t = quilt_request_getparam(request, "class");
	if(t && t[0])
	{
		dest->explicit = 1;
		quilt_canon_set_param(request->canonical, "class", t);
		dest->qclass = t;
	}
	else if(qclass && qclass[0])
	{
		dest->qclass = qclass;
	}
	/* Offset and limit */
	dest->offset = request->offset;
	if(request->offset)
	{
		quilt_canon_set_param_int(request->canonical, "offset", request->offset);
	}
	dest->limit = request->limit;
	if(request->limit != request->deflimit)
	{
		quilt_canon_set_param_int(request->canonical, "limit", request->limit);
	}
	/* Media queries */
	dest->media = quilt_request_getparam(request, "media");
	if(dest->media)
	{
		quilt_canon_set_param(request->canonical, "media", dest->media);
		dest->explicit = 1;
	}
	/* Duration queries */
	dest->duration_min = quilt_request_getparam_int(request, "duration-min");
	if(dest->duration_min)
	{
		quilt_canon_set_param_int(request->canonical, "duration-min", dest->duration_min);
		dest->explicit = 1;
	}
	dest->duration_max = quilt_request_getparam_int(request, "duration-max");
	if(dest->duration_max)
	{
		quilt_canon_set_param_int(request->canonical, "duration-max", dest->duration_max);
		dest->explicit = 1;
	}
	/* Deal with topical queries (about=xxx) */
	dest->about = quilt_request_getparam_multi(request, "about");
	if(dest->about)
	{
		quilt_canon_set_param_multi(request->canonical, "about", dest->about);
		dest->explicit = 1;
	}
	/* Restricted-audience group queries */
	dest->audience = quilt_request_getparam_multi(request, "for");
	if(dest->audience)
	{
		quilt_canon_set_param_multi(request->canonical, "for", dest->audience);
		dest->explicit = 1;
	}
	/* Media MIME type queries */
	dest->type = quilt_request_getparam(request, "type");
	if(dest->type && dest->type[0])
	{
		dest->explicit = 1;
	}
	if(dest->type && strcmp(dest->type, "any"))
	{
		quilt_canon_set_param(request->canonical, "type", dest->type);
	}
	/* Query mode */
	t = quilt_request_getparam(request, "mode");
	if(t && t[0])
	{
		dest->explicit = 1;
		if(!strcmp(t, "autocomplete"))
		{
			dest->mode = QM_AUTOCOMPLETE;
		}
		else
		{
			t = NULL;
		}
		if(t)
		{
			quilt_canon_set_param(request->canonical, "mode", t);
		}
	}
	/* Score threshold */
	t = quilt_request_getparam(request, "score");
	if(t && t[0])
	{
		dest->explicit = 1;
		dest->score = atoi(t);
		quilt_canon_set_param(request->canonical, "score", t);
	}
	if(dest->score == -1)
	{
		dest->score = patchwork->threshold;
	}
	return 200;
}

/* Free resources used by a query structure */
int
patchwork_query_free(struct query_struct *query)
{
	free(query->base);
	free(query->resource);
	return 0;
}

/* Perform a query (using either a database or SPARQL back-ends) */
int
patchwork_query(QUILTREQ *request, struct query_struct *query)
{
	int r;

	if(!query->base)
	{
		query->base = quilt_canon_str(request->canonical, QCO_ABSTRACT);
	}
	if(!query->resource)
	{
		query->resource = quilt_canon_str(request->canonical, QCO_DEFAULT|QCO_USERSUPPLIED);
		if(query->explicit || request->index)
		{
			quilt_request_set_subject_uristr(request, query->resource);
		}
	}
	if(query->about && query->about[0] && !query->about[1])
	{
		/* If there's a single 'about' query, define a canonical URI
		 * for the subject
		 */
		query->rcanon = quilt_canon_create(request->canonical);
		quilt_canon_set_base(query->rcanon, request->base);
		quilt_canon_add_path(query->rcanon, query->about[0]);
		quilt_canon_set_fragment(query->rcanon, "id");
	}
	quilt_logf(LOG_DEBUG, QUILT_PLUGIN_NAME ": query.c:patchwork_query(): base <%s>\n", request->base);
	if(patchwork->db)
	{
		r = patchwork_query_db(request, query);
	}
	else
	{
		r = patchwork_query_sparql(request, query);
	}
	if(query->rcanon)
	{
		quilt_canon_destroy(query->rcanon);
	}
	return r;
}

/* Generate information about the query, such as navigational links */
int
patchwork_query_meta(QUILTREQ *request, struct query_struct *query)
{
	QUILTCANON *link;
	char *linkstr;
	int c;
	librdf_statement *st;
	librdf_node *graph;

	graph = quilt_request_graph(request);
	if(request->offset)
	{
		/* If the request had an offset, link to the previous page */
		link = quilt_canon_create(request->canonical);
		c = request->offset - request->limit;
		if(c < 0)
		{
			c = 0;
		}
		if(c)
		{
			quilt_canon_set_param_int(link, "offset", c);
		}
		else
		{
			quilt_canon_set_param(link, "offset", NULL);
		}
		linkstr = quilt_canon_str(link, QCO_DEFAULT);
		st = quilt_st_create_uri(query->resource, NS_XHTML "prev", linkstr);
		librdf_model_context_add_statement(request->model, graph, st);
		librdf_free_statement(st);
		free(linkstr);
		quilt_canon_destroy(link);
	}
	if(query->more)
	{
		/* ... xhv:next </?offset=...> */
		link = quilt_canon_create(request->canonical);
		quilt_canon_set_param_int(link, "offset", request->offset + request->limit);
		linkstr = quilt_canon_str(link, QCO_DEFAULT);
		st = quilt_st_create_uri(query->resource, NS_XHTML "next", linkstr);
		librdf_model_context_add_statement(request->model, graph, st);
		librdf_free_statement(st);
		free(linkstr);
		quilt_canon_destroy(link);
	}
	if(strcmp(query->resource, query->base))
	{
		st = quilt_st_create_uri(query->resource, NS_DCTERMS "isPartOf", query->base);
		librdf_model_context_add_statement(request->model, graph, st);
		librdf_free_statement(st);

		st = quilt_st_create_uri(query->base, NS_RDF "type", NS_VOID "Dataset");
		librdf_model_context_add_statement(request->model, graph, st);
		librdf_free_statement(st);

		if(request->indextitle)
		{
			st = quilt_st_create_literal(query->base, NS_RDFS "label", request->indextitle, "en-gb");
			librdf_model_context_add_statement(request->model, graph, st);
			librdf_free_statement(st);
		}
	}
	/* ... rdf:type void:Dataset */
	st = quilt_st_create_uri(query->resource, NS_RDF "type", NS_VOID "Dataset");
	librdf_model_context_add_statement(request->model, graph, st);
	librdf_free_statement(st);

	if(request->index || query->explicit)
	{
		/* ... rdf:label */
		patchwork_query_title_(request, query->resource, query);
	}

	return 200;
}

/* Add OpenSearch descriptive metadata and friends to a dataset or subset */
int
patchwork_query_osd(QUILTREQ *request)
{
	char *linkstr;
	QUILTCANON *link;
	librdf_statement *st;
	librdf_node *graph;
	const char *subj;

	subj = quilt_request_subject(request);
	graph = quilt_request_graph(request);
	link = quilt_canon_create(request->canonical);
	quilt_canon_reset_params(link);
	quilt_canon_add_param(link, "q", "{searchTerms?}");
	quilt_canon_add_param(link, "lang", "{language?}");
	quilt_canon_add_param(link, "limit", "{count?}");
	quilt_canon_add_param(link, "offset", "{startIndex?}");
	if(request->home || !request->index)
	{
		quilt_canon_add_param(link, "class", "{rdfs:Class?}");
		quilt_canon_add_param(link, "collection", "{dcmitype:Collection?}");
	}
	quilt_canon_add_param(link, "for", "{odrl:Party?}");
	quilt_canon_add_param(link, "media", "{dct:DCMIType?}");
	quilt_canon_add_param(link, "type", "{dct:IMT?}");
	if(request->home)
	{
	    quilt_canon_add_param(link, "mode", "{quilt.patchwork:queryMode?}");
	}
	quilt_canon_set_ext(link, NULL);
	linkstr = quilt_canon_str(link, QCO_ABSTRACT);
	st = quilt_st_create_literal(subj, NS_OSD "template", linkstr, NULL);
	librdf_model_context_add_statement(request->model, graph, st);
	librdf_free_statement(st);
	free(linkstr);
	quilt_canon_destroy(link);

	st = quilt_st_create_literal(subj, NS_OSD "Language", "en-gb", NULL);
	librdf_model_context_add_statement(request->model, graph, st);
	librdf_free_statement(st);

	st = quilt_st_create_literal(subj, NS_OSD "Language", "cy-gb", NULL);
	librdf_model_context_add_statement(request->model, graph, st);
	librdf_free_statement(st);

	st = quilt_st_create_literal(subj, NS_OSD "Language", "gd-gb", NULL);
	librdf_model_context_add_statement(request->model, graph, st);
	librdf_free_statement(st);

	st = quilt_st_create_literal(subj, NS_OSD "Language", "ga-gb", NULL);
	librdf_model_context_add_statement(request->model, graph, st);
	librdf_free_statement(st);

	/* XXX Why is this not part of patchwork_query_meta()? */
	if(request->home)
	{
		/* Add VoID descriptive metadata */
		st = quilt_st_create_uri(subj, NS_RDF "type", NS_VOID "Dataset");
		librdf_model_context_add_statement(request->model, graph, st);
		librdf_free_statement(st);

		link = quilt_canon_create(request->canonical);
		quilt_canon_reset_params(link);
		quilt_canon_add_param(link, "uri", "");
		linkstr = quilt_canon_str(link, QCO_ABSTRACT);
		st = quilt_st_create_uri(subj, NS_VOID "uriLookupEndpoint", linkstr);
		librdf_model_context_add_statement(request->model, graph, st);
		librdf_free_statement(st);
		free(linkstr);
		quilt_canon_destroy(link);
	}

	if(request->home)
	{
		/* ... void:openSearchDescription </xxx.osd> */
		link = quilt_canon_create(request->canonical);
		quilt_canon_reset_params(link);
		quilt_canon_set_explicitext(link, NULL);
		quilt_canon_set_ext(link, "osd");
		linkstr = quilt_canon_str(link, QCO_CONCRETE);
		st = quilt_st_create_uri(subj, NS_VOID "openSearchDescription", linkstr);
		librdf_model_context_add_statement(request->model, graph, st);
		librdf_free_statement(st);
		free(linkstr);
		quilt_canon_destroy(link);
	}

	return 200;
}

static char *
patchwork_query_subjtitle_(QUILTREQ *request, const char *abstract, const char *primary, const char *secondary)
{
	char *pri, *sec, *none;
	const char *lang, *value;
	librdf_statement *query, *st;
	librdf_stream *stream;
	librdf_node *obj;

	pri = NULL;
	sec = NULL;
	none = NULL;
	query = quilt_st_create(abstract, NS_RDFS "label");
	for(stream = librdf_model_find_statements(request->model, query);
		!librdf_stream_end(stream);
		librdf_stream_next(stream))
	{
		st = librdf_stream_get_object(stream);
		obj = librdf_statement_get_object(st);
		if(!librdf_node_is_literal(obj))
		{
			continue;
		}
		value = (const char *) librdf_node_get_literal_value(obj);
		if(!value)
		{
			continue;
		}
		lang = librdf_node_get_literal_value_language(obj);
		if(lang)
		{
			if(!pri && primary && !strcasecmp(lang, primary))
			{
				pri = strdup(value);
			}
			if(!sec && secondary && !strcasecmp(lang, secondary))
			{
				sec = strdup(value);
			}
		}
		else if(!none)
		{
			none = strdup(value);
		}
	}
	librdf_free_stream(stream);
	librdf_free_statement(query);
	if(pri)
	{
		free(sec);
		free(none);
		return pri;
	}
	if(sec)
	{
		free(none);
		return sec;
	}
	return none;
}

static int
patchwork_query_title_(QUILTREQ *request, const char *abstract, struct query_struct *query)
{
	librdf_statement *st;
	size_t len, c;
	char *buf, *p, *title_en_gb;
	int sing;

	title_en_gb = NULL;
	if(request->indextitle)
	{
		len = strlen(request->indextitle) + 1;
	}
	else if(query->qclass)
	{
		/* Items with class <...> */
		len = strlen(query->qclass) + 22;
	}
	else
	{
		/* "Everything" */
		len = 11;
	}
	if(query->collection)
	{
		/* within <...> */
		title_en_gb = patchwork_query_subjtitle_(request, abstract, "en-gb", "en");
		if(title_en_gb)
		{
			len += strlen(title_en_gb) + 16;
		}
		else
		{
			len += strlen(query->collection) + 16;
		}
	}
	if(query->text)
	{
		/* containing "..." */
		len += strlen(query->text) + 16;
	}
	if(query->media || query->type || query->audience)
	{
		/* which have related ... media */
		len += 25;
		if(query->media)
		{
			len += strlen(query->media) + 2;
		}
		if(query->type)
		{
			/* which is ... */
			len += strlen(query->type) + 10;
		}
		if(query->audience)
		{
			/* available to [everyone | <audience>] */
			size_t i=0;
			while(query->audience && (query->audience[i] != NULL)) {
				len += strlen(query->audience[i]) + 22;
				i++;
			}
		}
	}
	buf = (char *) malloc(len + 1);
	if(!buf)
	{
		quilt_logf(LOG_CRIT, QUILT_PLUGIN_NAME ": failed to allocate %lu bytes for index title buffer\n", (unsigned long) len + 1);
		return -1;
	}
	p = buf;
	sing = 0;
	if(request->indextitle)
	{
		strcpy(p, request->indextitle);
		p = strchr(p, 0);
		/* We should have a flag indicating collective/singular form */
		if(!strcasecmp(request->indextitle, "everything"))
		{
			sing = 1;
		}
	}
	else if(query->qclass)
	{
		strcpy(p, "Items with class <");
		p = strchr(p, 0);
		strcpy(p, query->qclass);
		p = strchr(p, 0);
		*p = '>';
		p++;
	}
	else
	{
		strcpy(p, "Everything");
		p = strchr(p, 0);
		sing = 1;
	}
	if(query->collection)
	{
		if(title_en_gb)
		{
			strcpy(p, " within “");
			p = strchr(p, 0);
			strcpy(p, title_en_gb);
			p = strchr(p, 0);
			strcpy(p, "”");
			p = strchr(p, 0);
		}
		else
		{
			strcpy(p, " within <");
			p = strchr(p, 0);
			strcpy(p, query->collection);
			p = strchr(p, 0);
			*p = '>';
			p++;
		}
	}
	if(query->text)
	{
		strcpy(p, " containing \"");
		p = strchr(p, 0);
		strcpy(p, query->text);
		p = strchr(p, 0);
		*p = '"';
		p++;
	}
	if(query->media || query->type || query->audience)
	{
		/* which have */
		if(sing)
		{
			strcpy(p, " which has related");
		}
		else
		{
			strcpy(p, " which have related");
		}
		p = strchr(p, 0);
		for(c = 0; patchwork->mediamatch[c].name; c++)
		{
			if(query->media && !strcmp(patchwork->mediamatch[c].uri, query->media))
			{
				*p = ' ';
				p++;
				strcpy(p, patchwork->mediamatch[c].name);
				p = strchr(p, 0);
				break;
			}
		}
		if(!patchwork->mediamatch[c].name)
		{
			if(query->media && strcmp(query->media, "any"))
			{
				*p = ' ';
				p++;
				*p = '<';
				p++;
				strcpy(p, query->media);
				p = strchr(p, 0);
				*p = '>';
				p++;
			}
			strcpy(p, " media");
			p = strchr(p, 0);
		}
		if(query->type && strcmp(query->type, "any"))
		{
			strcpy(p, " which is ");
			p = strchr(p, 0);
			strcpy(p, query->type);
			p = strchr(p, 0);
		}
		if(query->audience && !patchwork_array_contains(query->audience, "any"))
		{
			if(patchwork_array_contains(query->audience, "all"))
			{
				strcpy(p, " available to everyone");
				p = strchr(p, 0);
			}
			else
			{
				strcpy(p, " available to <");
				size_t i=0;
				while(query->audience[i] != NULL)
				{
					strcat(p, query->audience[i++]);
					if (query->audience[i] != NULL)
					{
						strcat(p, ", ");
					}
				}
				strcat(p, ">");
			}
		}
	}
	*p = 0;

	st = quilt_st_create_literal(abstract, NS_RDFS "label", buf, "en-gb");
	librdf_model_context_add_statement(request->model, request->graph, st);
	librdf_free_statement(st);
	free(buf);
	free(title_en_gb);
	return 0;
}

int
patchwork_membership(QUILTREQ *request, const char *id)
{
	if(patchwork->db)
	{
		return patchwork_membership_db(request, id);
	}
	return 200;
}
