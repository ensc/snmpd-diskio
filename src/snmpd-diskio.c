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

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <stdbool.h>
#include <string.h>
#include <sysexits.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/fcntl.h>
#include <sys/wait.h>

#define BASE_OID	".1.3.6.1.4.1.22683.1"
#define ARRAY_SIZE(_a)	(sizeof(_a) / sizeof (_a)[0])

enum {
	CMD_HELP = 0x1000,
	CMD_VERSION,
	CMD_BASE_OID,
	CMD_CACHE,
	CMD_CACHE_PROGRAM,
	CMD_CONF,
};

static struct option const CMDLINE_OPTIONS[] = {
	{ "help",        no_argument,       NULL, CMD_HELP },
	{ "version",     no_argument,       NULL, CMD_VERSION },
	{ "base-oid",    required_argument, NULL, CMD_BASE_OID },
	{ "cache",       required_argument, NULL, CMD_CACHE },
	{ "cache-program",  required_argument, NULL, CMD_CACHE_PROGRAM },
	{ "conf",        required_argument, NULL, CMD_CONF },
	{ "get",         no_argument,       NULL, 'g' },
	{ "get-next",    no_argument,       NULL, 'n' },
	{ NULL, 0, NULL, 0}
};

struct cmdline_options {
	char const	*cache_file;
	char const	*conf_file;
	char const	*cache_prog;
	char const	*base_oid;

	unsigned int	op_set:1;
	unsigned int	op_get:1;
	unsigned int	op_get_next:1;
};

static void show_help(void)
{
	puts("Usage: snmpd-diskio [--cache <cache-file>] [--get|-g] [--get-next|-n] <OID>]\n");
	exit(0);
}

static void show_version(void)
{
	puts("snmpd-diskio " VERSION " -- diskio statistics\n\n"
	     "Copyright (C) 2011 Enrico Scholz\n"
	     "This program is free software; you may redistribute it under the terms of\n"
	     "the GNU General Public License.  This program has absolutely no warranty.");
	exit(0);
}

static int create_cache_file(int conf_fd,
			     char const *cache_file,
			     char const *cache_prog,
			     bool do_create)
{
	char	buf_cache_fd[sizeof(int)*3 + 2];
	char	buf_conf_fd[sizeof(int)*3 + 2];
	pid_t	pid;
	int	status;
	bool	cache_dirty = false;
	int	cache_fd = open(cache_file,
				O_WRONLY | (do_create ? (O_CREAT | O_EXCL) : 0),
				0666);
	int	rc;

	if (cache_fd < 0) {
		perror("open(<cache>, O_WRONLY)");
		rc = -EX_CANTCREAT;
		goto err;
	}

	if (do_create) {
		if (flock(cache_fd, LOCK_EX) < 0) {
			perror("flock(<cache-gen>, LOCK_EX)");
			rc = -EX_IOERR;
			goto err;
		}
	}

	if (ftruncate(cache_fd, 0) < 0) {
		perror("ftruncate(<cache>, 0)");
		rc = -EX_IOERR;
		goto err;
	}

	sprintf(buf_cache_fd, "%u", cache_fd);
	sprintf(buf_conf_fd, "%u", conf_fd);

	cache_dirty = true;
	pid = vfork();
	if (pid < 0) {
		perror("fork()");
		rc = -EX_OSERR;
		goto err;
	} else if (pid == 0) {
		execl(cache_prog, cache_prog, buf_conf_fd, buf_cache_fd,
		      (char const *)NULL);
		_exit(EX_UNAVAILABLE);
	}

	if (waitpid(pid, &status, 0) < 0) {
		perror("waitpid()");
		rc = -EX_OSERR;
		goto err;
	}

	if (!WIFEXITED(status) ||
	    WEXITSTATUS(status) != 0) {
		fputs("cache gen program failed\n", stderr);
		rc = WIFEXITED(status) ?
			-WEXITSTATUS(status) : -EX_UNAVAILABLE;
		goto err;
	}
	cache_dirty = false;

	close(cache_fd);

	return 0;

err:
	if (cache_fd >= 0) {
		if (cache_dirty) {
			if (ftruncate(cache_fd, 0) < 0) {
				perror("ftruncate(<cache_fd>)");
				/* can not handle this further... */
			}
		}

		close(cache_fd);
	}

	return rc;
}

