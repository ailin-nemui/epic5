/*
 * ircaux.h: header file for ircaux.c 
 *
 * Written By Michael Sandrof
 *
 * Copyright(c) 1990 
 *
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 *
 * @(#)$Id: ircaux.h,v 1.31 2002/10/18 21:10:22 jnelson Exp $
 */

#ifndef _IRCAUX_H_
#define _IRCAUX_H_

#define SAFE(x) (((x) && *(x)) ? (x) : empty_string)

typedef int 	comp_len_func 		(char *, char *, int);
typedef int 	comp_func 		(char *, char *);

#define new_malloc(x) really_new_malloc	((x), __FILE__, __LINE__)
#define new_free(x)   really_new_free	((void **)(x), __FILE__, __LINE__)
#define new_realloc(x,y) really_new_realloc ((x), (y), __FILE__, __LINE__)
#define MUST_BE_MALLOCED(x, y) \
		fatal_malloc_check ((void *)(x), (y), __FILE__, __LINE__)
#define RESIZE(x, y, z) new_realloc	((void **)& (x), sizeof(y) * (z))

/*
 * It's really really important that you never use LOCAL_COPY in the actual
 * argument list of a function call, because bad things can happen.  Always
 * do your LOCAL_COPY as a separate step before you call a function.
 */
#define LOCAL_COPY(y) strcpy(alloca(strlen((y)) + 1), y)

#define m_e3cat(x,y,z) m_ec3cat((x),(y),(z),NULL)
#define m_s3cat(x,y,z) m_sc3cat((x),(y),(z),NULL)
#define m_s3cat_s(x,y,z) m_sc3cat_s((x),(y),(z),NULL)
#define m_3cat(x,y,z) m_c3cat((x),(y),(z),NULL)
#define malloc_strcat(x,y) malloc_strcat_c((x),(y),NULL)
#define m_strcat_ues(x,y,z) m_strcat_ues_c((x),(y),(z),NULL)

extern	int	need_delayed_free;
void	fatal_malloc_check	(void *, const char *, char *, int);
void *	really_new_malloc 	(size_t, char *, int);
void *	really_new_free 	(void **, char *, int);
int	debug_new_free		(void **, char *, int);
void *	really_new_realloc 	(void **, size_t, char *, int);
void	malloc_dump		(char *);

char *	check_nickname 		(char *, int);
char *	next_arg 		(char *, char **);
char *	new_next_arg 		(char *, char **);
char *	new_new_next_arg 	(char *, char **, char *);
char *	s_next_arg		(char **);
char *	last_arg 		(char **, size_t *cluep);
int	normalize_filename	(const char *, Filename);
int	expand_twiddle 		(const char *, Filename);
char *	upper 			(char *);
char *	lower 			(char *);
char *	sindex			(char *, char *);
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
char *	m_strdup 		(const char *);
void	wait_new_free 		(char **);
char *	malloc_sprintf 		(char **, const char *, ...) __A(2);
char *	m_sprintf 		(const char *, ...) __A(1);
int	is_number 		(const char *);
int	is_real_number 		(const char *);
char *	my_ctime 		(time_t);
extern	unsigned char stricmp_table[];
int	my_stricmp 		(const unsigned char *, const unsigned char *);
int	my_strnicmp 		(const unsigned char *, const unsigned char *, size_t);
int	scanstr 		(char *, char *);
void	really_free 		(int);
char *	chop 			(char *, size_t);
char *	strmcpy 		(char *, const char *, int);
char *	strmcat 		(char *, const char *, int);
char *	strmcat_ue 		(char *, const char *, int);
char *	m_strcat_ues_c 		(char **, char *, int, size_t *);
char *	strmopencat		(char *, int, ...);
char *	stristr 		(const char *, const char *);
char *	rstristr 		(const char *, const char *);
char *	findchar		(char *, int);
FILE *	uzfopen 		(char **, char *, int);
int	end_strcmp 		(const char *, const char *, int);
char*   exec_pipe		(char *, char *, size_t *, char**);
void	panic 			(char *, ...) __A(1) __N;
int	vt100_decode 		(char);
int	count_ansi		(char *, int);
int	fw_strcmp 		(comp_len_func *, char *, char *);
int	lw_strcmp 		(comp_func *, char *, char *);
int	open_to 		(char *, int, int);
struct timeval get_time 	(struct timeval *);
double 	time_diff 		(struct timeval, struct timeval);
struct timeval time_add		(struct timeval, struct timeval);
struct timeval time_subtract	(struct timeval, struct timeval);
struct timeval double_to_timeval (double);
char *	plural 			(int);
struct timeval	time_to_next_minute 	(void);
char *	remove_trailing_spaces 	(char *, size_t *cluep);
char *	ltoa 			(long);
char *	ftoa			(double);
char *	strformat 		(char *, const char *, int, int);
char *	chop_word 		(char *);
char *	skip_spaces		(char *);
int	split_args		(char *, char **to, size_t);
int	splitw 			(char *, char ***);
char *	unsplitw 		(char ***, int);
int	check_val 		(char *);
char *	strext	 		(const char *, const char *);
char *	strextend 		(char *, char, int);
char *	pullstr 		(char *, char *);
int 	empty 			(const char *);
char *	safe_new_next_arg 	(char *, char **);
char *	MatchingBracket 	(char *, char, char);
int	word_count 		(const char *);
int	parse_number 		(char **);
char *	remove_brackets 	(const char *, const char *, int *);
long	my_atol 		(const char *);
u_long	hashpjw 		(char *, u_long);
char *	m_dupchar 		(int);
char *	strmccat		(char *, char, int);
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
#ifndef HAVE_VSNPRINTF
int	vsnprintf 		(char *, size_t, const char *, va_list);
#endif
#ifndef HAVE_SNPRINTF
int	snprintf 		(char *, size_t, const char *, ...) __A(3);
#endif
char *	next_in_comma_list	(char *, char **);
char *	get_userhost		(void);
int	charcount		(const char *, char);
void	beep_em			(int);
void	strip_control		(const char *, char *);
const char *strfill		(char, int);
char *	encode			(const char *, int);
char *	decode			(const char *);
char *	chomp			(char *);
int 	opento			(const char *, int, off_t);
int	figure_out_address	(char *, char **, char **, char **);
int	figure_out_domain	(char *, char **, char **, int *);
int	count_char		(const unsigned char *, const unsigned char);
char *	strnrchr		(char *, char, int);
void	mask_digits		(char **);
char *	strpcat			(char *, const char *, ...) __A(2);
char *  strmpcat		(char *, size_t, const char *, ...) __A(3);
u_char *strcpy_nocolorcodes	(u_char *, const u_char *);
u_long	random_number		(u_long);
char *	urlencode		(const char *);
char *	urldecode		(char *, size_t *);
const char *	find_forward_quote	(const char *, const char *);
const char *	find_backward_quote	(const char *, const char *);
const char *	my_strerror		(int);

