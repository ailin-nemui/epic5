/*
 * sdbm - ndbm work-alike hashed database library
 * based on Per-Aake Larson's Dynamic Hashing algorithms. BIT 18 (1978).
 * author: oz@nexus.yorku.ca
 * status: public domain.
 *
 * Reponsible party: Jeremy Nelson of EPIC Software Labs (2005-11-01)
 * Taken from: apache-2.1, cross-referenced with perl-5.8.7 in order to remove
 *             all of the apache owned code.  In good faith I say that as far
 *	       as I know, this file is wholly public domain.
 * Note: I didn't change anything!  I swear! ;-)  It is my fervent hope that
 *       files created by this file are compatable with the sdbm support in
 *       perl and apache.  I didn't test it against ndbm or gdbm.
 */
#include "irc_std.h"
#include "sdbm.h"

#define DBLKSIZ 4096
#define PBLKSIZ 1024
#define PAIRMAX 1008			/* arbitrary on PBLKSIZ-N */
#define SPLTMAX	10			/* maximum allowed splits */
					/* for a single insertion */
#define DIRFEXT	".dir"
#define PAGFEXT	".pag"

struct SDBM {
	int dirf;		       /* directory file descriptor */
	int pagf;		       /* page file descriptor */
	int flags;		       /* status/error flags, see below */
	long maxbno;		       /* size of dirfile in bits */
	long curbit;		       /* current bit number */
	long hmask;		       /* current hash mask */
	long blkptr;		       /* current block for nextkey */
	int keyptr;		       /* current key for nextkey */
	long blkno;		       /* current page to read/write */
	long pagbno;		       /* current page in pagbuf */
	char pagbuf[PBLKSIZ];	       /* page file block buffer */
	long dirbno;		       /* current block in dirbuf */
	char dirbuf[DBLKSIZ];	       /* directory file block buffer */
	int  error;			/* Errno value */
};

#define DBM_RDONLY	0x1	       /* data base open read-only */
#define DBM_IOERR	0x2	       /* data base I/O error */

/*
 * utility macros
 */
#define sdbm_rdonly(db)		((db)->flags & DBM_RDONLY)

#define BYTESIZ		8

static	SDBM *	sdbm__prep 	(char *, char *, int, int);
static	int	sdbm__fitpair 	(char *, int);
static	void	sdbm__putpair 	(char *, datum, datum);
static	datum	sdbm__getpair 	(char *, datum);
static	int	sdbm__delpair 	(char *, datum);
static	int	sdbm__chkpage 	(char *);
static	datum	sdbm__getnkey 	(char *, int);
static	void	sdbm__splpage 	(char *, char *, long);
static	int	sdbm__duppair 	(char *, datum);
static 	long	sdbm__hash	(char *str, int len);
static 	int 	sdbm__getdbit 	(SDBM *, long);
static 	int 	sdbm__setdbit 	(SDBM *, long);
static 	int 	sdbm__getpage 	(SDBM *, long);
static 	datum 	sdbm__getnext 	(SDBM *);
static 	int 	sdbm__makroom	(SDBM *, long, int);
static 	int 	sdbm__seepair 	(char *, int, char *, int);

/*
 * useful macros
 */
#define bad(x)		((x).dptr == NULL || (x).dsize <= 0)
#define exhash(item)	sdbm__hash((item).dptr, (item).dsize)
#define ioerr(db)	((db)->flags |= DBM_IOERR, (db)->error = errno)

#define OFF_PAG(off)	(long) (off) * PBLKSIZ
#define OFF_DIR(off)	(long) (off) * DBLKSIZ

static long masks[] = {
	000000000000, 000000000001, 000000000003, 000000000007,
	000000000017, 000000000037, 000000000077, 000000000177,
	000000000377, 000000000777, 000000001777, 000000003777,
	000000007777, 000000017777, 000000037777, 000000077777,
	000000177777, 000000377777, 000000777777, 000001777777,
	000003777777, 000007777777, 000017777777, 000037777777,
	000077777777, 000177777777, 000377777777, 000777777777,
	001777777777, 003777777777, 007777777777, 017777777777
};

