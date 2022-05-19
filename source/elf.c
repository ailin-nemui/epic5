/*
 * elf.c: code for generalizing loading from just about anywhere
 *
 * Copyright 2007, 2012 EPIC Software Labs.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notices, the above paragraph (the one permitting redistribution),
 *    this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the author(s) may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
/*
 * This code was contributed to EPIC Software Labs by Alexander Grotewohl,
 * used with permission.
 */
#include "irc.h"
#include "ircaux.h"
#include "elf.h"
#include "output.h"

#ifdef HAVE_LIBARCHIVE
static int archive_fopen(struct epic_loadfile *elf, char *filename, const char *ext, int do_error);
static int find_in_archive(struct archive *a, struct archive_entry **entry, const char *str, int do_error);
#endif

struct epic_loadfile * epic_fopen(char *filename, const char *mode, int do_error)
{
    FILE * doh;
    int    ret;

    struct epic_loadfile *elf;

    elf = (struct epic_loadfile *) new_malloc(sizeof(struct epic_loadfile));

    elf->fp = NULL;
#ifdef HAVE_LIBARCHIVE
    elf->a = NULL;
    elf->entry = NULL;
#endif
    elf->eof = 0;

#ifdef HAVE_LIBARCHIVE
    if (stristr(filename, ".zip")!=-1) {
        ret=archive_fopen(elf, filename, ".zip", do_error);
    } else if (stristr(filename, ".tar")!=-1) {
        ret=archive_fopen(elf, filename, ".tar", do_error);
    }
    else
#endif
	ret = 0;

    if (ret==1)
        return elf;

    if (ret!=-1) { /* archive didnt have loadable data */

        /* Its not a compressed file... Try to open it regular-like. */
        if ((doh = fopen(filename, mode))) {
            elf->fp = doh;
            return elf;
        }
        if (do_error)
            yell("Cannot open file %s: %s", filename, strerror(errno));
    }
    new_free(&elf);

    return NULL;
}


#ifdef HAVE_LIBARCHIVE
/*
 * this one takes a filename and an extension we expect is an archive,
 * and attempts to open it up. it returns -1 on error, 0 if it's not
 * an archive, or 1 if the archive was successfully loaded
 */
static int	archive_fopen(struct epic_loadfile *elf, char *filename, const char *ext, int do_error)
{
    int    ret;

    int    pos;
    char * fname;
    char * extra;
    char * safet;

    fname = LOCAL_COPY(filename);
    safet = LOCAL_COPY(filename);

    extra = fname;

    if ((pos=stristr(fname, ext))!=-1) {

        /*
         * Here we make fname actually point to a real
         * file, instead of virtual files/directories
         * inside the archive.. and check if we're
         * supposed to load a file by default..
         */
        extra+=pos;
        while (*extra!='/') {
            if (*extra==0)
                break;
            extra++;
            pos++;
        }
        safet+=pos;
        *extra++ = 0;

        elf->a = archive_read_new();
        if (!archive_read_support_format_all(elf->a)) {

            if ( !archive_read_open_filename(elf->a, fname, 10240))  {
                if ( (strstr(safet, "/"))!=NULL ) { /* specific file provided */
                    if (!find_in_archive(elf->a, &elf->entry, extra, do_error))
                        return 0;
                } else {
                    if (!find_in_archive(elf->a, &elf->entry, ".ircrc", do_error)) { /* bootstrap */
                        if (do_error)
                            yell("No .ircrc in the zip file specified");
                        return -1;
                    }
                }
                /*
                 * Our archive should now be open and pointing to the
                 * beginning of the specified file
                 */
                return 1;
            } /* <-- we can fall off the end */
        }
    }
    return 0;
}

static char *	archive_fgets(char *s, int n, struct archive *a)
{
    int ret, bytes_read;
    char *p = s;
    char c;

    bytes_read=0;
    while (bytes_read < n) {
        ret=archive_read_data(a, &c, 1);
        if (ret>0) {
            bytes_read+=ret;
            *p++ = c;
            if (c=='\n')
                break;
            continue;
        }
        return NULL;
    }
    *p='\0';
    return s;
}
#endif

int	epic_fgetc(struct epic_loadfile *elf)
{
    int ret;
    char c;

    if ((elf->fp)!=NULL) {
        return fgetc(elf->fp);
    }
#ifdef HAVE_LIBARCHIVE
    else if ((elf->a)!=NULL) {
        ret=archive_read_data(elf->a, &c, 1);
        if (ret>0) {
            return c;
        } else {
            elf->eof=1;
            return EOF;
        }
    } 
#endif
    else {
        /* other */
        return EOF;
    }
}

char *	epic_fgets(char *s, int n, struct epic_loadfile *elf)
{
    if ((elf->fp)!=NULL) {
        return fgets(s, n, elf->fp);
    }
#ifdef HAVE_LIBARCHIVE
    else if ((elf->a)!=NULL) {
        return archive_fgets(s, n, elf->a);
    } 
#endif
    else {
        return NULL;
    }
}

int	epic_feof(struct epic_loadfile *elf)
{
    if ((elf->fp)!=NULL) {
        return feof(elf->fp);
    }
#ifdef HAVE_LIBARCHIVE
    else if ((elf->a)!=NULL) {
        return elf->eof;
        /* unspecified */
    } 
#endif
    else {
        return 1;
    }
}

int	epic_fclose(struct epic_loadfile *elf)
{
    if ((elf->fp)!=NULL) {
        return fclose(elf->fp);
    }
#ifdef HAVE_LIBARCHIVE
    else if ((elf->a)!=NULL) {
        archive_read_close(elf->a);
        archive_read_free(elf->a);
        return 0;
    } 
#endif
    else {
        return EOF;
    }
}

