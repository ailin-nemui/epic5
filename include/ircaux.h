/*
 * ircaux.h: header file for ircaux.c 
 *
 * Written By Michael Sandrof
 *
 * Copyright(c) 1990 
 *
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 *
 * @(#)$Id: ircaux.h,v 1.130 2015/04/11 04:16:33 jnelson Exp $
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
	char *	last_arg 		(char **, size_t *, int);

	int	normalize_filename	(const char *, Filename);
	int	expand_twiddle 		(const char *, Filename);
	char *	upper 			(char *);
	char *	lower 			(char *);
	char *	sindex			(char *, const char *);
	char *	rsindex 		(char *, char *, char *, int);
	const char * cpindex 		(const char *, const char *, int, size_t *);
	ptrdiff_t cpindex2 		(const char *, const char *, int, size_t *, int *);
	const char * rcpindex 		(const char *, const char *, const char *, int, size_t *);
	ptrdiff_t    rcpindex2 		(const char *, const char *, const char *, int, size_t *, int *);

	ssize_t	findchar_quoted		(const char *, int);
	int	path_search 		(const char *, const char *, Filename);
	char *	double_quote 		(const char *, const char *, char *);
	char *	malloc_strcat_c		(char **, const char *, size_t *);
	void	wait_new_free 		(char **);
	char *	malloc_sprintf 		(char **, const char *, ...) __A(2);
	char *	malloc_vsprintf 	(char **, const char *, va_list);
	int	is_number 		(const char *);
	int	is_real_number 		(const char *);
	const char *	my_ctime 	(time_t);
	int     rgb_to_256 		(uint8_t r, uint8_t g, uint8_t b);
	size_t  hex256			(uint8_t x, char **retval);


	int	my_table_strnicmp 	(const char *, const char *, size_t, int);
#define my_table_stricmp(x, y, t) my_table_strnicmp(x, y, UINT_MAX, t)
	int	server_strnicmp		(const char *, const char *, size_t, int);
#define server_stricmp(x, y, s)	server_strnicmp(x, y, UINT_MAX, s)
	int	my_strnicmp 		(const char *, const char *, size_t);
	int	my_stricmp 		(const char *, const char *);
	int     my_strncmp 		(const char *, const char *, size_t);
	int	ascii_strnicmp 		(const char *, const char *, size_t);
	int	ascii_stricmp 		(const char *, const char *);
	int	rfc1459_strnicmp 	(const char *, const char *, size_t);
	int	rfc1459_stricmp 	(const char *, const char *);
	int     alist_stricmp 		(const char *, const char *, size_t);

	char *  uuid4_generate 		(void);
	char *	uuid4_generate_no_dashes	(void);

	void	really_free 		(int);
	char *	chop 			(char *, size_t);
	char *	malloc_strcat_ues_c	(char **, const char *, const char *, size_t *);
	char *	strlopencat		(char *, size_t, ...);
	ssize_t	stristr 		(const char *, const char *);
	ssize_t	rstristr 		(const char *, const char *);
struct 	epic_loadfile *	uzfopen 	(char **, const char *, int, struct stat *);
	int	end_strcmp 		(const char *, const char *, size_t);
	char*   exec_pipe		(const char *, char *, size_t *, char *const *);
	FILE **	open_exec		(const char *executable, char * const *args);
	void	panic 			(int, const char *, ...) __A(2) __N;
	int	fw_strcmp 		(comp_len_func *, char *, char *);
	int	lw_strcmp 		(comp_func *, char *, char *);
	int	open_to 		(char *, int, int);
struct 	metric_time get_metric_time	(double *);
struct 	metric_time timeval_to_metric	(const Timeval *);
	Timeval get_time		(Timeval *);
	double 	time_diff 		(const Timeval, const Timeval);
	Timeval time_add		(const Timeval, const Timeval);
	Timeval time_subtract		(const Timeval, const Timeval);
	Timeval double_to_timeval 	(double);
	double  time_since_startup 	(Timeval t);
const 	char *	plural 			(int);
	double	time_to_next_minute 	(void);
	char *	remove_trailing_spaces 	(char *, size_t *cluep);
	char *	forcibly_remove_trailing_spaces (char *, size_t *);
#define ltoa my_ltoa
const 	char *	my_ltoa 		(long);
const 	char *	intmaxtoa 		(intmax_t);
const 	char *	ftoa			(double);

	char *	chop_word 		(char *);
	char *	skip_spaces		(char *);
	int	split_args		(char *, char **to, size_t);
	int	splitw 			(char *, char ***, int);
	char *	unsplitw 		(char ***, int, int);
	int	new_split_string 	(char *, char ***, int);
	int	split_string 		(char *, char ***, char);
	int	check_val 		(const char *);
	char *	strext	 		(const char *, const char *);
	char *	strextend 		(char *, char, int);
	char *	pullstr 		(char *, char *);
	int 	empty 			(const char *);
	char *	safe_new_next_arg	(char *, char **);
	char *	safe_next_arg		(char *, char **);
	ssize_t	MatchingBracket 	(const char *, char, char);
	int	parse_number 		(char **);
	char *	remove_brackets 	(const char *, const char *);
	long	my_atol 		(const char *);
	char *	malloc_dupchar 		(int);
	off_t	file_size		(const char *);
	int	file_exists		(const char *);
	int	isdir			(const char *);
	int	isdir2			(const char *, const void * const);
	int	is_root			(const char *, const char *, int);
	size_t	streq			(const char *, const char *);
	char *	malloc_strndup		(const char *, size_t);
	char *	prntdump		(const char *, size_t);
	char *	ov_strcpy		(char *, const char *);
	size_t	ccspan			(const char *, int);
	int	last_char		(const char *);
	char *	next_in_comma_list	(char *, char **);
	char *	next_in_div_list	(char *, char **, int);
	char *	get_userhost		(void);
	double  time_to_next_interval	(int interval);
	int	charcount		(const char *, char);
	void	beep_em			(int);
	void	strip_control		(const char *, char *);
const 	char *	strfill			(char, int);
	char *	chomp			(char *);
	int 	opento			(const char *, int, off_t);
	int	figure_out_address	(const char *, char **, char **, char **);
	int	figure_out_domain	(char *, char **, char **, int *);
	int	count_char		(const char *, const char);
	char *	strnrchr		(char *, char, int);
	void	mask_digits		(char **);
	char *	strpcat			(char *, const char *, ...) __A(2);
	char *  strlpcat		(char *, size_t, const char *, ...) __A(3);
	unsigned long	random_number	(unsigned long);

	char *	endstr			(char *);
	ssize_t searchbuf		(const char *, size_t, size_t, int);
	int	remove_from_comma_list	(char *str, const char *what);
	char *	dequote_buffer		(char *str, size_t *len);
	char *	fix_string_width	(const char *, int, int, size_t, int);
	int     check_xdigit 		(char digit);

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
	char *	malloc_strcat3_c	(char **, const char *, const char *, const char *, size_t *);
	char *	malloc_strcat_wordlist_c (char **, const char *, const char *,size_t *);
	char *	malloc_strcat_word_c    (char **, const char *, const char *, int, size_t *);
	char *	malloc_sprintf 		(char **, const char *, ...) __A(2);
	char *  malloc_vsprintf		(char **ptr, const char *format, va_list args);

#define malloc_strcpy(x,y) malloc_strcpy_c((x),(y),NULL)
#define malloc_strcat(x,y) malloc_strcat_c((x),(y),NULL)
#define malloc_strcat2(x,y,z) malloc_strcat2_c((x),(y),(z),NULL)
#define malloc_strcat_wordlist(x,y,z) malloc_strcat_wordlist_c((x),(y),(z),NULL)
#define malloc_strcat_word(x,y,z,a) malloc_strcat_word_c((x),(y),(z),(a),NULL)

	char *  universal_next_arg_count (char *, char **, int, int, int, const char *);
	void    dequoter                (char **, size_t *, int, int, const char *);

#define my_isspace(x) isspace(x)
#define my_isdigit(x) \
	(isdigit(*x) || ((*x == '-' || *x == '+') && isdigit(x[1])))

	int	vmy_strnicmp		(size_t, char *, ...);
	char *	substitute_string	(const char *, const char *, const char *, int, int);

/* ---------------- */
/* Used for the inbound mangling stuff */
#define MANGLE_ESCAPES		(1 << 0)
#define NORMALIZE		(1 << 1)
#define STRIP_COLOR		(1 << 2)
#define STRIP_REVERSE		(1 << 3)
#define STRIP_UNDERLINE		(1 << 4)
#define STRIP_BOLD		(1 << 5)
#define STRIP_BLINK		(1 << 6)
#define STRIP_ND_SPACE		(1 << 7)
#define STRIP_ALT_CHAR		(1 << 8)
#define STRIP_ALL_OFF		(1 << 9)
#define STRIP_UNPRINTABLE	(1 << 10)
#define STRIP_OTHER		(1 << 11)
#define STRIP_ITALIC		(1 << 12)