static int open_cache_file(char const *conf_file,
			   char const *cache_file,
			   char const *cache_prog)
{
	bool			regen_cache;
	int			conf_fd = -1;
	int			cache_fd = -1;
	int			rc;

	conf_fd = open(conf_file, O_RDONLY);
	if (conf_fd < 0) {
		perror("open(<conf>)");
		rc = -EX_UNAVAILABLE;
		goto err;
	}
	if (flock(conf_fd, LOCK_SH) < 0) {
		perror("flock(<conf>, LOCK_SH)");
		rc = -EX_IOERR;
		goto err;
	}

again:
	regen_cache = false;
	errno = 0;
	cache_fd = open(cache_file, O_RDONLY);
	if (cache_fd < 0 && errno == ENOENT)
		regen_cache = true;
	else if (cache_fd < 0) {
		perror("open(<cache_file>, O_RDONLY)");
		rc = -EX_UNAVAILABLE;
		goto err;
	} else {
		struct stat	st_conf;
		struct stat	st_cache;

		if (flock(cache_fd, LOCK_SH) < 0) {
			perror("flock(<cache>, LOCK_SH)");
			rc = -EX_IOERR;
			goto err;
		}

		if (fstat(conf_fd, &st_conf) < 0 ||
		    fstat(cache_fd, &st_cache) < 0) {
			perror("fstat()");
			rc = -EX_IOERR;
			goto err;
		}

		if (st_conf.st_mtime > st_cache.st_mtime ||
		    st_cache.st_size == 0) {
			if (flock(cache_fd, LOCK_EX) < 0) {
				perror("flock(<cache>, LOCK_EX)");
				rc = -EX_IOERR;
				goto err;
			}

			regen_cache = true;
		}
	}

	if (regen_cache) {
		rc = create_cache_file(conf_fd, cache_file,
				       cache_prog, cache_fd < 0);

		if (rc < 0)
			goto err;

		if (cache_fd < 0)
			goto again;

		if (flock(cache_fd, LOCK_SH) < 0) {
			perror("flock(<cache>, LOCK_SH)");
			rc = -EX_IOERR;
			goto err;
		}
	}

	close(conf_fd);
	lseek(cache_fd, 0, SEEK_SET);

	return cache_fd;

err:
	if (cache_fd >= 0)
		close(cache_fd);

	if (conf_fd >= 0)
		close(conf_fd);

	return rc;
}

typedef unsigned long	stat_entries[11];

struct cache_entry {
	char const	*alias;
	char const	*sysfs;
	unsigned int	idx;

	unsigned int	have_stats:1;

	stat_entries	stats;
};

static int consume_str(char const **str,
		       char const **ptr, char const *end_ptr,
		       char const *token_name)
{
	char			*err_ptr;
	unsigned long		val;
	int			rc;

	val = strtoul(*ptr, &err_ptr, 10);
	if (*err_ptr != '|') {
		fprintf(stderr, "invalid string len in cache entry");
		rc = -EX_SOFTWARE;
		goto err;
	}

	*ptr = err_ptr + 1;
	if (*ptr + val > end_ptr) {
		fprintf(stderr, "too large string field '%s' in cache entry\n",
			token_name);
		rc = -EX_SOFTWARE;
		goto err;
	}

	*str = strndup(*ptr, val);
	if (!*str) {
		perror("strndup()");
		rc = -EX_OSERR;
		goto err;
	}

	*ptr += val;

	return 0;

err:
	return rc;
}

static int consume_uint(unsigned int *v,
			char const **ptr, char const *token_name)
{
	char			*err_ptr;

	*v = strtoul(*ptr, &err_ptr, 10);
	*ptr = err_ptr;

	return 0;
}

