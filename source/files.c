/* $EPIC: files.c,v 1.33 2008/01/15 13:02:21 jnelson Exp $ */
/*
 * files.c -- allows you to read/write files. Wow.
 *
 * Copyright © 1995, 2003 EPIC Software Labs
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
#include "elf.h"

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
        long int id;
        struct epic_loadfile *elf;
	struct FILE___ *next;
};
typedef struct FILE___ File;

static File *	FtopEntry = (File *) 0;

static File *	new_file (struct epic_loadfile *elf/*FILE *the_file*/)
{
        static long int id = 0;
        File *tmp = FtopEntry;
	File *tmp_file = (File *)new_malloc(sizeof(File));

        id++;

	if (!FtopEntry)
		FtopEntry = tmp_file;
	else
	{
		while (tmp->next)
			tmp = tmp->next;
		tmp->next = tmp_file;
	}

        tmp_file->id  = id;
        tmp_file->elf = elf;

	tmp_file->next = NULL;

	return tmp_file;
}

static void	remove_file (File *file)
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
	epic_fclose(file->elf);
	new_free((char **)&file);
}


int	open_file_for_read (const char *filename)
{
	char *dummy_filename = (char *) 0;
        struct epic_loadfile *elf;
        struct stat sb;

        File * fr;

	malloc_strcpy(&dummy_filename, filename);
	elf = uzfopen(&dummy_filename, ".", 1, &sb);
	new_free(&dummy_filename);

        if (!elf) {
                new_free(&elf);
                return -1;
        }

	if (sb.st_mode & 0111)
	{
		say("Cannot open %s -- executable file", filename);	
		return -1;
	}

	fr=new_file(elf);
	return fr->id;
}

int	open_file_for_write (const char *filename, const char *mode)
{
	Filename expand;
        struct epic_loadfile *elf;

        File * fr;

	if (normalize_filename(filename, expand))
		strlcpy(expand, filename, sizeof(expand));

	if (!(elf = epic_fopen(expand, mode, 1)))
		return -1;

	fr=new_file(elf);
	return fr->id;
}

int *	open_exec_for_in_out_err (const char *filename, char * const *args)
{
	Filename expand;
	FILE **files;
static	int ret[3];

struct  epic_loadfile *elf;
        File * fr;

	if (normalize_filename(filename, expand))
		strlcpy(expand, filename, sizeof(expand));

	if ((files = open_exec(filename, args)))
	{
		int foo;
		for (foo = 0; foo < 3; foo++) 
		{
                        if (!(elf=(struct epic_loadfile *)malloc(sizeof(struct epic_loadfile)))) {
                            yell("Not enough memory.");
                            return NULL;
                        }

                        elf->fp=files[foo];
#ifdef HAVE_LIBARCHIVE
                        elf->a=NULL;
#endif
                        fr=new_file(elf);
			ret[foo] = fr->id;
                }
                return ret;
	}
	else 
		return NULL;
}

static File *	lookup_file (int fd)
{
	File *ptr = FtopEntry;

	while (ptr)
	{
		if ((ptr->id) == fd)
			return ptr;
		else
			ptr = ptr -> next;
	}
	return NULL;
}

