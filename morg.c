/* morg.c */
/* 
 * This file is public domain as declared by Sturm Mabie.
 */

#include <sys/stat.h>
#include <sys/types.h>

#include <dirent.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <libgen.h>
#include <unistd.h>

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <taglib/tag_c.h>

#define FMTSTR								\
	"/home/\'\"$USER\"\'/music/%artist/%artist - %album/%track %title.%type"
#define CPSTR	"mkdir -p \"`dirname '%dst'`\" && cp '%src' '%dst'"

#ifdef __GLIBC__
#undef basename
#undef dirname
#define basename bsd_basename
#define dirname bsd_dirname

char *bsd_basename(const char *);
char *bsd_dirname(const char *);
size_t strlcat(char *, const char *, size_t);
size_t strlcpy(char *, const char *, size_t);
#endif  /* __GLIBC__ */

int copy_file(const char *);
int find_files(const char *);

char *make_path(TagLib_Tag *, const char *);
char *make_copy(const char *, const char *, const char *);

void usage();

char *fmtstr, *cpstr;
int vflg;

int
main(int argc, char **argv)
{
	int c;
	
	vflg = 0;
	if ((fmtstr = getenv("MORGFMT")) == NULL)
		fmtstr = FMTSTR;
	if ((cpstr = getenv("MORGCP")) == NULL)
		cpstr = CPSTR;
	while ((c = getopt(argc, argv, "s:t:v")) != -1) {
		switch (c) {
		case 's':	/* format string specified */
			fmtstr = optarg;
			break;
		case 't':	/* transfer command specified */
			cpstr = optarg;
			break;
		case 'v':	/* verbose mode */
			vflg = 1;
			break;
		default:
			usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc < 1)
		usage();
	do {
		(void)find_files(*argv);
	} while (*++argv != NULL);
	return 0;
}

int
find_files(const char *path)
{
	struct stat sb;
	DIR *dirp;
	struct dirent *dp;
	static char buf[PATH_MAX];
	size_t s;

	if ((dirp = opendir(path)) == NULL) {
		if (errno == ENOTDIR) {
			(void)copy_file(path);
			return 0;
		} else {
			warn("opendir: %s", path);
			return 1;
		}
	}
	if ((s = strlcpy(buf, path, PATH_MAX)) >= PATH_MAX)
		goto longer;
	if (path[(s = strlen(path)) - 1] != '/') {
		buf[s] = '/';
		buf[s + 1] = '\0';
	}

	errno = 0;
	while ((dp = readdir(dirp)) != NULL) {
		if (strcmp(dp->d_name, ".") == 0 || 
		    strcmp(dp->d_name, "..") == 0)
			continue;
		if (strlcat(buf, dp->d_name, PATH_MAX) >= PATH_MAX)
			goto longer;
		if (stat(buf, &sb) == -1) {
			warn("stat: %s", buf);
			goto er;
		}
		if (S_ISDIR(sb.st_mode) || S_ISLNK(sb.st_mode))
			(void)find_files(buf);
		else 
			(void)copy_file(buf);
		buf[buf[s] == '/' ? s + 1 : s] = '\0';
	}
	if (errno) {
		warn("readdir");
		goto er;
	}

	(void)closedir(dirp);
	return 0;
longer:
	warnx("PATH_MAX of %d violated", PATH_MAX);
er:
	(void)closedir(dirp);
	return 1;
}

int copy_file(const char *file)
{
	static char real[PATH_MAX];
	TagLib_File *tag_file;
	char *p, *s, *name, ext[5];
	
	(void)strlcpy(real, file, PATH_MAX);
	if ((p = strrchr(real, '.')) == NULL)
		return 1;
	*p = '/';
	if ((p = basename(real)) == NULL) {
		warn("basename: %s", real);
		return 1;
 	}
	if (strcmp(p, "mp3") != 0 && 
	    strcmp(p, "flac") != 0 && 
	    strcmp(p, "ogg") != 0)
		return 0;
	(void)strlcpy(ext, p, sizeof(ext));
	if ((name = basename(file)) == NULL) {
		warn("basename: %s", file);
		return 1;
	}
	if ((tag_file = taglib_file_new(file)) == NULL) {
		warnx("the type of %s cannot be determined or the file cannot "
		      "be opened", name);
		return 1;
	}
	if (!taglib_file_is_valid(tag_file)) {
		warnx("the file %s is not valid", name);
		taglib_file_free(tag_file);
		return 1;
	}
	s = make_path(taglib_file_tag(tag_file), ext);
	if (vflg) {
		if (realpath(file, real) == NULL) {
			warn("realpath: %s", file);
			return 1;
		}
		(void)fprintf(stderr, "%s -> %s\n", real, s);
	}
	(void)system(make_copy(cpstr, file, s));

	taglib_tag_free_strings();
	taglib_file_free(tag_file);
	return 0;
}

char *
make_copy(const char *fmt, const char *src, const char *dst)
{
	static char ret[2048];
	const char *p;
	int d;
	
	for (p = fmt, d = 0; *p != '\0' && d < sizeof(ret); p++) {
		if (*p == '%') {
			if (*(p + 1) == '%')
				ret[d++] = *p;
			else if (strncmp(p + 1, "src", 3) == 0) {
				(void)strlcpy(&ret[d], src, sizeof(ret));
				d += strlen(src);
				p += 3;
			} else if (strncmp(p + 1, "dst", 3) == 0) {
				(void)strlcpy(&ret[d], dst, sizeof(ret));
				d += strlen(dst);
				p += 3;
			}
		} else
			ret[d++] = *p;
	}
	ret[d] = '\0';
	return ret;
}

char *
make_path(TagLib_Tag *tags, const char *type)
{
#define SAFE(s) (strlen(s) != 0 ? (s) : " ")
	static char ret[2048];
	static char yearbuf[5];
	static char trackbuf[3];
	char *p;
	int d, c;
	struct {
		const char *s;
		const char *p;
	} table[] = {
		{ "track", NULL	},
		{ "title", NULL },
		{ "artist",NULL },
		{ "album", NULL },
		{ "genre", NULL },
		{ "year",  NULL },
		{ "type", NULL }
	};

	(void)snprintf(trackbuf, 3, taglib_tag_track(tags) < 10 ? "0%d" : "%d",
		       taglib_tag_track(tags));
 	(void)snprintf(yearbuf, 5, "%d", taglib_tag_year(tags));
	table[0].p = trackbuf;
	table[1].p = SAFE(taglib_tag_title(tags));
	table[2].p = SAFE(taglib_tag_artist(tags));
	table[3].p = SAFE(taglib_tag_album(tags));
	table[4].p = SAFE(taglib_tag_genre(tags));
	table[5].p = yearbuf;
	table[6].p = type;

	for (p = fmtstr, d = 0; *p != '\0' && d < sizeof(ret); p++) {
		if (*p == '%') {
			if (*(p + 1) == '%') {
				ret[d++] = *p;
				continue;
			}
			for (c = 0; c < sizeof(ret) / sizeof(*ret); c++) {
				if (strncmp(p + 1, table[c].s, 
					    strlen(table[c].s)) == 0) {
					(void)strlcpy(&ret[d], 
						      table[c].p, sizeof(ret));
					d += strlen(table[c].p);
					p += strlen(table[c].s);
					break;
				}
			}
		} else
			ret[d++] = *p;
	}
	ret[d] = '\0';
	return ret;
}

void
usage()
{
	(void)fprintf(stderr, "morg [-v] [-s fmtstr] [-t cpstr] file ...\n");
	exit(1);
}
