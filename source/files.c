/* $EPIC: files.c,v 1.16 2002/12/11 19:20:23 crazyed Exp $ */
/*
 * files.c -- allows you to read/write files. Wow.
 *
 * Copyright © 1995, 2002 EPIC Software Labs
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

#include "irc.h"
#include "ircaux.h"
#include "files.h"
#include "window.h"
#include "output.h"

/* Here's the plan.
 *  You want to open a file.. you can READ it or you can WRITE it.
 *    unix files can be read/written, but its not the way you expect.
 *    so we will only alllow one or the other.  If you try to write to
 *    read only file, it punts, and if you try to read a writable file,
 *    you get a null.
 *
 * New functions: open(FILENAME <type>)
 *			<type> is 0 for read, 1 for write, 0 is default.
 *			Returns fd of opened file, -1 on error
 *		  read (fd)
 *			Returns line for given fd, as long as fd is
 *			opened via the open() call, -1 on error
 *		  write (fd text)
 *			Writes the text to the file pointed to by fd.
 *			Returns the number of bytes written, -1 on error
 *		  close (fd)
 *			closes file for given fd
 *			Returns 0 on OK, -1 on error
 *		  eof (fd)
 *			Returns 1 if fd is at EOF, 0 if not. -1 on error
 */

struct FILE___ {
	FILE *file;
	struct FILE___ *next;
};
typedef struct FILE___ File;

static File *FtopEntry = (File *) 0;

File *new_file (void)
{
	File *tmp = FtopEntry;
	File *tmpfile = (File *)new_malloc(sizeof(File));

	if (FtopEntry == (File *) 0)
		FtopEntry = tmpfile;
	else
	{
		while (tmp->next)
			tmp = tmp->next;
		tmp->next = tmpfile;
	}
	return tmpfile;
}

static void remove_file (File *file)
{
	File *tmp = FtopEntry;

	if (file == FtopEntry)
		FtopEntry = file->next;
	else
	{
		while (tmp->next && tmp->next != file)
			tmp = tmp->next;
		if (tmp->next)
			tmp->next = tmp->next->next;
	}
	fclose(file->file);
	new_free((char **)&file);
}


int open_file_for_read (char *filename)
{
	char *dummy_filename = (char *) 0;
	FILE *file;

	malloc_strcpy(&dummy_filename, filename);
	file = uzfopen(&dummy_filename, ".", 1);
	new_free(&dummy_filename);

	if (file)
	{
		File *nfs = new_file();
		nfs->file = file;
		nfs->next = (File *) 0;
		return fileno(file);
	}
	else
		return -1;
}

int open_file_for_write (char *filename)
{
	Filename expand;
	FILE *file;

	if (normalize_filename(filename, expand))
		strlcpy(expand, filename, sizeof(expand));

	if ((file = fopen(expand, "a")))
	{
		File *nfs = new_file();
		nfs->file = file;
		nfs->next = (File *) 0;
		return fileno(file);
	}
	else 
		return -1;
}

int* open_exec_for_in_out_err (char *filename, char **args)
{
	Filename expand;
	FILE **files;
static	int ret[3];

	if (normalize_filename(filename, expand))
		strlcpy(expand, filename, sizeof(expand));

	if ((files = open_exec(filename, args)))
	{
		int foo;
		for (foo = 0; foo < 3; foo++) {
			File *nfs = new_file();
			nfs->file = files[foo];
			nfs->next = (File *) 0;
			ret[foo] = fileno(files[foo]);
		}
		return ret;
	}
	else 
		return NULL;
}

static File *lookup_file (int fd)
{
	File *ptr = FtopEntry;

	while (ptr)
	{
		if (fileno(ptr->file) == fd)
			return ptr;
		else
			ptr = ptr -> next;
	}
	return (File *) 0;
}

static File *lookup_logfile (int fd)
{
	FILE *x = NULL;
	static File retval;
	Window *w;

	if (fd == -1)
		x = irclog_fp;
	if ((w = get_window_by_refnum(fd)))
		x = w->log_fp;

	retval.file = x;		/* XXX Should be a file */
	retval.next = NULL;
	return &retval;
}