extern	int	outbound_line_mangler;
extern	int	inbound_line_mangler;
extern	int	logfile_line_mangler;

/* ---------------- */
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

	Bucket *new_bucket 		(void);
	void	free_bucket 		(Bucket **);
	void	add_to_bucket 		(Bucket *, const char *, void *);

/* ---------------- */
/* 
 * Theoretically, all these macros are independant because the return value of
 * INT*FUNC is typecasted back to INTTYPE.  One caveat is that INT2STR
 * must return a malloc'd string.
 */
#define STR2INT(x) (strtoimax(x, NULL, 10))
#define INT2STR(x) (malloc_sprintf(NULL, INTMAX_FORMAT, (intmax_t)(x)))
#define STRNUM(x) (strtoimax(x, NULL, 10))
#define NUMSTR(x) (intmaxtoa((intmax_t)(x)))

/********************/
	void	init_transforms 	(void);
	size_t	transform_string 	(int, int, const char *, const char *, size_t, char *, size_t);
	int	lookup_transform 	(const char *, int *, int *, int *);
	char *	valid_transforms 	(void);
	char *	transform_string_dyn 	(const char *, const char *, size_t, size_t *);

extern	int	NONE_xform;
extern	int	URL_xform;
extern	int	ENC_xform;
extern	int	B64_xform;
extern	int	FISH64_xform;
extern	int	CTCP_xform;
extern	int	SHA256_xform;
#define XFORM_ENCODE 1
#define XFORM_DECODE 0