static int consume_delim(char const **ptr, char delim)
{
	int	rc = 0;

	if (**ptr != delim) {
		fprintf(stderr, "invalid delimiter 0x%02x (expected '%c')\n",
			**ptr, delim);
		rc = -EX_SOFTWARE;
	} else
		*ptr += 1;

	return rc;
}

static int cache_entry_cmp(void const *a_v, void const *b_v)
{
	struct cache_entry const	*a = a_v;
	struct cache_entry const	*b = b_v;

	return (signed int)a->idx - (signed int)b->idx;
}

static int read_cache_file(int fd, struct cache_entry **cache_entries,
			   size_t *num_cache_entries)
{
	struct stat	st;
	int		rc;
	char const	*cfg = NULL;
	char const	*ptr;
	char const	*end_ptr;
	bool		in_comment = false;
	enum {
		ST_MAJOR,
		ST_MINOR,
		ST_ALIAS,
		ST_SYSFS,
		ST_IDX
	}			state = ST_MAJOR;
	struct cache_entry	*entry = NULL;
	size_t			alloc_entries = 0;

	*cache_entries = NULL;
	*num_cache_entries = 0;

	if (fstat(fd, &st) < 0) {
		perror("fstat()");
		rc = -EX_OSERR;
		goto err;
	}

	cfg = mmap(NULL, st.st_size, PROT_READ, MAP_SHARED, fd, 0);
	if (!cfg) {
		perror("mmap()");
		rc = -EX_OSERR;
		goto err;
	};

	ptr = cfg;
	end_ptr = cfg + st.st_size;

	while (ptr < cfg + st.st_size) {
		if (alloc_entries <= *num_cache_entries) {
			struct cache_entry	*tmp;

			alloc_entries = alloc_entries * 3/2 + 10;
			tmp = realloc(*cache_entries, sizeof *tmp * alloc_entries);

			if (!tmp) {
				perror("realloc(<cache_entries>)");
				rc = -EX_OSERR;
				goto err;
			}

			*cache_entries = tmp;
			entry = (void *)0xdeadbeaf;
		}

		if (in_comment) {
			if (*ptr == '\n')
				in_comment = false;
			++ptr;
		} else if (*ptr == '#') {
			in_comment = true;
			++ptr;
		} else {
			unsigned int		dummy;

			switch (state) {
			case ST_MAJOR:
				entry = &(*cache_entries)[*num_cache_entries];
				entry->have_stats = 0;

				if (memchr(ptr, '\n', end_ptr - ptr) == NULL) {
					fprintf(stderr, "invalid cache entry");
					rc = -EX_SOFTWARE;
					goto err;
				}

				rc = consume_uint(&dummy, &ptr, "major");
				break;

			case ST_MINOR:
				rc = consume_uint(&dummy, &ptr, "minor");
				break;

			case ST_IDX:
				rc = consume_uint(&entry->idx, &ptr, "idx");
				break;

			case ST_ALIAS:
				rc = consume_str(&entry->alias, &ptr, end_ptr, "alias");
				break;

			case ST_SYSFS:
				rc = consume_str(&entry->sysfs, &ptr, end_ptr, "sysfs");
				break;

			default:
				abort();
			}

			if (rc >= 0)
				rc = consume_delim(&ptr,
						   (state == ST_IDX) ? '\n' : ' ');

			if (rc < 0)
				goto err;
			else if (state == ST_IDX) {
				state = ST_MAJOR;
				++*num_cache_entries;
			} else
				++state;
		}
	}

	if (state != ST_MAJOR) {
		fprintf(stderr, "incomplete cache entry\n");
		rc = -EX_SOFTWARE;
		goto err;
	}

	munmap((void *)cfg, st.st_size);

	qsort(*cache_entries, *num_cache_entries, sizeof (*cache_entries)[0],
	      cache_entry_cmp);


	return 0;

err:
	free(*cache_entries);

	if (cfg)
		munmap((void *)cfg, st.st_size);

	return rc;
}