datum nullitem = {NULL, 0};

SDBM *sdbm_open (char *file, int flags, int mode)
{
	SDBM *db;
	char *dirname;
	char *pagname;
	int n;

	if (file == NULL || !*file)
		return errno = EINVAL, (SDBM *) NULL;
/*
 * need space for two seperate filenames
 */
	n = strlen(file) * 2 + strlen(DIRFEXT) + strlen(PAGFEXT) + 2;

	if ((dirname = malloc((unsigned) n)) == NULL)
		return errno = ENOMEM, (SDBM *) NULL;
/*
 * build the file names
 */
	dirname = strcat(strcpy(dirname, file), DIRFEXT);
	pagname = strcpy(dirname + strlen(dirname) + 1, file);
	pagname = strcat(pagname, PAGFEXT);

#ifdef NETWARE
	locking_sem = OpenLocalSemaphore (1);
#endif

	db = sdbm__prep(dirname, pagname, flags, mode);
	free((char *) dirname);
	return db;
}

static SDBM *	sdbm__prep (char *dirname, char *pagname, int flags, int mode)
{
	SDBM *db;
	struct stat dstat;

	if ((db = (SDBM *) malloc(sizeof(SDBM))) == NULL)
		return errno = ENOMEM, (SDBM *) NULL;

        db->flags = 0;
        db->hmask = 0;
        db->blkptr = 0;
        db->keyptr = 0;
/*
 * adjust user flags so that WRONLY becomes RDWR, 
 * as required by this package. Also set our internal
 * flag for RDONLY if needed.
 */
	if (flags & O_WRONLY)
		flags = (flags & ~O_WRONLY) | O_RDWR;

	else if ((flags & 03) == O_RDONLY)
		db->flags = DBM_RDONLY;

	if ((db->pagf = open(pagname, flags, mode)) > -1) {
		if ((db->dirf = open(dirname, flags, mode)) > -1) {
/*
 * need the dirfile size to establish max bit number.
 */
			if (fstat(db->dirf, &dstat) == 0) {
/*
 * zero size: either a fresh database, or one with a single,
 * unsplit data page: dirpage is all zeros.
 */
				db->dirbno = (!dstat.st_size) ? 0 : -1;
				db->pagbno = -1;
				db->maxbno = dstat.st_size * BYTESIZ;

				(void) memset(db->pagbuf, 0, PBLKSIZ);
				(void) memset(db->dirbuf, 0, DBLKSIZ);
			/*
			 * success
			 */
				return db;
			}
			(void) close(db->dirf);
		/* }
		(void) sdbm_fd_unlock(db->pagf); */
	    }
	    (void) close(db->pagf);
	}
	free((char *) db);
	return (SDBM *) NULL;
}

void	sdbm_close(SDBM *db)
{
	if (db == NULL)
		errno = EINVAL;
	else {
		(void) close(db->dirf);
		/* (void) sdbm_fd_unlock(db->pagf); */
		(void) close(db->pagf);
		free((char *) db);
	}
}

datum	sdbm_fetch (SDBM *db, datum key)
{
	if (db == NULL || bad(key))
		return errno = EINVAL, nullitem;

	if (sdbm__getpage(db, exhash(key)))
		return sdbm__getpair(db->pagbuf, key);

	return ioerr(db), nullitem;
}

int	sdbm_delete (SDBM *db, datum key)
{
	if (db == NULL || bad(key))
		return errno = EINVAL, -1;
	if (sdbm_rdonly(db))
		return errno = EPERM, -1;

	if (sdbm__getpage(db, exhash(key))) {
		if (!sdbm__delpair(db->pagbuf, key))
			return -1;
/*
 * update the page file
 */
		if (lseek(db->pagf, OFF_PAG(db->pagbno), SEEK_SET) < 0
		    || write(db->pagf, db->pagbuf, PBLKSIZ) < 0)
			return ioerr(db), -1;

		return 0;
	}

	return ioerr(db), -1;
}