int 	target_file_write (const char *fd, const char *stuff)
{
	if (*fd == 'w' && is_number(fd + 1))
		return file_write(1, my_atol(fd + 1), stuff);
	else if (is_number(fd))
		return file_write(0, my_atol(fd), stuff);
	else
		return -1;
}

int file_write (int window, int fd, const char *stuff)
{
	File 	*ptr;
	int	retval;

	if (window == 1)
		ptr = lookup_logfile(fd);
	else
		ptr = lookup_file(fd);

	if (!ptr || !ptr->file)
		return -1;

	retval = fprintf(ptr->file, "%s\n", stuff);
	if ((fflush(ptr->file)) == EOF)
		return -1;
	return retval;
}

int file_writeb (int window, int fd, char *stuff)
{
	File 	*ptr;
	int	retval;

	if (window == 1)
		ptr = lookup_logfile(fd);
	else
		ptr = lookup_file(fd);

	if (!ptr || !ptr->file)
		return -1;

	retval = fwrite(stuff, 1, strlen(stuff), ptr->file);
	if ((fflush(ptr->file)) == EOF)
		return -1;
	return retval;
}

char *file_read (int fd)
{
	File *ptr = lookup_file(fd);
	if (!ptr)
		return m_strdup(empty_string);
	else
	{
		char	*ret = NULL;
		char	*end = NULL;
		size_t	len = 0;
		size_t	newlen = 0;

		clearerr(ptr->file);

		for (;;)
		{
		    newlen += 4096;
		    RESIZE(ret, char, newlen);
		    ret[len] = 0;	/* Keep this -- C requires it! */
		    if (!fgets(ret + len, newlen - len, ptr->file))
			break;
		    if ((end = strchr(ret + len, '\n')))
			break;
		    len = newlen - 1;
		}

		/* Do we need to truncate the result? */
		if (end)
			*end = 0;	/* Either the newline */
		else if (ferror(ptr->file))
			*ret = 0;	/* Or the whole thing on error */

		return ret;
	}
}

char *file_readb (int fd, int numb)
{
	File *ptr = lookup_file(fd);
	if (!ptr)
		return m_strdup(empty_string);
	else
	{
		char *blah = (char *)new_malloc(numb+1);
		char *bleh = NULL;
		clearerr(ptr->file);
		numb = fread(blah, 1, numb, ptr->file);
		bleh = enquote_it(blah, numb);
		new_free(&blah);
		return bleh;
	}
}

int	file_eof (int fd)
{
	File *ptr = lookup_file (fd);
	if (!ptr)
		return -1;
	else
		return feof(ptr->file);
}

int	file_error (int fd)
{
	File *ptr = lookup_file (fd);
	if (!ptr)
		return -1;
	else
		return ferror(ptr->file);
}

int	file_rewind (int fd)
{
	File *ptr = lookup_file (fd);
	if (!ptr)
		return -1;
	else
	{
		rewind(ptr->file);
		return ferror(ptr->file);
	}
}

int	file_seek (int fd, long offset, const char *whence)
{
	File *ptr = lookup_file (fd);
	if (!ptr)
		return -1;

	if (!my_stricmp(whence, "SET"))
		return fseek(ptr->file, offset, SEEK_SET);
	else if (!my_stricmp(whence, "CUR"))
		return fseek(ptr->file, offset, SEEK_CUR);
	else if (!my_stricmp(whence, "END"))
		return fseek(ptr->file, offset, SEEK_END);
	else
		return -1;
}

int	file_skip (int fd, int lines)
{
	int line = 0;

	while (line < lines && !file_eof(fd))
	{
		char *foo = file_read(fd);
		new_free(&foo);
		line++;
	}
	if (file_eof(fd))
		line--;

	return line;
}

int	file_close (int fd)
{
	File *ptr = lookup_file (fd);
	if (!ptr)
		return -1;
	else
		remove_file (ptr);
	return 0;
}

int	file_valid (int fd)
{
	if (lookup_file(fd))
		return 1;
	return 0;
}


