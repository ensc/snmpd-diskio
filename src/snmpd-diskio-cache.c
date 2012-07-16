/*	--*- c -*--
 * Copyright (C) 2011 Enrico Scholz <enrico.scholz@informatik.tu-chemnitz.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 3 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define HAVE_CONFIG_H

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <ctype.h>
#include <sysexits.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/stat.h>

#include <blkid/blkid.h>

#ifdef HAVE_LIBBLKID1
static char *blkid_evaluate_tag(char const *tag, char const *value,
				blkid_cache *blcache)
{
	return blkid_get_devname(*blcache, tag, value);
}
#endif

static char *read_quoted_string(char **ptr, unsigned int line_num)
{
	bool	is_quoted = false;
	char	*out_ptr = *ptr;
	char	*res = *ptr;

	for (; **ptr && (is_quoted || !isspace(**ptr)); ++(*ptr)) {
		switch (**ptr) {
		case '"':
			is_quoted = !is_quoted;
			break;

		case '\\':
			if ((*ptr)[1] == '\0') {
				fprintf(stderr,
					":%u backslash at eol not supported\n",
					line_num);
				exit(EX_DATAERR);
			}

			++(*ptr);
			*out_ptr++ = **ptr;
			break;

		default:
			*out_ptr++ = **ptr;
			break;
		}
	}

	if (out_ptr != *ptr)
		*out_ptr = '\0';

	return res;
}

int main(int argc, char *argv[])
{
	int		fd_conf;
	int		fd_cache;
	time_t		now = time(NULL);
	char		now_str[128];
	FILE		*cfg_file;
	char		*line = NULL;
	size_t		line_len = 0;
	unsigned int	line_num = 0;
	blkid_cache	blkid_cache;

	if (argc != 3)
		exit(EX_USAGE);

	if (blkid_get_cache(&blkid_cache, NULL) < 0)
		blkid_cache = NULL;

	fd_conf = atoi(argv[1]);
	fd_cache = atoi(argv[2]);

	cfg_file = fdopen(fd_conf, "r");

	strftime(now_str, sizeof now_str, "%Y%m%dT%H%M%S UTC",
		 gmtime(&now));
	dprintf(fd_cache, "# generated at %s\n", now_str);

	for (;;) {
		ssize_t		l = getline(&line, &line_len, cfg_file);
		char		*ptr;
		char const	*tag = NULL;
		char		*value = NULL;

		char		*dev_name;
		char const	*dev_alias;
		char		*dev_idx_err;
		unsigned int	dev_idx;
		struct stat	st;
		char		sysfs_path[4096];

		if (l < 0)
			break;

		++line_num;
		ptr = line;

		while (isspace(*ptr)) {
			++ptr;
			--l;
		}

		if (*ptr == '#')
			continue;

		while (l > 0 && isspace(line[l-1]))
			--l;

		line[l] = '\0';

		if (strncmp(ptr, "UUID=", 5) == 0) {
			tag = "UUID";
			ptr += 5;
		} else if (strncmp(ptr, "LABEL=", 5) == 0) {
			tag = "LABEL";
			ptr += 6;
		} else {
			tag = NULL;
		}

		/* tag/uuid/label */
		value = read_quoted_string(&ptr, line_num);
		if (ptr[0] == '\0') {
			fprintf(stderr, ":%u missing device alias field\n",
				line_num);
			exit(EX_DATAERR);
		}
		*ptr++ = '\0';
		while (isspace(*ptr))
			++ptr;

		/* alias */
		dev_alias = read_quoted_string(&ptr, line_num);
		if (ptr[0] == '\0') {
			fprintf(stderr, ":%u missing device index field\n",
				line_num);
			exit(EX_DATAERR);
		}
		*ptr++ = '\0';
		while (isspace(*ptr))
			++ptr;

		/* device index */
		dev_idx = strtoul(ptr, &dev_idx_err, 10);
		while (isspace(*dev_idx_err))
			++dev_idx_err;

		if (*dev_idx_err != '\0' && *dev_idx_err != '#') {
			fprintf(stderr, ":%u unexpected data after dev index field\n",
				line_num);
			exit(EX_DATAERR);
		}

		/* evaluate tag */
		if (tag) {
			dev_name = blkid_evaluate_tag(tag, value, &blkid_cache);
		} else {
			dev_name = realpath(value, NULL);
		}

		if (!dev_name)
			continue;

		if (stat(dev_name, &st) < 0) {
			fprintf(stderr, ":%u failed to stat '%s': %m\n",
				line_num, dev_name);
			exit(EX_IOERR);
		}

		{
			char	*tmp = dev_name;

			if (strncmp("/dev/", dev_name, 5) == 0)
				tmp += 5;

			for (ptr = tmp; *ptr; ++ptr) {
				if (*ptr == '/')
					*ptr = '!';
			}

			snprintf(sysfs_path, sizeof sysfs_path,
				 "/sys/class/block/%s/stat", tmp);
		}

		free(dev_name);

		if (access(sysfs_path, R_OK) < 0)
			continue;

		dprintf(fd_cache, "%u %u %zu|%s %zu|%s %u\n",
			major(st.st_rdev), minor(st.st_rdev),
			strlen(dev_alias), dev_alias,
			strlen(sysfs_path), sysfs_path,
			dev_idx);

	}

	free(line);
	fclose(cfg_file);
	blkid_put_cache(blkid_cache);

	return 0;
}