off_t	epic_stat(const char *filename, struct stat *buf)
{
#ifdef HAVE_LIBARCHIVE
    struct  archive *a;
    struct  archive_entry *entry = NULL;
#endif
    int     ret;

    char *  zip;
    char *  zipstr;
    char *  sl;
    int     ziploc;
    int     scan = 0;

#ifdef HAVE_LIBARCHIVE
    /*
     * should probably fill the stat structure with
     * as much as possible.. i don't know what might
     * rely on this information..
     */

    zipstr = LOCAL_COPY(filename);
    sl = LOCAL_COPY(filename);
    zip = zipstr;

    if (((ziploc=stristr(zipstr, ".zip"))!=-1) || ((ziploc=stristr(zipstr, ".tar"))!=-1)) {
        zip+=ziploc;
        while (*zip!='/') {
            if (*zip==0)
                break;
            zip++;
            ziploc++;
        }
        sl+=ziploc;
        *zip++ = 0;

        if ( strstr(sl, "/")!=NULL ) scan=1;

        a = archive_read_new();
        archive_read_support_format_all(a);

        if ( !archive_read_open_filename(a, zipstr, 10240) ) {
            if (scan) {
                if (!(find_in_archive(a, &entry, zip, 0)) ) {
                    return -1;
                } else {
                    /* this is such a hack, but libarchive consistantly */
                    /* crashes on me when i use it's stat routines */
                    buf->st_size=1;
                    buf->st_mode=33188;

                    archive_read_close(a);
                    archive_read_free(a);
                    return 0;
                }
            }
        }
    }
#endif

    if (!stat(filename, buf)) {
        return 0;
    } else {
        return -1;
    }
}

#ifdef HAVE_LIBARCHIVE
static int	find_in_archive(struct archive *a, struct archive_entry **entry, const char *str, int do_error)
{
    int ret;

    for (;;) {
        ret = archive_read_next_header(a, entry);
        if (ret == ARCHIVE_EOF) {
            archive_read_close(a);
            archive_read_free(a);
            return 0;
        }
        if (ret != ARCHIVE_OK) {
            if (do_error) {
                yell("%s", archive_error_string(a));
            }
            archive_read_close(a);
            archive_read_free(a);
            return 0;
        }

        if (strcmp(archive_entry_pathname(*entry), str)==0)
            break;
    }
    return 1;
}
#endif

/*
 * This converts any "elf" into a bytestream.
 * It isn't necessary that the file is a string, or in any particular
 * encoding -- we just dump it into "file_contents" and tell you how many
 * bytes we read.  It's up to you to decide if it needs recoding or 
 * decrypting or whatever.
 *
 * Arguments:
 *	elf		- A pointer previously returned by epic_fopen()
 *	file_contents 	- A pointer to a (char *)variable assigned to NULL
 *			  THE INPUT VALUE (*file_contents) WILL BE DISCARDED
 *			  THE RETURN VALUE (*file_contents) MUST BE FREE'D.
 *	file_contents_size - A pointer to an off_t variable.
 *			  The input value will be discarded.
 *			  The return value is the number of bytes in the file.
 *			  Do not reference (*file_contents) beyond the number
 *			  of bytes returned by this function.
 * Return value:
 *	-1	- An error occurred.  Do not free (*file_contents).
 *	>= 0	- This many bytes were read from the file and put into 
 *		  malloced space which (*file_contents) now points to.  
 *		  If this function returns >= 0, YOU MUST FREE (*file_contents)!
 */
size_t	slurp_elf_file (struct epic_loadfile *elf, char **file_contents, off_t *file_contents_size)
{
	size_t	size;
	size_t	next_byte = 0;
	int	sigh;

	if (!elf)
		return -1;
	if (!file_contents)
		return -1;

	size = 8192;
	RESIZE(*file_contents, char, size);

	while (!epic_feof(elf))
	{
		if (next_byte >= size)
		{
			size += 8192;
			RESIZE(*file_contents, char, size);
		}

		sigh = epic_fgetc(elf);
		if (sigh == EOF)
			(*file_contents)[next_byte] = 0;
		else if (sigh >= 0 && sigh <= 255)
			(*file_contents)[next_byte] = (char)sigh;
		next_byte++;
	}

	/* The previous epic_fgetc() resulted in EOF so we got to back up */
	next_byte--;

	/* Just for laughs, zero terminate it so it's a C string */
	(*file_contents)[next_byte] = 0;
	*file_contents_size = next_byte;
	return next_byte;
}

int	string_feof(const char *file_contents, off_t file_contents_size)
{
	if (file_contents_size > 0)
		return 0;
	return 1;
}

int	string_fgetc(const char **file_contents, off_t *file_contents_size)
{
	int	retval;

	if (*file_contents_size <= 0)
		return EOF;

	retval = (*file_contents)[0];
	(*file_contents)++;
	(*file_contents_size)--;
	return retval;
}

off_t	string_fgets(char *buffer, size_t buffer_size, const char **file_contents, off_t *file_contents_size)
{
	size_t	offset = 0;
	int	next_byte = 0;
	int	read_any_byte = 0;

	for (;;)
	{
		next_byte = string_fgetc(file_contents, file_contents_size);
		if (next_byte == EOF)
		{
			if (!read_any_byte)
				return 0;
			else
				return offset;
		}

		read_any_byte = 1;
		if (offset <= buffer_size)
			buffer[offset++] = next_byte;
		if (next_byte == '\n')
		{
			if (offset <= buffer_size)
				buffer[offset++] = 0;
			return offset;
		}
	}
}