struct oid_info {
	unsigned int	oid;
	unsigned int	idx;
} const		SUB_OIDS[] = {
	{  0, ~2u },
	{  1, ~0u },
	{  2, ~1u },
	{ 10,  0 },
	{ 11,  1 },
	{ 12,  2 },
	{ 13,  3 },
	{ 20,  4 },
	{ 21,  5 },
	{ 22,  6 },
	{ 23,  7 },
	{ 30,  8 },
	{ 31,  9 },
	{ 32, 10 },
	{  0,  0 },			/* sentinel for get-next operation */
};

static int oid_entry_search_cmp(void const *a_v, void const *b_v)
{
	unsigned int const	*a = a_v;
	struct oid_info const	*b = b_v;

	return *a - b->oid;
}

static bool parse_oid(char const *str,
		      struct oid_info const **oid_info,
		      unsigned int *idx)
{
	unsigned int		suboid;
	struct oid_info const	*oid;

	if (*str == '.') {
		char			*err_ptr;

		suboid = strtoul(str+1, &err_ptr, 10);
		if (*err_ptr == '\0')
			*idx = ~0u;
		else if (*err_ptr == '.')
			*idx = strtoul(err_ptr + 1, &err_ptr, 10);

		if (*err_ptr != '\0') {
			fprintf(stderr, "unsupported suboid '%s'\n", str);
			return false;
		}

		oid = bsearch(&suboid, &SUB_OIDS[0],
			      ARRAY_SIZE(SUB_OIDS) - 1, sizeof SUB_OIDS[0],
			      &oid_entry_search_cmp);

		if (!oid) {
			fprintf(stderr, "unrecognized suboid '%s'\n", str);
			return false;
		}
	} else if (*str == '\0') {
		oid = NULL;
		*idx = ~0u;
	} else {
		fprintf(stderr, "invalid suboid '%s'\n", str);
		return false;
	}

	*oid_info = oid;
	return true;
}

static int read_sysfs_cache(struct cache_entry *entry)
{
	FILE	*file = fopen(entry->sysfs, "r");
	int	rc;

	if (!file) {
		perror("fopen(<sysfs>)");
		rc = -EX_OSERR;
		goto err;
	}

	if (fscanf(file, "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu\n",
		   &entry->stats[0], &entry->stats[1], &entry->stats[2], &entry->stats[3],
		   &entry->stats[4], &entry->stats[5], &entry->stats[6], &entry->stats[7],
		   &entry->stats[8], &entry->stats[9], &entry->stats[10]) != 11) {
		fprintf(stderr, "failed to read sysfs stats\n");
		rc = -EX_SOFTWARE;
		goto err;
	}

	fclose(file);

	entry->have_stats = 1;
	return 0;

err:
	if (file)
		fclose(file);

	return rc;
}

static void print_cache(struct cache_entry const *entry,
			char const *base_oid,
			struct oid_info const *oid_info)
{
	if (entry->have_stats) {
		printf("%s.%u.%u\n", base_oid, oid_info->oid, entry->idx);

		if (oid_info->idx == ~0u)
			printf("string\n%s\n", entry->alias);
		else if (oid_info->idx == ~1u)
			printf("integer\n%u\n", entry->idx);
		else if (oid_info->idx >= ARRAY_SIZE(entry->stats))
			abort();
		else
			printf("counter\n%lu\n",
			       entry->stats[oid_info->idx] & 0xffffffffu);
	}
}

