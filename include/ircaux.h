/*
 * ircaux.h: header file for ircaux.c 
 *
 * Written By Michael Sandrof
 *
 * Copyright(c) 1990 
 *
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 *
 * @(#)$Id: ircaux.h,v 1.53 2003/07/09 21:10:24 jnelson Exp $
 */

#ifndef _IRCAUX_H_
#define _IRCAUX_H_

#include "compat.h"
#include "network.h"

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

#define m_e3cat(x,y,z) m_ec3cat((x),(y),(z),NULL)
#define m_s3cat(x,y,z) m_sc3cat((x),(y),(z),NULL)
#define m_s3cat_s(x,y,z) m_sc3cat_s((x),(y),(z),NULL)
#define m_3cat(x,y,z) m_c3cat((x),(y),(z),NULL)
#define malloc_strcat(x,y) malloc_strcat_c((x),(y),NULL)
#define malloc_strcat_ues(x,y,z) malloc_strcat_ues_c((x),(y),(z),NULL)

extern	int	need_delayed_free;
void	fatal_malloc_check	(void *, const char *, const char *, int);
void *	really_new_malloc 	(size_t, const char *, int);
void *	really_new_free 	(void **, const char *, int);
void *	really_new_realloc 	(void **, size_t, const char *, int);
void	malloc_dump		(const char *);

char *	check_nickname 		(char *, int);
char *	dequote			(char *);
#define next_arg(a,b) next_arg_count((a),(b),1)
char *	next_arg_count 		(char *, char **, int);
char *	new_next_arg		(char *, char **);
char *	new_next_arg_count 	(char *, char **, int);
char *	new_new_next_arg 	(char *, char **, char *);
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
char *	malloc_strcpy 		(char **, const char *);
char *	malloc_strcat_c		(char **, const char *, size_t *);
char *	malloc_str2cpy		(char **, const char *, const char *);
char *	m_sc3cat_s 		(char **, const char *, const char *, size_t *clue);
char *	m_sc3cat 		(char **, const char *, const char *, size_t *clue);
char *	m_c3cat			(char **, const char *, const char *, size_t *clue);
char *	m_ec3cat 		(char **, const char *, const char *, size_t *clue);
char *	m_2dup 			(const char *, const char *);
char *	m_3dup 			(const char *, const char *, const char *);
char *	m_opendup 		(const char *, ...) __A(1);
void	wait_new_free 		(char **);
char *	malloc_sprintf 		(char **, const char *, ...) __A(2);
int	is_number 		(const char *);
int	is_real_number 		(const char *);
char *	my_ctime 		(time_t);
extern	unsigned char stricmp_table[];
int	my_stricmp 		(const unsigned char *, const unsigned char *);
int	my_strnicmp 		(const unsigned char *, const unsigned char *, size_t);
int	scanstr 		(char *, char *);
void	really_free 		(int);
char *	chop 			(char *, size_t);
char *	malloc_strcat_ues_c	(char **, const char *, const char *, size_t *);
char *	strlopencat		(char *, size_t, ...);
ssize_t	stristr 		(const char *, const char *);
ssize_t	rstristr 		(const char *, const char *);
char *	findchar		(char *, int);
FILE *	uzfopen 		(char **, const char *, int);
int	end_strcmp 		(const char *, const char *, size_t);
char*   exec_pipe		(char *, char *, size_t *, char **);
FILE **	open_exec		(char *executable, char **args);
void	panic 			(const char *, ...) __A(1) __N;
int	vt100_decode 		(char);
int	count_ansi		(char *, int);
int	fw_strcmp 		(comp_len_func *, char *, char *);
int	lw_strcmp 		(comp_func *, char *, char *);
int	open_to 		(char *, int, int);
struct metric_time get_metric_time	(double *);
struct metric_time timeval_to_metric	(Timeval *);
Timeval get_time		(Timeval *);
double 	time_diff 		(Timeval, Timeval);
Timeval time_add		(Timeval, Timeval);
Timeval time_subtract		(Timeval, Timeval);
Timeval double_to_timeval 	(double);
const char *	plural 		(int);
double	time_to_next_minute 	(void);
char *	remove_trailing_spaces 	(char *, size_t *cluep);
char *	ltoa 			(long);
char *	ftoa			(double);
char *	strformat 		(char *, const char *, ssize_t, int);
char *	chop_word 		(char *);
char *	skip_spaces		(char *);
int	split_args		(char *, char **to, size_t);
int	splitw 			(char *, char ***);
char *	unsplitw 		(char ***, int);
int	check_val 		(const char *);
char *	strext	 		(const char *, const char *);
char *	strextend 		(char *, char, int);
char *	pullstr 		(char *, char *);
int 	empty 			(const char *);
char *	safe_new_next_arg	(char *, char **);
ssize_t	MatchingBracket 	(const char *, char, char);
int	word_count 		(const char *);
int	parse_number 		(char **);
char *	remove_brackets 	(const char *, const char *, int *);
long	my_atol 		(const char *);
u_long	hashpjw 		(char *, u_long);
char *	m_dupchar 		(int);
off_t	file_size		(const char *);
int	file_exists		(const char *);
int	isdir			(const char *);
int	is_root			(const char *, const char *, int);
size_t	streq			(const char *, const char *);
char *	m_strndup		(const char *, size_t);
char *	prntdump		(const char *, size_t);
char *	ov_strcpy		(char *, const char *);
size_t	ccspan			(const char *, int);
int	last_char		(const char *);
char *	next_in_comma_list	(char *, char **);
char *	next_in_div_list	(char *, char **, char);
char *	get_userhost		(void);
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
char *	malloc_sprintf 		(char **, const char *, ...) __A(2);

#if notyet
#define malloc_strcpy(x,y) malloc_strcpy_c((x),(y),NULL)
#define malloc_strcat(x,y) malloc_strcat_c((x),(y),NULL)
#define m_strdup malloc_strdup
#define malloc_strcat2(x,y,z) malloc_strcat2_c((x),(y),(z),NULL)
#define malloc_strcat_wordlist(x,y,z) malloc_strcat_wordlist_c((x),(y),(z),NULL)
#endif

extern	unsigned char isspace_table[256];
#define my_isspace(x) isspace_table[(unsigned)(unsigned char)(x)]
#define my_isdigit(x) \
	(isdigit(*x) || ((*x == '-' || *x == '+') && isdigit(x[1])))

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

#endif /* _IRCAUX_H_ */