int	sdbm_store (SDBM *db, datum key, datum val, int flags)
{
	int need;
	long hash;

	if (db == NULL || bad(key))
		return errno = EINVAL, -1;
	if (sdbm_rdonly(db))
		return errno = EPERM, -1;

	need = key.dsize + val.dsize;
/*
 * is the pair too big (or too small) for this database ??
 */
	if (need < 0 || need > PAIRMAX)
		return errno = EINVAL, -1;

	if (sdbm__getpage(db, (hash = exhash(key)))) {
/*
 * if we need to replace, delete the key/data pair
 * first. If it is not there, ignore.
 */
		if (flags == DBM_REPLACE)
			(void) sdbm__delpair(db->pagbuf, key);
		else if (sdbm__duppair(db->pagbuf, key))
			return 1;
/*
 * if we do not have enough room, we have to split.
 */
		if (!sdbm__fitpair(db->pagbuf, need))
			if (!sdbm__makroom(db, hash, need))
				return ioerr(db), -1;
/*
 * we have enough room or split is successful. insert the key,
 * and update the page file.
 */
		(void) sdbm__putpair(db->pagbuf, key, val);

		if (lseek(db->pagf, OFF_PAG(db->pagbno), SEEK_SET) < 0
		    || write(db->pagf, db->pagbuf, PBLKSIZ) < 0)
			return ioerr(db), -1;
	/*
	 * success
	 */
		return 0;
	}

	return ioerr(db), -1;
}

/*
 * sdbm__makroom - make room by splitting the overfull page
 * this routine will attempt to make room for SPLTMAX times before
 * giving up.
 */
static int	sdbm__makroom (SDBM *db, long hash, int need)
{
	long newp;
	char twin[PBLKSIZ];
	char *pag = db->pagbuf;
	char *new = twin;
	int smax = SPLTMAX;

	do {
/*
 * split the current page
 */
		(void) sdbm__splpage(pag, new, db->hmask + 1);
/*
 * address of the new page
 */
		newp = (hash & db->hmask) | (db->hmask + 1);

/*
 * write delay, read avoidence/cache shuffle:
 * select the page for incoming pair: if key is to go to the new page,
 * write out the previous one, and copy the new one over, thus making
 * it the current page. If not, simply write the new page, and we are
 * still looking at the page of interest. current page is not updated
 * here, as sdbm_store will do so, after it inserts the incoming pair.
 */
		if (hash & (db->hmask + 1)) {
			if (lseek(db->pagf, OFF_PAG(db->pagbno), SEEK_SET) < 0
			    || write(db->pagf, db->pagbuf, PBLKSIZ) < 0)
				return 0;
			db->pagbno = newp;
			(void) memcpy(pag, new, PBLKSIZ);
		}
		else if (lseek(db->pagf, OFF_PAG(newp), SEEK_SET) < 0
			 || write(db->pagf, new, PBLKSIZ) < 0)
			return 0;

		if (!sdbm__setdbit(db, db->curbit))
			return 0;
/*
 * see if we have enough room now
 */
		if (sdbm__fitpair(pag, need))
			return 1;
/*
 * try again... update curbit and hmask as sdbm__getpage would have
 * done. because of our update of the current page, we do not
 * need to read in anything. BUT we have to write the current
 * [deferred] page out, as the window of failure is too great.
 */
		db->curbit = 2 * db->curbit +
			((hash & (db->hmask + 1)) ? 2 : 1);
		db->hmask |= db->hmask + 1;

		if (lseek(db->pagf, OFF_PAG(db->pagbno), SEEK_SET) < 0
		    || write(db->pagf, db->pagbuf, PBLKSIZ) < 0)
			return 0;

	} while (--smax);
/*
 * if we are here, this is real bad news. After SPLTMAX splits,
 * we still cannot fit the key. say goodnight.
 */
	errno = ERANGE;
	return 0;

}

