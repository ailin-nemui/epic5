/*
 * files.c -- allows you to read/write files. Wow.
 *
 * Written by Jeremy Nelson
 * Copyright 1995 Jeremy Nelson
 * See the COPYRIGHT file for more information
 */

#include "irc.h"
#include "ircaux.h"

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

	/* XXXX - this looks like a memory leak */
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
	/* patch by Scott H Kilau so expand_twiddle works */
	char *expand = NULL;
	FILE *file;

	if (!(expand = expand_twiddle(filename)))
		malloc_strcpy(&expand, filename);
	file = fopen(expand, "a");
	new_free(&expand);
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

int file_write (int fd, char *stuff)
{
	File 	*ptr = lookup_file(fd);
	int	retval;

	if (!ptr)
		return -1;

	retval = fprintf(ptr->file, "%s\n", stuff);
	if ((fflush(ptr->file)) == EOF)
		return -1;
	return retval;
}

int file_writeb (int fd, char *stuff)
{
	File 	*ptr = lookup_file(fd);
	int	retval;

	if (!ptr)
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
#if 0
		char	*ret = NULL;
		size_t	len = 0, newlen = 4096;

		while (1) {
			new_realloc((void **)&ret, newlen);
			ret[len] = 0;
			if (NULL == fgets(ret+len, newlen-len, ptr->file))
				break;
			if (NULL != strchr(ret+len, '\n')) break;
			len = newlen - 1;
			newlen += 4096;
		}

		chomp(ret+len);
		new_realloc((void **)&ret, len+strlen(ret+len));

		return ret ? ret : m_strdup(empty_string);
#else
		char blah[10240];
		if (fgets(blah, 10239, ptr->file))
			chop(blah, 1);
		else
			blah[0] = 0;
		return m_strdup(blah);
#endif
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
		fread(blah, 1, numb, ptr->file);
		blah[numb] = 0;
		return blah;
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

int	file_skip (int fd)
{
	char blah[10240];
	File *ptr = lookup_file (fd);

	if (!ptr)
		return -1;

	if (fgets(blah, 10239, ptr->file))
		return feof(ptr->file);
	else
		return -1;
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


