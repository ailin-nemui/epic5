/*
 * ircaux.h: header file for ircaux.c 
 *
 * Written By Michael Sandrof
 *
 * Copyright(c) 1990 
 *
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 *
 * @(#)$Id: ircaux.h,v 1.72 2005/01/12 00:12:20 jnelson Exp $
 */

#ifndef _IRCAUX_H_
#define _IRCAUX_H_

#include "compat.h"
#include "network.h"
#include "words.h"

struct metric_time {
	long mt_days;
	double mt_mdays;
};

typedef int 	comp_len_func 		(char *, char *, int);
typedef int 	comp_func 		(char *, char *);

#define new_malloc(x) really_new_malloc	((x), __FILE__, __LINE__)
#define new_free(x)   really_new_free	((void **)(x), __FILE__, __LINE__)
#define new_realloc(x,y) really_new_realloc ((x), (y), __FILE__, __LINE__)
#define MUST_BE_MALLOCED(x, y) \
		fatal_malloc_check ((void *)(x), (y), __FILE__, __LINE__)
#define RESIZE(x, y, z) new_realloc	((void **)& (x), sizeof(y) * (z))

#define malloc_strcat(x,y) malloc_strcat_c((x),(y),NULL)
#define malloc_strcat_ues(x,y,z) malloc_strcat_ues_c((x),(y),(z),NULL)

extern	int	need_delayed_free;
void	fatal_malloc_check	(void *, const char *, const char *, int);
void *	really_new_malloc 	(size_t, const char *, int);
void *	really_new_free 	(void **, const char *, int);
void *	really_new_realloc 	(void **, size_t, const char *, int);
void	malloc_dump		(const char *);

char *	check_nickname 		(char *, int);
char *	new_new_next_arg_count 	(char *, char **, char *, int);
char *	s_next_arg		(char **);
char *	last_arg 		(char **, size_t *cluep);

int	normalize_filename	(const char *, Filename);
int	expand_twiddle 		(const char *, Filename);
char *	upper 			(char *);
char *	lower 			(char *);
char *	sindex			(char *, const char *);
char *	rsindex 		(char *, char *, char *, int);
int	path_search 		(const char *, const char *, Filename);
char *	double_quote 		(const char *, const char *, char *);
char *	malloc_strcat_c		(char **, const char *, size_t *);
void	wait_new_free 		(char **);
char *	malloc_sprintf 		(char **, const char *, ...) __A(2);
int	is_number 		(const char *);
int	is_real_number 		(const char *);
char *	my_ctime 		(time_t);
extern	unsigned char	stricmp_table[];
int	my_stricmp 		(const unsigned char *, const unsigned char *);
int	my_strnicmp 		(const unsigned char *, const unsigned char *, size_t);
void	really_free 		(int);
char *	chop 			(char *, size_t);
char *	malloc_strcat_ues_c	(char **, const char *, const char *, size_t *);
char *	strlopencat		(char *, size_t, ...);
ssize_t	stristr 		(const char *, const char *);
ssize_t	rstristr 		(const char *, const char *);
char *	findchar		(char *, int);
FILE *	uzfopen 		(char **, const char *, int, struct stat *);
int	end_strcmp 		(const char *, const char *, size_t);
char*   exec_pipe		(const char *, char *, size_t *, char *const *);
FILE **	open_exec		(const char *executable, char * const *args);
void	panic 			(const char *, ...) __A(1) __N;
int	fw_strcmp 		(comp_len_func *, char *, char *);
int	lw_strcmp 		(comp_func *, char *, char *);
int	open_to 		(char *, int, int);
struct metric_time get_metric_time	(double *);
struct metric_time timeval_to_metric	(const Timeval *);
Timeval get_time		(Timeval *);
double 	time_diff 		(const Timeval, const Timeval);
Timeval time_add		(const Timeval, const Timeval);
Timeval time_subtract		(const Timeval, const Timeval);
Timeval double_to_timeval 	(double);
const char *	plural 		(int);
double	time_to_next_minute 	(void);
char *	remove_trailing_spaces 	(char *, size_t *cluep);
char *	forcibly_remove_trailing_spaces (char *, size_t *);
char *	ltoa 			(long);
char *	ftoa			(double);
char *	strformat 		(char *, const char *, ssize_t, int);
char *	chop_word 		(char *);
char *	skip_spaces		(char *);
int	split_args		(char *, char **to, size_t);
int	splitw 			(char *, const char ***);
char *	unsplitw 		(const char ***, int);
int	check_val 		(const char *);
char *	strext	 		(const char *, const char *);
char *	strextend 		(char *, char, int);
char *	pullstr 		(char *, char *);
int 	empty 			(const char *);
char *	safe_new_next_arg	(char *, char **);
ssize_t	MatchingBracket 	(const char *, char, char);
int	parse_number 		(char **);
char *	remove_brackets 	(const char *, const char *);
long	my_atol 		(const char *);
char *	malloc_dupchar 		(int);
off_t	file_size		(const char *);
int	file_exists		(const char *);
int	isdir			(const char *);
int	is_root			(const char *, const char *, int);
size_t	streq			(const char *, const char *);
char *	malloc_strndup		(const char *, size_t);
char *	prntdump		(const char *, size_t);
char *	ov_strcpy		(char *, const char *);
size_t	ccspan			(const char *, int);
int	last_char		(const char *);
char *	next_in_comma_list	(char *, char **);
char *	next_in_div_list	(char *, char **, char);
char *	get_userhost		(void);
double  time_to_next_interval	(int interval);
int	charcount		(const char *, char);
void	beep_em			(int);
void	strip_control		(const char *, char *);
const char *strfill		(char, int);
char *	encode			(const char *, size_t);
char *	decode			(const char *);
char *	chomp			(char *);
int 	opento			(const char *, int, off_t);
int	figure_out_address	(const char *, char **, char **, char **);
int	figure_out_domain	(char *, char **, char **, int *);
int	count_char		(const unsigned char *, const unsigned char);
char *	strnrchr		(char *, char, int);
void	mask_digits		(char **);
char *	strpcat			(char *, const char *, ...) __A(2);
char *  strlpcat		(char *, size_t, const char *, ...) __A(3);
u_char *strcpy_nocolorcodes	(u_char *, const u_char *);
u_long	random_number		(u_long);
char *	urlencode		(const char *);
char *	urldecode		(char *, size_t *);
char *	enquote_it		(char *str, size_t len);
char *	dequote_it		(char *str, size_t *len);
const char *	my_strerror	(int, int);
int	slurp_file		(char **buffer, char *filename);
char *	endstr			(char *);
ssize_t searchbuf		(const unsigned char *, size_t, size_t, int);
int	remove_from_comma_list	(char *str, const char *what);
char *	dequote_buffer		(char *str, size_t *len);

