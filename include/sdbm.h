/*
 * sdbm - ndbm work-alike hashed database library
 * based on Per-Ake Larson's Dynamic Hashing algorithms. BIT 18 (1978).
 * author: oz@nexus.yorku.ca
 * status: public domain. 
 */
struct SDBM;
typedef struct SDBM SDBM;

typedef struct {
	char *dptr;
	int dsize;
} datum;

/*
 * flags to sdbm_store
 */
#define DBM_INSERT	0
#define DBM_REPLACE	1

/*
 * ndbm interface
 */
extern SDBM *	sdbm_open (char *, int, int);
extern void 	sdbm_close (SDBM *);
extern datum 	sdbm_fetch (SDBM *, datum);
extern int 	sdbm_delete (SDBM *, datum);
extern int 	sdbm_store (SDBM *, datum, datum, int);
extern datum 	sdbm_firstkey (SDBM *);
extern datum 	sdbm_nextkey (SDBM *);
extern int	sdbm_error (SDBM *);