extern int 	num_code_points (const char *);

#ifdef HAVE_ICONV
extern struct Iconv_stuff {
	char *stuff;
	iconv_t forward; /* I dunno  */
	iconv_t reverse; /* Alright! */
} **iconv_list;
extern ssize_t 	iconv_list_size;
	int 	my_iconv_open 		(iconv_t *, iconv_t *, const char *);
#endif

	int	recode_with_iconv 	(const char *, const char *, char **, size_t *);
	int     recode_with_iconv_t 	(iconv_t, char **, size_t *);

/***********************/
	int	codepoint_numcolumns 	(int);
	int	next_code_point2 	(const char *, ptrdiff_t *, int);
	int	partial_code_point	(const char *);
	int	quick_display_column_count 	(const char *);
	int	input_column_count 	(const char *);
	int	ucs_to_utf8 		(uint32_t, char *, size_t);
	int	grab_codepoint 		(const char *x);
	int     quick_code_point_count	(const char *str);
	int     previous_code_point2	(const char *, const char *, ptrdiff_t *);
	int	quick_code_point_index	(const char *, const char *);
	int     count_initial_codepoints (const char *, const char *);

	int	strext2			(char **, char *, size_t , size_t);
	int     invalid_utf8str 	(char *);
	int     is_iso2022_jp 		(const char *);
	void    create_utf8_locale 	(void);
	int	mkupper_l		(int);
	int	mklower_l		(int);
	char *  cp437_to_utf8 	(const char *, size_t, size_t *);

/******* recode.c ******/
	void	init_recodings		(void);
const 	char *	find_recoding		(const char *, iconv_t *, iconv_t *);
	char *	recode_message		(const char *, const char *, const char *, const char **, int);
	int    	ucs_to_console 		(uint32_t, char *, size_t);
	BUILT_IN_COMMAND(encoding);
const 	char *	outbound_recode 	(const char *, int, const char *, char **);
const 	char *	inbound_recode 		(const char *, int, const char *, const char *, char **);
	char *	function_encodingctl 	(char *);

#endif /* _IRCAUX_H_ */

