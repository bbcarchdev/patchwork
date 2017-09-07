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

static int patchwork_cache_init_s3_(const char *bucket);
static int patchwork_cache_init_file_(const char *path);

int
patchwork_cache_init(void)
{
	URI *base, *uri;
	URI_INFO *info;
	char *t;
	int r;
	
	if((t = quilt_config_geta(QUILT_PLUGIN_NAME ":cache", NULL)))
	{
		base = uri_create_cwd();
		uri = uri_create_str(t, base);
		info = uri_info(uri);
		uri_destroy(uri);
		uri_destroy(base);
		if(!strcmp(info->scheme, "s3"))
		{
			r = patchwork_cache_init_s3_(info->host);
		}
		else if(!strcmp(info->scheme, "file"))
		{
			r = patchwork_cache_init_file_(info->path);
		}
		else
		{
			quilt_logf(LOG_CRIT, QUILT_PLUGIN_NAME ": cache scheme '%s' is not supported in URI <%s>\n", info->scheme, t);
			r = -1;
		}
		uri_info_destroy(info);
		free(t);
		
	}
	else if((t = quilt_config_geta(QUILT_PLUGIN_NAME ":bucket", NULL)))
	{
		quilt_logf(LOG_WARNING, QUILT_PLUGIN_NAME ": the 'bucket' configuration option is deprecated; you should specify an S3 bucket URI as the value of the 'cache' option instead\n");
		r = patchwork_cache_init_s3_(t);
		free(t);
	}
	else
	{
		r = 0;
	}
	return r;
}

static int
patchwork_cache_init_s3_(const char *bucket)
{
	char *t;
	
	patchwork->cache.bucket = aws_s3_create(bucket);
	if(!patchwork->cache.bucket)
	{
		quilt_logf(LOG_CRIT, QUILT_PLUGIN_NAME ": failed to initialise S3 bucket '%s'\n", bucket);
		return -1;
	}
	if((t = quilt_config_geta("s3:endpoint", NULL)))
	{
		aws_s3_set_endpoint(patchwork->cache.bucket, t);
		free(t);
	}
	if((t = quilt_config_geta("s3:access", NULL)))
	{
		aws_s3_set_access(patchwork->cache.bucket, t);
		free(t);
	}
	if((t = quilt_config_geta("s3:secret", NULL)))
	{
		aws_s3_set_secret(patchwork->cache.bucket, t);
		free(t);
	}

	// As its in terms of kbs
	patchwork->cache.s3_fetch_limit = 1024 * quilt_config_get_int("s3:fetch_limit", DEFAULT_PATCHWORK_FETCH_LIMIT);

	patchwork->cache.s3_verbose = quilt_config_get_bool("s3:verbose", 0);
	return 0;
}

static int
patchwork_cache_init_file_(const char *path)
{
	char *t;
	
	if(!path || !path[0])
	{
		return 0;
	}
	patchwork->cache.path = (char *) calloc(1, strlen(path) + 2);
	if(!patchwork->cache.path)
	{
		quilt_logf(LOG_CRIT, QUILT_PLUGIN_NAME ": failed to allocate memory for cache path buffer\n");
		return -1;
	}
	strcpy(patchwork->cache.path, path);
	if(path[0])
	{
		t = strchr(patchwork->cache.path, 0);
		t--;
		if(*t != '/')
		{
			t++;
			*t = '/';
			t++;
			*t = 0;
		}
	}
	return 0;
}