/*
 * the following two routines will break if
 * deletions aren't taken into account. (ndbm bug)
 */
datum	sdbm_firstkey (SDBM *db)
{
	if (db == NULL)
		return errno = EINVAL, nullitem;
/*
 * start at page 0
 */
	if (lseek(db->pagf, OFF_PAG(0), SEEK_SET) < 0
	    || read(db->pagf, db->pagbuf, PBLKSIZ) < 0)
		return ioerr(db), nullitem;
	db->pagbno = 0;
	db->blkptr = 0;
	db->keyptr = 0;

	return sdbm__getnext(db);
}

datum	sdbm_nextkey (SDBM *db)
{
	if (db == NULL)
		return errno = EINVAL, nullitem;
	return sdbm__getnext(db);
}

int	sdbm_error (SDBM *db)
{
	if (db == NULL)
		return -1;
	return db->error;
}

/*
 * all important binary trie traversal
 */
static int	sdbm__getpage (SDBM *db, long hash)
{
	int hbit;
	long dbit;
	long pagb;

	dbit = 0;
	hbit = 0;
	while (dbit < db->maxbno && sdbm__getdbit(db, dbit))
		dbit = 2 * dbit + ((hash & (1 << hbit++)) ? 2 : 1);

	db->curbit = dbit;
	db->hmask = masks[hbit];

	pagb = hash & db->hmask;
/*
 * see if the block we need is already in memory.
 * note: this lookaside cache has about 10% hit rate.
 */
	if (pagb != db->pagbno) { 
/*
 * note: here, we assume a "hole" is read as 0s.
 * if not, must zero pagbuf first.
 */
		if (lseek(db->pagf, OFF_PAG(pagb), SEEK_SET) < 0
		    || read(db->pagf, db->pagbuf, PBLKSIZ) < 0)
			return 0;
		if (!sdbm__chkpage(db->pagbuf))
			return 0;
		db->pagbno = pagb;
	}
	return 1;
}

static int	sdbm__getdbit (SDBM *db, long dbit)
{
	long c;
	long dirb;

	c = dbit / BYTESIZ;
	dirb = c / DBLKSIZ;

	if (dirb != db->dirbno) {
		if (lseek(db->dirf, OFF_DIR(dirb), SEEK_SET) < 0
		    || read(db->dirf, db->dirbuf, DBLKSIZ) < 0)
			return 0;
		db->dirbno = dirb;
	}

	return db->dirbuf[c % DBLKSIZ] & (1 << dbit % BYTESIZ);
}

static int	sdbm__setdbit (SDBM *db, long dbit)
{
	long c;
	long dirb;

	c = dbit / BYTESIZ;
	dirb = c / DBLKSIZ;

	if (dirb != db->dirbno) {
		if (lseek(db->dirf, OFF_DIR(dirb), SEEK_SET) < 0
		    || read(db->dirf, db->dirbuf, DBLKSIZ) < 0)
			return 0;
		db->dirbno = dirb;
	}

	db->dirbuf[c % DBLKSIZ] |= (1 << dbit % BYTESIZ);

	if (dbit >= db->maxbno)
		db->maxbno += DBLKSIZ * BYTESIZ;

	if (lseek(db->dirf, OFF_DIR(dirb), SEEK_SET) < 0
	    || write(db->dirf, db->dirbuf, DBLKSIZ) < 0)
		return 0;

	return 1;
}

/*
 * sdbm__getnext - get the next key in the page, and if done with
 * the page, try the next page in sequence
 */