static File *	lookup_logfile (int fd)
{
	FILE *x = NULL;
	static File retval;
	Window *w;

	if (fd == -1)
		x = irclog_fp;
	else if ((w = get_window_by_refnum(fd)))
		x = w->log_fp;
	else
		return NULL;

	retval.elf->fp = x;		/* XXX Should be a file */
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

int	file_write (int window, int fd, const char *stuff)
{
	File 	*ptr;
	int	retval;

	if (window == 1)
		ptr = lookup_logfile(fd);
	else
		ptr = lookup_file(fd);

	if (!ptr || !ptr->elf->fp)
		return -1;

	retval = fprintf(ptr->elf->fp, "%s\n", stuff);
	if ((fflush(ptr->elf->fp)) == EOF)
		return -1;
	return retval;
}

int	file_writeb (int window, int fd, char *stuff)
{
	File 	*ptr;
	int	retval;
	size_t	len = strlen(stuff);

	if (window == 1)
		ptr = lookup_logfile(fd);
	else
		ptr = lookup_file(fd);

	if (!ptr || !ptr->elf->fp)
		return -1;

	stuff = dequote_it(stuff, &len);
	retval = fwrite(stuff, 1, len, ptr->elf->fp);
	new_free(&stuff);

	if ((fflush(ptr->elf->fp)) == EOF)
		return -1;
	return retval;
}

char *	file_read (int fd)
{
	File *ptr = lookup_file(fd);
	if (!ptr)
		return malloc_strdup(empty_string);
	else
	{
		char	*ret = NULL;
		char	*end = NULL;
		size_t	len = 0;
		size_t	newlen = 0;

                if (ptr->elf->fp)
                    clearerr(ptr->elf->fp);

		for (;;)
		{
		    newlen += 4096;
		    RESIZE(ret, char, newlen);
		    ret[len] = 0;	/* Keep this -- C requires it! */
		    if (!epic_fgets(ret + len, newlen - len, ptr->elf))
			break;
		    if ((end = strchr(ret + len, '\n')))
			break;
		    len = newlen - 1;
		}

		/* Do we need to truncate the result? */
		if (end)
                    *end = 0;	/* Either the newline */
		else if ( (ptr->elf->fp) && (ferror(ptr->elf->fp)) )
                    *ret = 0;	/* Or the whole thing on error */

		return ret;
	}
}

char *	file_readb (int fd, int numb)
{
	File *ptr = lookup_file(fd);
	if (!ptr)
		return malloc_strdup(empty_string);
	else
	{
		char *blah = (char *)new_malloc(numb+1);
		char *bleh = NULL;
                if (ptr->elf->fp) {
                    clearerr(ptr->elf->fp);
                    numb = fread(blah, 1, numb, ptr->elf->fp);
#ifdef HAVE_LIBARCHIVE
                } else if (ptr->elf->a) {
                    numb = archive_read_data(ptr->elf->a, blah, numb);
#endif
                } else {
                    /* others */
                }
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
		return epic_feof(ptr->elf);
}

int	file_error (int fd)
{
	File *ptr = lookup_file (fd);
	if (!ptr)
		return -1;
	else
		return ferror(ptr->elf->fp);
}

int	file_rewind (int fd)
{
	File *ptr = lookup_file (fd);
	if (!ptr)
		return -1;
	else
	{
		rewind(ptr->elf->fp);
		return ferror(ptr->elf->fp);
	}
}

/* LONG should support 64 bit */
int	file_seek (int fd, off_t offset, const char *whence)
{
	File *ptr = lookup_file (fd);
	if (!ptr)
		return -1;

	if (!my_stricmp(whence, "SET"))
		return fseek(ptr->elf->fp, offset, SEEK_SET);
	else if (!my_stricmp(whence, "CUR"))
		return fseek(ptr->elf->fp, offset, SEEK_CUR);
	else if (!my_stricmp(whence, "END"))
		return fseek(ptr->elf->fp, offset, SEEK_END);
	else
		return -1;
}

intmax_t	file_tell (int fd)
{
	File *ptr = lookup_file (fd);
	if (!ptr)
		return -1;
	else
		/* XXX Should call ftello(). */
		return (intmax_t)ftell(ptr->elf->fp);
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

/****************************************************************************/
#include "functions.h"
#include "sdbm.h"

static int	db_refnum = 0;

struct DBM___ {
	SDBM *	db;
	int	refnum;
	int	type;		/* Always 0 for now, future expansion */
	struct DBM___ *next;
};
typedef struct DBM___ Dbm;

static Dbm *	DtopEntry = (Dbm *) 0;

static Dbm *	new_dbm (SDBM *the_db, int type);
static void	remove_dbm (Dbm *db);
static int	open_dbm (const char *filename, int readonly, int type);
static Dbm *	lookup_dbm (int refnum);
static int	close_dbm (int refnum);
static int	write_to_dbm (int refnum, char *key, char *data, int replace);
static char *	read_from_dbm (int refnum, char *key);
static int	delete_from_dbm (int refnum, char *key);
static char *	iterate_on_dbm (int refnum, int restart);
static char *	all_keys_for_dbm (int refnum);
static int	error_from_dbm (int refnum);
static char *	Datum_to_string (Datum d);

static Dbm *	new_dbm (SDBM *the_db, int type)
{
	Dbm *tmp = DtopEntry;
	Dbm *tmp_db = (Dbm *)new_malloc(sizeof(Dbm));

	if (!DtopEntry)
		DtopEntry = tmp_db;
	else
	{
		while (tmp->next)
			tmp = tmp->next;
		tmp->next = tmp_db;
	}

	tmp_db->db = the_db;
	tmp_db->refnum = db_refnum++;
	tmp_db->type = type;
	tmp_db->next = NULL;

	return tmp_db;
}

static void	remove_dbm (Dbm *db)
{
	Dbm *tmp = DtopEntry;

	if (db == DtopEntry)
		DtopEntry = db->next;
	else
	{
		while (tmp->next && tmp->next != db)
			tmp = tmp->next;
		if (tmp->next)
			tmp->next = tmp->next->next;
	}
	sdbm_close(db->db);
	new_free((char **)&db);
}


static int	open_dbm (const char *filename, int readonly, int type)
{
	SDBM *db;
	Dbm *dbm;
	int	perm;

	if (readonly)
		perm = O_RDONLY;
	else
		perm = O_RDWR|O_CREAT;

	if (!(db = sdbm_open(filename, perm, 0660)))
	{
		yell("open_dbm(%s) failed: %s", filename, strerror(errno));
		return -1;
	}

	dbm = new_dbm(db, type);
	return dbm->refnum;
}

static Dbm *	lookup_dbm (int refnum)
{
	Dbm *ptr = DtopEntry;

	while (ptr)
	{
		if (ptr->refnum == refnum)
			return ptr;
		else
			ptr = ptr->next;
	}
	return NULL;
}

static int	close_dbm (int refnum)
{
	Dbm *db;

	if (!(db = lookup_dbm(refnum)))
		return -1;

	remove_dbm(db);
	return 0;
}

static int	write_to_dbm (int refnum, char *key, char *data, int replace)
{
	Dbm *db;
	Datum k, d;

	if (!(db = lookup_dbm(refnum)))
		return -1;

	k.dptr = key;
	k.dsize = strlen(key);
	d.dptr = data;
	d.dsize = strlen(data);
	if (sdbm_store(db->db, k, d, replace? DBM_REPLACE : DBM_INSERT))
		return sdbm_error(db->db);

	return 0;
}

/* RETURNS A MALLOCED STRING, EH! */
static char *	read_from_dbm (int refnum, char *key)
{
	Dbm *db;
	Datum k, d;

	if (!(db = lookup_dbm(refnum)))
		return NULL;

	k.dptr = key;
	k.dsize = strlen(key);
	d = sdbm_fetch(db->db, k);
	if (d.dptr == NULL)
		return NULL;

	return Datum_to_string(d);
}

static int	delete_from_dbm (int refnum, char *key)
{
	Dbm *	db;
	Datum 	k;
	int	retval;

	if (!(db = lookup_dbm(refnum)))
		return -1;

	k.dptr = key;
	k.dsize = strlen(key);
	retval = sdbm_delete(db->db, k);

	if (retval == 1)
		return -1;			/* Key Not found */
	else if (retval == -1)
		return sdbm_error(db->db);	/* Errno error */
	else
		return 0;
}

static char *	iterate_on_dbm (int refnum, int restart)
{
	Dbm *	db;
	Datum 	k;

	if (!(db = lookup_dbm(refnum)))
		return NULL;

	if (restart)
		k = sdbm_firstkey(db->db);
	else
		k = sdbm_nextkey(db->db);

	return Datum_to_string(k);
}

static char *	all_keys_for_dbm (int refnum)
{
	Dbm *	db;
	Datum 	k;
	char *	retval = NULL;
	size_t	clue = 0;
	char *	x;

	if (!(db = lookup_dbm(refnum)))
		return NULL;

	k = sdbm_firstkey(db->db);
	x = Datum_to_string(k);
	malloc_strcat_wordlist_c(&retval, space, x, &clue);
	new_free(&x);

	for (;;)
	{
		k = sdbm_nextkey(db->db);
		if (k.dptr == NULL)
			break;
		x = Datum_to_string(k);
		malloc_strcat_wordlist_c(&retval, space, x, &clue);
		new_free(&x);
	}

	return retval;
}

static int	error_from_dbm (int refnum)
{
	Dbm *	db;

	if (!(db = lookup_dbm(refnum)))
		return -1;

	return sdbm_error(db->db);
}

static char *	Datum_to_string (Datum d)
{
	char *retval;

	if (d.dptr == NULL)
		return NULL;

	retval = new_malloc(d.dsize + 1);
	memcpy(retval, d.dptr, d.dsize);
	retval[d.dsize] = 0;
	return retval;			/* MALLOCED, EH! */
}

/*
 * $dbmctl(OPEN type filename)
 *	Open a DBM file for read and write access.
 * $dbmctl(OPEN_READ type filename)
 *	Open a DBM file for read-only access.
 * $dbmctl(CLOSE refnum)
 *	Close a previously opened DBM file
 * $dbmctl(ADD refnum "key" data)
 *	Insert a new key/data pair.  Fail if key already exists.
 * $dbmctl(CHANGE refnum "key" data)
 *	If key already exists, change its data.  If it doesn't exist, add it.
 * $dbmctl(DELETE refnum "key")
 *	Remove a key/data pair
 * $dbmctl(READ refnum "key")
 *	Return the data for a key.
 * $dbmctl(NEXT_KEY refnum start-over)
 *	Return the next key in the database
 * $dbmctl(ALL_KEYS refnum)
 *	Return all keys -- could be huge! could take a long time!
 * $dbmctl(ERROR refnum)
 *	Return the errno for the last error.
 *
 * "refnum" is a value returned by OPEN and OPEN_READ.
 * "type" must always be "STD" for now. 
 * "filename" is a dbm file (without the .db extension!)
 * "key" is a dbm key.  Spaces are important!
 * "data" is a dbm value.  Spaces are important!
 * 
 */
char *	dbmctl (char *input)
{
	char *	listc;
	int	refnum;
	char *	type;
	char *	key;
	int	retval;
	char *	retstr;

	GET_FUNC_ARG(listc, input);
	if (!my_strnicmp(listc, "OPEN", 4)) {
		GET_FUNC_ARG(type, input);	/* Ignored for now */
		retval = open_dbm(input, 0, 0);
		RETURN_INT(retval);
	} else if (!my_strnicmp(listc, "OPEN_READ", 5)) {
		GET_FUNC_ARG(type, input);	/* Ignored for now */
		retval = open_dbm(input, 1, 0);
		RETURN_INT(retval);
	} else if (!my_strnicmp(listc, "CLOSE", 2)) {
		GET_INT_ARG(refnum, input);
		retval = close_dbm(refnum);
		RETURN_INT(retval);
	} else if (!my_strnicmp(listc, "ADD", 2)) {
		GET_INT_ARG(refnum, input);
		GET_DWORD_ARG(key, input);
		retval = write_to_dbm(refnum, key, input, 0);
		RETURN_INT(retval);
	} else if (!my_strnicmp(listc, "CHANGE", 2)) {
		GET_INT_ARG(refnum, input);
		GET_DWORD_ARG(key, input);
		retval = write_to_dbm(refnum, key, input, 1);
		RETURN_INT(retval);
	} else if (!my_strnicmp(listc, "DELETE", 1)) {
		GET_INT_ARG(refnum, input);
		GET_DWORD_ARG(key, input);
		retval = delete_from_dbm(refnum, key);
		RETURN_INT(retval);
	} else if (!my_strnicmp(listc, "READ", 1)) {
		GET_INT_ARG(refnum, input);
		GET_DWORD_ARG(key, input);
		retstr = read_from_dbm(refnum, key);
		RETURN_MSTR(retstr);
	} else if (!my_strnicmp(listc, "NEXT_KEY", 1)) {
		int	restart;
		GET_INT_ARG(refnum, input);
		GET_INT_ARG(restart, input);
		retstr = iterate_on_dbm(refnum, restart);
		RETURN_MSTR(retstr);
	} else if (!my_strnicmp(listc, "ALL_KEYS", 2)) {
		GET_INT_ARG(refnum, input);
		retstr = all_keys_for_dbm(refnum);
		RETURN_MSTR(retstr);
	} else if (!my_strnicmp(listc, "ERROR", 1)) {
		GET_INT_ARG(refnum, input);
		retval = error_from_dbm(refnum);
		RETURN_INT(retval);
	}

	RETURN_EMPTY;
}

