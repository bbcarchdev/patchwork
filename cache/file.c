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


/* Fetch an item by retrieving triples or quads from the on-disk cache */
int
patchwork_item_file(QUILTREQ *request, const char *id)
{
	char pathbuf[36];
	char *p;
	char *buf, *buffer;
	FILE *f;
	ssize_t r;
	size_t bufsize, buflen;
	
	if(strlen(id) != 32)
	{
		return 404;
	}
	pathbuf[0] = '/';
	strcpy(pathbuf + 1, id);
	
	buf = (char *) calloc(1, strlen(patchwork->cache.path) + strlen(pathbuf) + 16);
	if(!buf)
	{
		return 500;
	}
	strcpy(buf, patchwork->cache.path);
	strcat(buf, pathbuf);
	f = fopen(buf, "rb");
	if(!f)
	{
		quilt_logf(LOG_CRIT, QUILT_PLUGIN_NAME ": failed to open cache file for reading: %s: %s\n", buf, strerror(errno));
		free(buf);
		return 404;
	}
	r = 0;
	buffer = NULL;
	bufsize = 0;
	buflen = 0;
	while(!feof(f))
	{
		if(bufsize - buflen < 1024)
		{
			p = (char *) realloc(buffer, bufsize + 1024);
			if(!p)
			{
				quilt_logf(LOG_CRIT, QUILT_PLUGIN_NAME ": failed to reallocate buffer from %u bytes to %u bytes\n", (unsigned) bufsize, (unsigned) bufsize + 1024);
				free(buffer);
				free(buf);
				fclose(f);
				return 500;
			}
			buffer = p;
			bufsize += 1024;
		}
		r = fread(&(buffer[buflen]), 1, 1023, f);
		if(r < 0)
		{
			quilt_logf(LOG_CRIT, QUILT_PLUGIN_NAME ": error reading from '%s': %s\n", buf, strerror(errno));
			free(buffer);
			free(buf);
			fclose(f);
			return 500;
		}
		buflen += r;
		buffer[buflen] = 0;
	}
	fclose(f);
	if(quilt_model_parse(request->model, MIME_NQUADS, buffer, buflen))
	{
		quilt_logf(LOG_ERR, QUILT_PLUGIN_NAME ": file: failed to parse buffer from %s as '%s'\n", buf, MIME_NQUADS);
		free(buf);
		free(buffer);
		return 500;
	}
	free(buffer);
	free(buf);
	return 200;
}