static datum	sdbm__getnext (SDBM *db)
{
	datum key;

	for (;;) {
		db->keyptr++;
		key = sdbm__getnkey(db->pagbuf, db->keyptr);
		if (key.dptr != NULL)
			return key;
/*
 * we either run out, or there is nothing on this page..
 * try the next one... If we lost our position on the
 * file, we will have to seek.
 */
		db->keyptr = 0;
		if (db->pagbno != db->blkptr++)
			if (lseek(db->pagf, OFF_PAG(db->blkptr), SEEK_SET) < 0)
				break;
		db->pagbno = db->blkptr;
		if (read(db->pagf, db->pagbuf, PBLKSIZ) <= 0)
			break;
		if (!sdbm__chkpage(db->pagbuf))
			break;
	}

	return ioerr(db), nullitem;
}

/*
 * polynomial conversion ignoring overflows
 * [this seems to work remarkably well, in fact better
 * then the ndbm hash function. Replace at your own risk]
 * use: 65599	nice.
 *      65587   even better. 
 */
static long	sdbm__hash (char *str, int len)
{
	unsigned long n = 0;

#define DUFF	/* go ahead and use the loop-unrolled version */
#ifdef DUFF

#define HASHC	n = *str++ + 65599 * n

	if (len > 0) {
		int loop = (len + 8 - 1) >> 3;

		switch(len & (8 - 1)) {
		case 0:	do {
			HASHC;	case 7:	HASHC;
		case 6:	HASHC;	case 5:	HASHC;
		case 4:	HASHC;	case 3:	HASHC;
		case 2:	HASHC;	case 1:	HASHC;
			} while (--loop);
		}

	}
#else
	while (len--)
		n = *str++ + 65599 * n;
#endif
	return n;
}

/*
 * page format:
 *	+------------------------------+
 * ino	| n | keyoff | datoff | keyoff |
 * 	+------------+--------+--------+
 *	| datoff | - - - ---->	       |
 *	+--------+---------------------+
 *	|	 F R E E A R E A       |
 *	+--------------+---------------+
 *	|  <---- - - - | data          |
 *	+--------+-----+----+----------+
 *	|  key   | data     | key      |
 *	+--------+----------+----------+
 *
 * calculating the offsets for free area:  if the number
 * of entries (ino[0]) is zero, the offset to the END of
 * the free area is the block size. Otherwise, it is the
 * nth (ino[ino[0]]) entry's offset.
 */

static int	sdbm__fitpair (char *pag, int need)
{
	int n;
	int off;
	int avail;
	short *ino = (short *) pag;

	off = ((n = ino[0]) > 0) ? ino[n] : PBLKSIZ;
	avail = off - (n + 1) * sizeof(short);
	need += 2 * sizeof(short);

	return need <= avail;
}

static void	sdbm__putpair (char *pag, datum key, datum val)
{
	int n;
	int off;
	short *ino = (short *) pag;

	off = ((n = ino[0]) > 0) ? ino[n] : PBLKSIZ;
/*
 * enter the key first
 */
	off -= key.dsize;
	(void) memcpy(pag + off, key.dptr, key.dsize);
	ino[n + 1] = off;
/*
 * now the data
 */
	off -= val.dsize;
	(void) memcpy(pag + off, val.dptr, val.dsize);
	ino[n + 2] = off;
/*
 * adjust item count
 */
	ino[0] += 2;
}

static datum	sdbm__getpair (char *pag, datum key)
{
	int i;
	int n;
	datum val;
	short *ino = (short *) pag;

	if ((n = ino[0]) == 0)
		return nullitem;

	if ((i = sdbm__seepair(pag, n, key.dptr, key.dsize)) == 0)
		return nullitem;

	val.dptr = pag + ino[i + 1];
	val.dsize = ino[i] - ino[i + 1];
	return val;
}

static int	sdbm__duppair (char *pag, datum key)
{
	short *ino = (short *) pag;
	return ino[0] > 0 && sdbm__seepair(pag, ino[0], key.dptr, key.dsize) > 0;
}