/* From words.c */
#define SOS 		-32767
#define EOS 		 32767
char *		search 			(char *, char **, char *, int);
const char *	real_move_to_abs_word 	(const char *, const char **, int, int);
char *		real_extract 		(char *, int, int, int);
char *		real_extract2 		(const char *, int, int, int);
#define move_to_abs_word(a, b, c)	real_move_to_abs_word(a, b, c, 0);
#define extract(a, b, c)		real_extract(a, b, c, 0)
#define extract2(a, b, c)		real_extract2(a, b, c, 0)
#define extractw(a, b, c)		real_extract(a, b, c, 1)
#define extractw2(a, b, c)		real_extract2(a, b, c, 1)

/* Used for connect_by_number */
#define SERVICE_SERVER 	0
#define SERVICE_CLIENT 	1

/* Used from network.c */
#define V0(x) ((SA *)&(x))
#define FAMILY(x) (V0(x)->sa_family)

#define V4(x) ((ISA *)&(x))
#define V4FAM(x) (V4(x)->sin_family)
#define V4ADDR(x) (V4(x)->sin_addr)
#define V4PORT(x) (V4(x)->sin_port)

#define V6(x) ((ISA6 *)&(x))
#define V6FAM(x) (V6(x)->sin6_family)
#define V6ADDR(x) (V6(x)->sin6_addr)
#define V6PORT(x) (V6(x)->sin6_port)


int	inet_strton		(const char *, const char *, SA *, int);
int	inet_ntostr		(SA *, char *, int, char *, int, int);
char 	*inet_hntop 		(int, const char *, char *, int);
char 	*inet_ptohn 		(int, const char *, char *, int);
char 	*one_to_another 	(int, const char *, char *, int);
int	Accept			(int, SA *, int *);
const char *switch_hostname	(const char *);
int	ip_bindery		(int family, u_short port, SS *storage);
int	client_bind		(SA *, socklen_t);
int	client_connect		(SA *, socklen_t, SA *, socklen_t);
int	connectory		(int, const char *, const char *);
#define GNI_INTEGER 0x4000

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

/* Used from compat.c */
#ifndef HAVE_TPARM
char 	*tparm (const char *, ...);
#endif
#ifndef HAVE_STRTOUL
unsigned long 	strtoul (const char *, char **, int);
#endif
char *	bsd_getenv (const char *);
int	bsd_putenv (const char *);
int	bsd_setenv (const char *, const char *, int);
void	bsd_unsetenv (const char *);
#ifndef HAVE_STRLCPY
size_t	strlcpy (char *, const char *, size_t);
#endif
#ifndef HAVE_STRLCAT
size_t	strlcat (char *, const char *, size_t);
#endif
#ifndef HAVE_VSNPRINTF
int	vsnprintf (char *, size_t, const char *, va_list);
#endif
#ifndef HAVE_SNPRINTF
int	snprintf (char *, size_t, const char *, ...);
#endif
#ifndef HAVE_SETSID
int	setsid (void);
#endif
#ifndef STPCPY_DECLARED
char *	stpcpy (char *, const char *);
#endif

#endif /* _IRCAUX_H_ */
