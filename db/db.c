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

static int patchwork_db_version_(SQL *restrict sql, const char *restrict modulename);
static int patchwork_db_querylog_(SQL *restrict sql, const char *query);
static int patchwork_db_noticelog_(SQL *restrict sql, const char *notice);
static int patchwork_db_errorlog_(SQL *restrict sql, const char *sqlstate, const char *message);

int
patchwork_db_init(void)
{
	char *t;

	if((t = quilt_config_geta(QUILT_PLUGIN_NAME ":db", NULL)))
	{
		patchwork->db = sql_connect(t);
		if(!patchwork->db)
		{
			quilt_logf(LOG_CRIT, QUILT_PLUGIN_NAME ": failed to connect to database <%s>\n", t);
			free(t);
			return -1;
		}
		free(t);
		sql_set_querylog(patchwork->db, patchwork_db_querylog_);
		sql_set_errorlog(patchwork->db, patchwork_db_errorlog_);
		sql_set_noticelog(patchwork->db, patchwork_db_noticelog_);
		patchwork->db_version = patchwork_db_version_(patchwork->db, "com.github.bbcarchdev.spindle.twine");
		quilt_logf(LOG_INFO, QUILT_PLUGIN_NAME ": connected to Spindle database version %d\n", patchwork->db_version);
	}
	return 0;
}

static int
patchwork_db_version_(SQL *restrict sql, const char *restrict modulename)
{
	SQL_STATEMENT *rs;
	long l;

	rs = sql_queryf(sql, "SELECT \"version\" FROM \"_version\" WHERE \"ident\" = %Q", modulename);
	if(!rs)
	{
		quilt_logf(LOG_ERR, QUILT_PLUGIN_NAME ": failed to obtain database schema version from database\n");
		return 0;
	}
	if(sql_stmt_eof(rs))
	{
		quilt_logf(LOG_ERR, QUILT_PLUGIN_NAME ": no Spindle schema found in database\n");
		sql_stmt_destroy(rs);
		return 0;
	}
	l = sql_stmt_long(rs, 0);
	sql_stmt_destroy(rs);
	return (int) l;
}

static int
patchwork_db_querylog_(SQL *restrict sql, const char *query)
{
	(void) sql;

	quilt_logf(LOG_DEBUG, QUILT_PLUGIN_NAME ": SQL: %s\n", query);
	return 0;
}

static int
patchwork_db_noticelog_(SQL *restrict sql, const char *notice)
{
	(void) sql;

	quilt_logf(LOG_NOTICE, QUILT_PLUGIN_NAME ": %s\n", notice);
	return 0;
}

static int
patchwork_db_errorlog_(SQL *restrict sql, const char *sqlstate, const char *message)
{
	(void) sql;

	quilt_logf(LOG_ERR, QUILT_PLUGIN_NAME ": [%s] %s\n", sqlstate, message);
	return 0;
}
