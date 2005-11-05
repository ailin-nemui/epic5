/*
 * sdbm - ndbm work-alike hashed database library
 * based on Per-Ake Larson's Dynamic Hashing algorithms. BIT 18 (1978).
 * author: oz@nexus.yorku.ca
 * status: public domain. 
 */
struct SDBM;
typedef struct SDBM SDBM;

typedef struct {
	const char *dptr;
	int dsize;
} Datum;

/*
 * flags to sdbm_store
 */
#define DBM_INSERT	0
#define DBM_REPLACE	1

/*
 * ndbm interface
 */
extern SDBM *	sdbm_open (const char *, int, int);
extern void 	sdbm_close (SDBM *);
extern Datum 	sdbm_fetch (SDBM *, Datum);
extern int 	sdbm_delete (SDBM *, Datum);
extern int 	sdbm_store (SDBM *, Datum, Datum, int);
extern Datum 	sdbm_firstkey (SDBM *);
extern Datum 	sdbm_nextkey (SDBM *);
extern int	sdbm_error (SDBM *);