int main(int argc, char *argv[])
{
	struct cmdline_options	opts = {
		.cache_file	=  CACHE_FILE,
		.base_oid	=  BASE_OID,
		.conf_file	=  CONF_FILE,
		.cache_prog	=  CACHE_REGEN_PROG,
	};

	bool				is_ok;
	int				cache_fd;
	struct cache_entry		*cache_entries;
	size_t				num_cache_entries;
	int				rc;
	unsigned int			attr_idx = ~0u;
	size_t				req_cache_idx;
	struct oid_info const		*sub_oid;
	size_t				base_oid_len;

	while (1) {
		int		c = getopt_long(argc, argv, "gns", CMDLINE_OPTIONS, 0);
		if (c==-1) break;

		switch (c) {
		case CMD_HELP:		show_help();
		case CMD_VERSION:	show_version();
		case CMD_BASE_OID:	opts.base_oid = optarg; break;
		case CMD_CACHE:		opts.cache_file = optarg; break;
		case CMD_CACHE_PROGRAM:	opts.cache_prog = optarg; break;
		case CMD_CONF:		opts.conf_file = optarg; break;
		case 'g':		opts.op_get = 1; break;
		case 's':		opts.op_set = 1; break;
		case 'n':		opts.op_get_next = 1; break;
		default:
			fputs("Try '--help' for more information.\n", stderr);
			exit(EX_USAGE);
		}
	}

	is_ok = false;
	base_oid_len = strlen(opts.base_oid);

	if (opts.op_get + opts.op_set + opts.op_get_next > 1)
		fputs("more than one operation specified\n", stderr);
	else if (opts.op_get + opts.op_set + opts.op_get_next == 0)
		fputs("no operation specified\n", stderr);
	else if (optind + 1 != argc)
		fputs("no/too much OID specified\n", stderr);
	else if (strncmp(opts.base_oid, argv[optind], base_oid_len) &&
		 argv[optind][base_oid_len] != '\0' &&
		 argv[optind][base_oid_len] != '.')
		fputs("unsupported OID\n", stderr);
	else if (!parse_oid(&argv[optind][base_oid_len], &sub_oid, &attr_idx))
		;			/* noop */
	else if ((opts.op_get || opts.op_set) &&
		 (sub_oid == NULL || (attr_idx == ~0u && sub_oid != &SUB_OIDS[0])))
		fputs("unknown OID\n", stderr);
	else
		is_ok = true;

	if (!is_ok)
		exit(EX_USAGE);

	if (opts.op_set) {
		puts("not-writable");
		return EXIT_SUCCESS;
	}

	if (sub_oid == NULL) {
		sub_oid = &SUB_OIDS[0];
		attr_idx = 0;
	} else if (sub_oid == &SUB_OIDS[0])
		attr_idx = ~0u;

	cache_fd = open_cache_file(opts.conf_file, opts.cache_file, opts.cache_prog);
	if (cache_fd < 0)
		exit(-cache_fd);

	rc = read_cache_file(cache_fd, &cache_entries, &num_cache_entries);
	if (rc < 0)
		exit(-rc);

	close(cache_fd);

	req_cache_idx = num_cache_entries;
	if (attr_idx == ~0u && opts.op_get_next)
		req_cache_idx = 0;
	if (sub_oid != &SUB_OIDS[0] && attr_idx != ~0u) {
		size_t				i;

		for (i = 0; i < num_cache_entries; ++i) {
			if (cache_entries[i].idx != attr_idx)
				continue;

			req_cache_idx = i;
			if (opts.op_get_next) {
				++req_cache_idx;

				if (req_cache_idx >= num_cache_entries) {
					req_cache_idx = 0;
					++sub_oid;
				}
			}

			break;
		}
	} else if (opts.op_get_next && attr_idx == ~0u) {
		if (sub_oid == &SUB_OIDS[0])
			++sub_oid;

		req_cache_idx = 0;
	}

	if (sub_oid->idx == ~2u) {
		printf("%s.%u\ninteger\n%zu\n", opts.base_oid, sub_oid->oid,
		       num_cache_entries);
	} else if (req_cache_idx < num_cache_entries && sub_oid->oid != 0) {
		rc = read_sysfs_cache(&cache_entries[req_cache_idx]);
		if (rc < 0)
			exit(-rc);

		print_cache(&cache_entries[req_cache_idx],
			    opts.base_oid, sub_oid);
	}


	free(cache_entries);
}