void	add_mode_to_str		(char *, size_t, int);
void	remove_mode_from_str	(char *, size_t, int);
void	clear_modes		(char *);
void	update_mode_str		(char *, size_t, const char *);

void	add_mode_to_str		(char *, size_t, int);
void	remove_mode_from_str	(char *, size_t, int);
void	clear_modes		(char *);
void	update_mode_str		(char *, size_t, const char *);

size_t	strlcpy_c		(char *, const char *, size_t, size_t *);
size_t	strlcat_c		(char *, const char *, size_t, size_t *);
char *  strlopencat_c		(char *dest, size_t maxlen, size_t *cluep, ...);
int     is_string_empty 	(const char *str);

char *	malloc_strcpy_c		(char **, const char *, size_t *);
char *	malloc_strcat_c		(char **, const char *, size_t *);
char *	malloc_strdup 		(const char *);
char *	malloc_strdup2 		(const char *, const char *);
char *	malloc_strdup3 		(const char *, const char *, const char *);
char *	malloc_strcat2_c	(char **, const char *, const char *, size_t *);
char *	malloc_strcat_wordlist_c (char **, const char *, const char *,size_t *);
char *	malloc_strcat_word_c    (char **, const char *, const char *,size_t *);
char *	malloc_sprintf 		(char **, const char *, ...) __A(2);

#define malloc_strcpy(x,y) malloc_strcpy_c((x),(y),NULL)
#define malloc_strcat(x,y) malloc_strcat_c((x),(y),NULL)
#define malloc_strcat2(x,y,z) malloc_strcat2_c((x),(y),(z),NULL)
#define malloc_strcat_wordlist(x,y,z) malloc_strcat_wordlist_c((x),(y),(z),NULL)
#define malloc_strcat_word(x,y,z) malloc_strcat_word_c((x),(y),(z),NULL)

char *  universal_next_arg_count (char *, char **, int, int, int, const char *);
#define next_arg(a,b)           universal_next_arg_count((a),(b),1,DWORD_NEVER,0,"\"")
#define next_arg_count(a,b,c)   universal_next_arg_count((a),(b),(c),DWORD_NEVER, 0,"\"")
#define new_next_arg(a,b)       universal_next_arg_count((a),(b),1,DWORD_ALWAYS,1,"\"")
#define new_next_arg_count(a,b) universal_next_arg_count((a),(b),(c),DWORD_ALWAYS,1,"\"")
void    dequoter                (char **, size_t *, int, int, const char *);

#define my_isspace(x) isspace(x)
#define my_isdigit(x) \
	(isdigit(*x) || ((*x == '-' || *x == '+') && isdigit(x[1])))


int vmy_strnicmp(int, char *, ...);

/* Used for the inbound mangling stuff */
#define MANGLE_ESCAPES		1 << 0
#define MANGLE_ANSI_CODES	1 << 1
#define STRIP_COLOR		1 << 2
#define STRIP_REVERSE		1 << 3
#define STRIP_UNDERLINE		1 << 4
#define STRIP_BOLD		1 << 5
#define STRIP_BLINK		1 << 6
#define STRIP_ROM_CHAR		1 << 7
#define STRIP_ND_SPACE		1 << 8
#define STRIP_ALT_CHAR		1 << 9
#define STRIP_ALL_OFF		1 << 10
#define STRIP_OTHER		1 << 11

extern	int	outbound_line_mangler;
extern	int	inbound_line_mangler;
extern	int	logfile_line_mangler;
size_t	mangle_line		(char *, int, size_t);

struct BucketItem {
	const char *name;
	void *stuff;
};
typedef struct BucketItem BucketItem;

struct Bucket {
	int numitems;
	int max;
	BucketItem *list;
};
typedef struct Bucket Bucket;

	Bucket *new_bucket (void);
	void	free_bucket (Bucket **);
	void	add_to_bucket (Bucket *, const char *, void *);

#endif /* _IRCAUX_H_ */