static datum	sdbm__getnkey (char *pag, int num)
{
	datum key;
	int off;
	short *ino = (short *) pag;

	num = num * 2 - 1;
	if (ino[0] == 0 || num > ino[0])
		return nullitem;

	off = (num > 1) ? ino[num - 1] : PBLKSIZ;

	key.dptr = pag + ino[num];
	key.dsize = off - ino[num];

	return key;
}

static int	sdbm__delpair (char *pag, datum key)
{
	int n;
	int i;
	short *ino = (short *) pag;

	if ((n = ino[0]) == 0)
		return 0;

	if ((i = sdbm__seepair(pag, n, key.dptr, key.dsize)) == 0)
		return 0;
/*
 * found the key. if it is the last entry
 * [i.e. i == n - 1] we just adjust the entry count.
 * hard case: move all data down onto the deleted pair,
 * shift offsets onto deleted offsets, and adjust them.
 * [note: 0 < i < n]
 */
	if (i < n - 1) {
		int m;
		char *dst = pag + (i == 1 ? PBLKSIZ : ino[i - 1]);
		char *src = pag + ino[i + 1];
		int   zoo = dst - src;

/*
 * shift data/keys down
 */
		m = ino[i + 1] - ino[n];

#undef DUFF	/* just use memmove. it should be plenty fast. */
#ifdef DUFF
#define MOVB 	*--dst = *--src

		if (m > 0) {
			int loop = (m + 8 - 1) >> 3;

			switch (m & (8 - 1)) {
			case 0:	do {
				MOVB;	case 7:	MOVB;
			case 6:	MOVB;	case 5:	MOVB;
			case 4:	MOVB;	case 3:	MOVB;
			case 2:	MOVB;	case 1:	MOVB;
				} while (--loop);
			}
		}
#else
		dst -= m;
		src -= m;
		memmove(dst, src, m);
#endif

/*
 * adjust offset index up
 */
		while (i < n - 1) {
			ino[i] = ino[i + 2] + zoo;
			i++;
		}
	}
	ino[0] -= 2;
	return 1;
}

/*
 * search for the key in the page.
 * return offset index in the range 0 < i < n.
 * return 0 if not found.
 */
static int	sdbm__seepair (char *pag, int n, char *key, int siz)
{
	int i;
	int off = PBLKSIZ;
	short *ino = (short *) pag;

	for (i = 1; i < n; i += 2) {
		if (siz == off - ino[i] &&
		    memcmp(key, pag + ino[i], siz) == 0)
			return i;
		off = ino[i + 1];
	}
	return 0;
}

static void	sdbm__splpage (char *pag, char *new, long sbit)
{
	datum key;
	datum val;

	int n;
	int off = PBLKSIZ;
	char cur[PBLKSIZ];
	short *ino = (short *) cur;

	(void) memcpy(cur, pag, PBLKSIZ);
	(void) memset(pag, 0, PBLKSIZ);
	(void) memset(new, 0, PBLKSIZ);

	n = ino[0];
	for (ino++; n > 0; ino += 2) {
		key.dptr = cur + ino[0]; 
		key.dsize = off - ino[0];
		val.dptr = cur + ino[1];
		val.dsize = ino[0] - ino[1];
/*
 * select the page pointer (by looking at sbit) and insert
 */
		(void) sdbm__putpair((exhash(key) & sbit) ? new : pag, key, val);

		off = ino[1];
		n -= 2;
	}
}

/*
 * check page sanity: 
 * number of entries should be something
 * reasonable, and all offsets in the index should be in order.
 * this could be made more rigorous.
 */
static int	sdbm__chkpage (char *pag)
{
	int n;
	int off;
	short *ino = (short *) pag;

	if ((n = ino[0]) < 0 || n > PBLKSIZ / (int)sizeof(short))
		return 0;

	if (n > 0) {
		off = PBLKSIZ;
		for (ino++; n > 0; ino += 2) {
			if (ino[0] > off || ino[1] > off ||
			    ino[1] > ino[0])
				return 0;
			off = ino[1];
			n -= 2;
		}
	}
	return 1;
}

/* End */
