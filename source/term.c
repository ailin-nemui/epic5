/*
 * term.c -- termios and (termcap || terminfo) handlers
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1998 J. Kean Johnston, used with permission.
 * Copyright 1995, 2008 EPIC Software Labs 
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
/*
 * Ben Winslow deserves specific praise for his fine work adding 
 * terminfo support to EPIC.
 */
#define __need_term_h__
#define __need_putchar_x__
#define __need_term_flush__
#include "irc.h"
#include "ircaux.h"
#include "vars.h"
#include "termx.h"
#include "window.h"
#include "screen.h"
#include "output.h"
#include "newio.h"

/*
 * If "HAVE_TERMINFO" is #define'd then we will use terminfo type function
 * calls.  If it is #undef'd, then we will use the more traditional termcap
 * type function calls.  If you can possibly get away with it, use terminfo
 * instead of termcap, as it is muck more likely your terminal descriptions
 * will be closer to reality.
 */


#ifdef HAVE_TERMIOS_H
# include <termios.h>
#else
# ifdef HAVE_SYS_TERMIOS_H
#  include <sys/termios.h>
# endif
#endif
#include <sys/ioctl.h>

volatile	int		need_redraw;
static	int		tty_des;		/* descriptor for the tty */
static	struct	termios	oldb, newb;
	char		my_PC;

/*
 * XXX -- Supporting this correctly is a hack, but it's not my fault.
 * 
 * Configure has already determined if <term.h> can be used to get
 * prototypes for these functions.  It has also determined whether it is 
 * X/OPEN curses, and therefore requires us to include <termio.h> and 
 * <curses.h> before including <term.h>.
 *
 * For those systems that do not have a usable <term.h>, we will set forth
 * some empty decl prototypes for the functions, just to establish the 
 * return value.  All systems have either the terminfo functions or the 
 * termcap functions, but not all systems have tparm(), so to avoid clashing
 * with the my_tparm() decl in "compat.h", we only decl tparm() if we are
 * using the system's (and not our) tparm().
 */
#if defined(HAVE_TERM_H) && !defined(DONT_USE_TERM_H)
/*
# if defined(TERM_H_REQUIRES_CURSES_H)
#  include <termio.h>
#  include <curses.h>
# endif
# include <term.h>
*/
#else
# ifdef HAVE_TERMINFO
	extern  int     setupterm();
	extern  char *  tigetstr();
	extern  int     tigetnum();
	extern  int     tigetflag();
# else
	extern  int     tgetent();
	extern  char *  tgetstr();
	extern  int     tgetnum();
	extern  int     tgetflag();
# endif
# ifdef HAVE_TPARM
	extern  char *  tparm();
# endif
#endif

/*
 * We use macro wrappers to hide the difference between terminfo and termcap.
 */
#ifdef HAVE_TERMINFO
# define Tgetstr(x, y)	tigetstr(x.iname)
# define Tgetnum(x)	tigetnum(x.iname);
# define Tgetflag(x)	tigetflag(x.iname);
#else
# define Tgetstr(x, y)	tgetstr(x.tname, &y)
# define Tgetnum(x)	tgetnum(x.tname)
# define Tgetflag(x)	tgetflag(x.tname)
#endif

/*
 * And for the final inanity, X/OPEN curses uses
 *     char *tparm (char *, int, int, int, int, int, int, int, int, int);
 * but everyone else (including ncurses and solaris) uses
 *     char *tparm (const char *, ...);
 * and there's no way to cover those two arglists with one prototype, so
 * we're left with these idiot wrapper functions.
 */
static char *tparm1 (const char *str, int l1)
{
	return tparm(str, l1, 0, 0, 0, 0, 0, 0, 0, 0);
}

static char *tparm2 (const char *str, int l1, int l2)
{
	return tparm(str, l1, l2, 0, 0, 0, 0, 0, 0, 0);
}

static char *tparm4 (const char *str, int l1, int l2, int l3, int l4)
{
	return tparm(str, l1, l2, l3, l4, 0, 0, 0, 0, 0);
}

/*
 * The old code assumed termcap. termcap is almost always present, but on
 * many systems that have both termcap and terminfo, termcap is deprecated
 * and its databases often out of date. Configure will try to use terminfo
 * if at all possible. We define here a mapping between the termcap / terminfo
 * names, and where we store the information.
 */
#define CAP_TYPE_BOOL   0
#define CAP_TYPE_INT    1
#define CAP_TYPE_STR    2

typedef struct cap2info
{
	const char *	longname;
	const char *	iname;
	const char *	tname;
	int 		type;
	void *		ptr;
} cap2info;

struct	my_term	TI;

static const cap2info tcaps[] =
{
	{ "auto_left_margin",		"bw",		"bw",	CAP_TYPE_BOOL,	(void *)&TI.TI_bw },
	{ "auto_right_margin",		"am",		"am",	CAP_TYPE_BOOL,	(void *)&TI.TI_am },
	{ "no_esc_ctlc",		"xsb",		"xb",	CAP_TYPE_BOOL,	(void *)&TI.TI_xsb },
	{ "ceol_standout_glitch",	"xhp",		"xs",	CAP_TYPE_BOOL,	(void *)&TI.TI_xhp },
	{ "eat_newline_glitch",		"xenl",		"xn",	CAP_TYPE_BOOL,	(void *)&TI.TI_xenl },
	{ "erase_overstrike",		"eo",		"eo",	CAP_TYPE_BOOL,	(void *)&TI.TI_eo },
	{ "generic_type",		"gn",		"gn",	CAP_TYPE_BOOL,	(void *)&TI.TI_gn },
	{ "hard_copy",			"hc",		"hc",	CAP_TYPE_BOOL,	(void *)&TI.TI_hc },
	{ "has_meta_key",		"km",		"km",	CAP_TYPE_BOOL,	(void *)&TI.TI_km },
	{ "has_status_line",		"hs",		"hs",	CAP_TYPE_BOOL,	(void *)&TI.TI_hs },
	{ "insert_null_glitch",		"in",		"in",	CAP_TYPE_BOOL,	(void *)&TI.TI_in },
	{ "memory_above",		"da",		"da",	CAP_TYPE_BOOL,	(void *)&TI.TI_da },
	{ "memory_below",		"db",		"db",	CAP_TYPE_BOOL,	(void *)&TI.TI_db },
	{ "move_insert_mode",		"mir",		"mi",	CAP_TYPE_BOOL,	(void *)&TI.TI_mir },
	{ "move_standout_mode",		"msgr",		"ms",	CAP_TYPE_BOOL,	(void *)&TI.TI_msgr },
	{ "over_strike",		"os",		"os",	CAP_TYPE_BOOL,	(void *)&TI.TI_os },
	{ "status_line_esc_ok",		"eslok",	"es",	CAP_TYPE_BOOL,	(void *)&TI.TI_eslok },
	{ "dest_tabs_magic_smso",	"xt",		"xt",	CAP_TYPE_BOOL,	(void *)&TI.TI_xt },
	{ "tilde_glitch",		"hz",		"hz",	CAP_TYPE_BOOL,	(void *)&TI.TI_hz },
	{ "transparent_underline",	"ul",		"ul",	CAP_TYPE_BOOL,	(void *)&TI.TI_ul },
	{ "xon_xoff",			"xon",		"xo",	CAP_TYPE_BOOL,	(void *)&TI.TI_xon },
	{ "needs_xon_xoff",		"nxon",		"nx",	CAP_TYPE_BOOL,	(void *)&TI.TI_nxon },
	{ "prtr_silent",		"mc5i",		"5i",	CAP_TYPE_BOOL,	(void *)&TI.TI_mc5i },
	{ "hard_cursor",		"chts",		"HC",	CAP_TYPE_BOOL,	(void *)&TI.TI_chts },
	{ "non_rev_rmcup",		"nrrmc",	"NR",	CAP_TYPE_BOOL,	(void *)&TI.TI_nrrmc },
	{ "no_pad_char",		"npc",		"NP",	CAP_TYPE_BOOL,	(void *)&TI.TI_npc },
	{ "non_dest_scroll_region",	"ndscr",	"ND",	CAP_TYPE_BOOL,	(void *)&TI.TI_ndscr },
	{ "can_change",			"ccc",		"cc",	CAP_TYPE_BOOL,	(void *)&TI.TI_ccc },
	{ "back_color_erase",		"bce",		"ut",	CAP_TYPE_BOOL,	(void *)&TI.TI_bce },
	{ "hue_lightness_saturation",	"hls",		"hl",	CAP_TYPE_BOOL,	(void *)&TI.TI_hls },
	{ "col_addr_glitch",		"xhpa",		"YA",	CAP_TYPE_BOOL,	(void *)&TI.TI_xhpa },
	{ "cr_cancels_micro_mode",	"crxm",		"YB",	CAP_TYPE_BOOL,	(void *)&TI.TI_crxm },
	{ "has_print_wheel",		"daisy",	"YC",	CAP_TYPE_BOOL,	(void *)&TI.TI_daisy },
	{ "row_addr_glitch",		"xvpa",		"YD",	CAP_TYPE_BOOL,	(void *)&TI.TI_xvpa },
	{ "semi_auto_right_margin",	"sam",		"YE",	CAP_TYPE_BOOL,	(void *)&TI.TI_sam },
	{ "cpi_changes_res",		"cpix",		"YF",	CAP_TYPE_BOOL,	(void *)&TI.TI_cpix },
	{ "lpi_changes_res",		"lpix",		"YG",	CAP_TYPE_BOOL,	(void *)&TI.TI_lpix },
	{ "columns",			"cols",		"co",	CAP_TYPE_INT,	(void *)&TI.TI_cols },
	{ "init_tabs",			"it",		"it",	CAP_TYPE_INT,	(void *)&TI.TI_it },
	{ "lines",			"lines",	"li",	CAP_TYPE_INT,	(void *)&TI.TI_lines },
	{ "lines_of_memory",		"lm",		"lm",	CAP_TYPE_INT,	(void *)&TI.TI_lm },
	{ "magic_cookie_glitch",	"xmc",		"sg",	CAP_TYPE_INT,	(void *)&TI.TI_xmc },
	{ "padding_baud_rate",		"pb",		"pb",	CAP_TYPE_INT,	(void *)&TI.TI_pb },
	{ "virtual_terminal",		"vt",		"vt",	CAP_TYPE_INT,	(void *)&TI.TI_vt },
	{ "width_status_line",		"wsl",		"ws",	CAP_TYPE_INT,	(void *)&TI.TI_wsl },
	{ "num_labels",			"nlab",		"Nl",	CAP_TYPE_INT,	(void *)&TI.TI_nlab },
	{ "label_height",		"lh",		"lh",	CAP_TYPE_INT,	(void *)&TI.TI_lh },
	{ "label_width",		"lw",		"lw",	CAP_TYPE_INT,	(void *)&TI.TI_lw },
	{ "max_attributes",		"ma",		"ma",	CAP_TYPE_INT,	(void *)&TI.TI_ma },
	{ "maximum_windows",		"wnum",		"MW",	CAP_TYPE_INT,	(void *)&TI.TI_wnum },
	{ "max_colors",			"colors",	"Co",	CAP_TYPE_INT,	(void *)&TI.TI_colors },
	{ "max_pairs",			"pairs",	"pa",	CAP_TYPE_INT,	(void *)&TI.TI_pairs },
	{ "no_color_video",		"ncv",		"NC",	CAP_TYPE_INT,	(void *)&TI.TI_ncv },
	{ "buffer_capacity",		"bufsz",	"Ya",	CAP_TYPE_INT,	(void *)&TI.TI_bufsz },
	{ "dot_vert_spacing",		"spinv",	"Yb",	CAP_TYPE_INT,	(void *)&TI.TI_spinv },
	{ "dot_horz_spacing",		"spinh",	"Yc",	CAP_TYPE_INT,	(void *)&TI.TI_spinh },
	{ "max_micro_address",		"maddr",	"Yd",	CAP_TYPE_INT,	(void *)&TI.TI_maddr },
	{ "max_micro_jump",		"mjump",	"Ye",	CAP_TYPE_INT,	(void *)&TI.TI_mjump },
	{ "micro_col_size",		"mcs",		"Yf",	CAP_TYPE_INT,	(void *)&TI.TI_mcs },
	{ "micro_line_size",		"mls",		"Yg",	CAP_TYPE_INT,	(void *)&TI.TI_mls },
	{ "number_of_pins",		"npins",	"Yh",	CAP_TYPE_INT,	(void *)&TI.TI_npins },
	{ "output_res_char",		"orc",		"Yi",	CAP_TYPE_INT,	(void *)&TI.TI_orc },
	{ "output_res_line",		"orl",		"Yj",	CAP_TYPE_INT,	(void *)&TI.TI_orl },
	{ "output_res_horz_inch",	"orhi",		"Yk",	CAP_TYPE_INT,	(void *)&TI.TI_orhi },
	{ "output_res_vert_inch",	"orvi",		"Yl",	CAP_TYPE_INT,	(void *)&TI.TI_orvi },
	{ "print_rate",			"cps",		"Ym",	CAP_TYPE_INT,	(void *)&TI.TI_cps },
	{ "wide_char_size",		"widcs",	"Yn",	CAP_TYPE_INT,	(void *)&TI.TI_widcs },
	{ "buttons",			"btns",		"BT",	CAP_TYPE_INT,	(void *)&TI.TI_btns },
	{ "bit_image_entwining",	"bitwin",	"Yo",	CAP_TYPE_INT,	(void *)&TI.TI_bitwin },
	{ "bit_image_type",		"bitype",	"Yp",	CAP_TYPE_INT,	(void *)&TI.TI_bitype },
	{ "back_tab",			"cbt",		"bt",	CAP_TYPE_STR,	&TI.TI_cbt },
	{ "bell",			"bel",		"bl",	CAP_TYPE_STR,	&TI.TI_bel },
	{ "carriage_return",		"cr",		"cr",	CAP_TYPE_STR,	&TI.TI_cr },
	{ "change_scroll_region",	"csr",		"cs",	CAP_TYPE_STR,	&TI.TI_csr },
	{ "clear_all_tabs",		"tbc",		"ct",	CAP_TYPE_STR,	&TI.TI_tbc },
	{ "clear_screen",		"clear",	"cl",	CAP_TYPE_STR,	&TI.TI_clear },
	{ "clr_eol",			"el",		"ce",	CAP_TYPE_STR,	&TI.TI_el },
	{ "clr_eos",			"ed",		"cd",	CAP_TYPE_STR,	&TI.TI_ed },
	{ "column_address",		"hpa",		"ch",	CAP_TYPE_STR,	&TI.TI_hpa },
	{ "command_character",		"cmdch",	"CC",	CAP_TYPE_STR,	&TI.TI_cmdch },
	{ "cursor_address",		"cup",		"cm",	CAP_TYPE_STR,	&TI.TI_cup },
	{ "cursor_down",		"cud1",		"do",	CAP_TYPE_STR,	&TI.TI_cud1 },
	{ "cursor_home",		"home",		"ho",	CAP_TYPE_STR,	&TI.TI_home },
	{ "cursor_invisible",		"civis",	"vi",	CAP_TYPE_STR,	&TI.TI_civis },
	{ "cursor_left",		"cub1",		"le",	CAP_TYPE_STR,	&TI.TI_cub1 },
	{ "cursor_mem_address",		"mrcup",	"CM",	CAP_TYPE_STR,	&TI.TI_mrcup },
	{ "cursor_normal",		"cnorm",	"ve",	CAP_TYPE_STR,	&TI.TI_cnorm },
	{ "cursor_right",		"cuf1",		"nd",	CAP_TYPE_STR,	&TI.TI_cuf1 },
	{ "cursor_to_ll",		"ll",		"ll",	CAP_TYPE_STR,	&TI.TI_ll },
	{ "cursor_up",			"cuu1",		"up",	CAP_TYPE_STR,	&TI.TI_cuu1 },
	{ "cursor_visible",		"cvvis",	"vs",	CAP_TYPE_STR,	&TI.TI_cvvis },
	{ "delete_character",		"dch1",		"dc",	CAP_TYPE_STR,	&TI.TI_dch1 },
	{ "delete_line",		"dl1",		"dl",	CAP_TYPE_STR,	&TI.TI_dl1 },
	{ "dis_status_line",		"dsl",		"ds",	CAP_TYPE_STR,	&TI.TI_dsl },
	{ "down_half_line",		"hd",		"hd",	CAP_TYPE_STR,	&TI.TI_hd },
	{ "enter_alt_charset_mode",	"smacs",	"as",	CAP_TYPE_STR,	&TI.TI_smacs },
	{ "enter_blink_mode",		"blink",	"mb",	CAP_TYPE_STR,	&TI.TI_blink },
	{ "enter_bold_mode",		"bold",		"md",	CAP_TYPE_STR,	&TI.TI_bold },
	{ "enter_ca_mode",		"smcup",	"ti",	CAP_TYPE_STR,	&TI.TI_smcup },
	{ "enter_delete_mode",		"smdc",		"dm",	CAP_TYPE_STR,	&TI.TI_smdc },
	{ "enter_dim_mode",		"dim",		"mh",	CAP_TYPE_STR,	&TI.TI_dim },
	{ "enter_insert_mode",		"smir",		"im",	CAP_TYPE_STR,	&TI.TI_smir },
	{ "enter_secure_mode",		"invis",	"mk",	CAP_TYPE_STR,	&TI.TI_invis },
	{ "enter_protected_mode",	"prot",		"mp",	CAP_TYPE_STR,	&TI.TI_prot },
	{ "enter_reverse_mode",		"rev",		"mr",	CAP_TYPE_STR,	&TI.TI_rev },
	{ "enter_standout_mode",	"smso",		"so",	CAP_TYPE_STR,	&TI.TI_smso },
	{ "enter_underline_mode",	"smul",		"us",	CAP_TYPE_STR,	&TI.TI_smul },
	{ "erase_chars",		"ech",		"ec",	CAP_TYPE_STR,	&TI.TI_ech },
	{ "exit_alt_charset_mode",	"rmacs",	"ae",	CAP_TYPE_STR,	&TI.TI_rmacs },
	{ "exit_attribute_mode",	"sgr0",		"me",	CAP_TYPE_STR,	&TI.TI_sgr0 },
	{ "exit_ca_mode",		"rmcup",	"te",	CAP_TYPE_STR,	&TI.TI_rmcup },
	{ "exit_delete_mode",		"rmdc",		"ed",	CAP_TYPE_STR,	&TI.TI_rmdc },
	{ "exit_insert_mode",		"rmir",		"ei",	CAP_TYPE_STR,	&TI.TI_rmir },
	{ "exit_standout_mode",		"rmso",		"se",	CAP_TYPE_STR,	&TI.TI_rmso },
	{ "exit_underline_mode",	"rmul",		"ue",	CAP_TYPE_STR,	&TI.TI_rmul },
	{ "flash_screen",		"flash",	"vb",	CAP_TYPE_STR,	&TI.TI_flash },
	{ "form_feed",			"ff",		"ff",	CAP_TYPE_STR,	&TI.TI_ff },
	{ "from_status_line",		"fsl",		"fs",	CAP_TYPE_STR,	&TI.TI_fsl },
	{ "init_1string",		"is1",		"i1",	CAP_TYPE_STR,	&TI.TI_is1 },
	{ "init_2string",		"is2",		"is",	CAP_TYPE_STR,	&TI.TI_is2 },
	{ "init_3string",		"is3",		"i3",	CAP_TYPE_STR,	&TI.TI_is3 },
	{ "init_file",			"if",		"if",	CAP_TYPE_STR,	&TI.TI_if },
	{ "insert_character",		"ich1",		"ic",	CAP_TYPE_STR,	&TI.TI_ich1 },
	{ "insert_line",		"il1",		"al",	CAP_TYPE_STR,	&TI.TI_il1 },
	{ "insert_padding",		"ip",		"ip",	CAP_TYPE_STR,	&TI.TI_ip },
	{ "key_backspace",		"kbs",		"kb",	CAP_TYPE_STR,	&TI.TI_kbs },
	{ "key_catab",			"ktbc",		"ka",	CAP_TYPE_STR,	&TI.TI_ktbc },
	{ "key_clear",			"kclr",		"kC",	CAP_TYPE_STR,	&TI.TI_kclr },
	{ "key_ctab",			"kctab",	"kt",	CAP_TYPE_STR,	&TI.TI_kctab },
	{ "key_dc",			"kdch1",	"kD",	CAP_TYPE_STR,	&TI.TI_kdch1 },
	{ "key_dl",			"kdl1",		"kL",	CAP_TYPE_STR,	&TI.TI_kdl1 },
	{ "key_down",			"kcud1",	"kd",	CAP_TYPE_STR,	&TI.TI_kcud1 },
	{ "key_eic",			"krmir",	"kM",	CAP_TYPE_STR,	&TI.TI_krmir },
	{ "key_eol",			"kel",		"kE",	CAP_TYPE_STR,	&TI.TI_kel },
	{ "key_eos",			"ked",		"kS",	CAP_TYPE_STR,	&TI.TI_ked },
	{ "key_f0",			"kf0",		"k0",	CAP_TYPE_STR,	&TI.TI_kf0 },
	{ "key_f1",			"kf1",		"k1",	CAP_TYPE_STR,	&TI.TI_kf1 },
	{ "key_f10",			"kf10",		"k;",	CAP_TYPE_STR,	&TI.TI_kf10 },
	{ "key_f2",			"kf2",		"k2",	CAP_TYPE_STR,	&TI.TI_kf2 },
	{ "key_f3",			"kf3",		"k3",	CAP_TYPE_STR,	&TI.TI_kf3 },
	{ "key_f4",			"kf4",		"k4",	CAP_TYPE_STR,	&TI.TI_kf4 },
	{ "key_f5",			"kf5",		"k5",	CAP_TYPE_STR,	&TI.TI_kf5 },
	{ "key_f6",			"kf6",		"k6",	CAP_TYPE_STR,	&TI.TI_kf6 },
	{ "key_f7",			"kf7",		"k7",	CAP_TYPE_STR,	&TI.TI_kf7 },
	{ "key_f8",			"kf8",		"k8",	CAP_TYPE_STR,	&TI.TI_kf8 },
	{ "key_f9",			"kf9",		"k9",	CAP_TYPE_STR,	&TI.TI_kf9 },
	{ "key_home",			"khome",	"kh",	CAP_TYPE_STR,	&TI.TI_khome },
	{ "key_ic",			"kich1",	"kI",	CAP_TYPE_STR,	&TI.TI_kich1 },
	{ "key_il",			"kil1",		"kA",	CAP_TYPE_STR,	&TI.TI_kil1 },
	{ "key_left",			"kcub1",	"kl",	CAP_TYPE_STR,	&TI.TI_kcub1 },
	{ "key_ll",			"kll",		"kH",	CAP_TYPE_STR,	&TI.TI_kll },
	{ "key_npage",			"knp",		"kN",	CAP_TYPE_STR,	&TI.TI_knp },
	{ "key_ppage",			"kpp",		"kP",	CAP_TYPE_STR,	&TI.TI_kpp },
	{ "key_right",			"kcuf1",	"kr",	CAP_TYPE_STR,	&TI.TI_kcuf1 },
	{ "key_sf",			"kind",		"kF",	CAP_TYPE_STR,	&TI.TI_kind },
	{ "key_sr",			"kri",		"kR",	CAP_TYPE_STR,	&TI.TI_kri },
	{ "key_stab",			"khts",		"kT",	CAP_TYPE_STR,	&TI.TI_khts },
	{ "key_up",			"kcuu1",	"ku",	CAP_TYPE_STR,	&TI.TI_kcuu1 },
	{ "keypad_local",		"rmkx",		"ke",	CAP_TYPE_STR,	&TI.TI_rmkx },
	{ "keypad_xmit",		"smkx",		"ks",	CAP_TYPE_STR,	&TI.TI_smkx },
	{ "lab_f0",			"lf0",		"l0",	CAP_TYPE_STR,	&TI.TI_lf0 },
	{ "lab_f1",			"lf1",		"l1",	CAP_TYPE_STR,	&TI.TI_lf1 },
	{ "lab_f10",			"lf10",		"la",	CAP_TYPE_STR,	&TI.TI_lf10 },
	{ "lab_f2",			"lf2",		"l2",	CAP_TYPE_STR,	&TI.TI_lf2 },
	{ "lab_f3",			"lf3",		"l3",	CAP_TYPE_STR,	&TI.TI_lf3 },
	{ "lab_f4",			"lf4",		"l4",	CAP_TYPE_STR,	&TI.TI_lf4 },
	{ "lab_f5",			"lf5",		"l5",	CAP_TYPE_STR,	&TI.TI_lf5 },
	{ "lab_f6",			"lf6",		"l6",	CAP_TYPE_STR,	&TI.TI_lf6 },
	{ "lab_f7",			"lf7",		"l7",	CAP_TYPE_STR,	&TI.TI_lf7 },
	{ "lab_f8",			"lf8",		"l8",	CAP_TYPE_STR,	&TI.TI_lf8 },
	{ "lab_f9",			"lf9",		"l9",	CAP_TYPE_STR,	&TI.TI_lf9 },
	{ "meta_off",			"rmm",		"mo",	CAP_TYPE_STR,	&TI.TI_rmm },
	{ "meta_on",			"smm",		"mm",	CAP_TYPE_STR,	&TI.TI_smm },
	{ "newline",			"nel",		"nw",	CAP_TYPE_STR,	&TI.TI_nel },
	{ "pad_char",			"pad",		"pc",	CAP_TYPE_STR,	&TI.TI_pad },
	{ "parm_dch",			"dch",		"DC",	CAP_TYPE_STR,	&TI.TI_dch },
	{ "parm_delete_line",		"dl",		"DL",	CAP_TYPE_STR,	&TI.TI_dl },
	{ "parm_down_cursor",		"cud",		"DO",	CAP_TYPE_STR,	&TI.TI_cud },
	{ "parm_ich",			"ich",		"IC",	CAP_TYPE_STR,	&TI.TI_ich },
	{ "parm_index",			"indn",		"SF",	CAP_TYPE_STR,	&TI.TI_indn },
	{ "parm_insert_line",		"il",		"AL",	CAP_TYPE_STR,	&TI.TI_il },
	{ "parm_left_cursor",		"cub",		"LE",	CAP_TYPE_STR,	&TI.TI_cub },
	{ "parm_right_cursor",		"cuf",		"RI",	CAP_TYPE_STR,	&TI.TI_cuf },
	{ "parm_rindex",		"rin",		"SR",	CAP_TYPE_STR,	&TI.TI_rin },
	{ "parm_up_cursor",		"cuu",		"UP",	CAP_TYPE_STR,	&TI.TI_cuu },
	{ "pkey_key",			"pfkey",	"pk",	CAP_TYPE_STR,	&TI.TI_pfkey },
	{ "pkey_local",			"pfloc",	"pl",	CAP_TYPE_STR,	&TI.TI_pfloc },
	{ "pkey_xmit",			"pfx",		"px",	CAP_TYPE_STR,	&TI.TI_pfx },
	{ "print_screen",		"mc0",		"ps",	CAP_TYPE_STR,	&TI.TI_mc0 },
	{ "prtr_off",			"mc4",		"pf",	CAP_TYPE_STR,	&TI.TI_mc4 },
	{ "prtr_on",			"mc5",		"po",	CAP_TYPE_STR,	&TI.TI_mc5 },
	{ "repeat_char",		"rep",		"rp",	CAP_TYPE_STR,	&TI.TI_rep },
	{ "reset_1string",		"rs1",		"r1",	CAP_TYPE_STR,	&TI.TI_rs1 },
	{ "reset_2string",		"rs2",		"r2",	CAP_TYPE_STR,	&TI.TI_rs2 },
	{ "reset_3string",		"rs3",		"r3",	CAP_TYPE_STR,	&TI.TI_rs3 },
	{ "reset_file",			"rf",		"rf",	CAP_TYPE_STR,	&TI.TI_rf },
	{ "restore_cursor",		"rc",		"rc",	CAP_TYPE_STR,	&TI.TI_rc },
	{ "row_address",		"vpa",		"cv",	CAP_TYPE_STR,	&TI.TI_vpa },
	{ "save_cursor",		"sc",		"sc",	CAP_TYPE_STR,	&TI.TI_sc },
	{ "scroll_forward",		"ind",		"sf",	CAP_TYPE_STR,	&TI.TI_ind },
	{ "scroll_reverse",		"ri",		"sr",	CAP_TYPE_STR,	&TI.TI_ri },
	{ "set_attributes",		"sgr",		"sa",	CAP_TYPE_STR,	&TI.TI_sgr },
	{ "set_tab",			"hts",		"st",	CAP_TYPE_STR,	&TI.TI_hts },
	{ "set_window",			"wind",		"wi",	CAP_TYPE_STR,	&TI.TI_wind },
	{ "tab",			"ht",		"ta",	CAP_TYPE_STR,	&TI.TI_ht },
	{ "to_status_line",		"tsl",		"ts",	CAP_TYPE_STR,	&TI.TI_tsl },
	{ "underline_char",		"uc",		"uc",	CAP_TYPE_STR,	&TI.TI_uc },
	{ "up_half_line",		"hu",		"hu",	CAP_TYPE_STR,	&TI.TI_hu },
	{ "init_prog",			"iprog",	"iP",	CAP_TYPE_STR,	&TI.TI_iprog },
	{ "key_a1",			"ka1",		"K1",	CAP_TYPE_STR,	&TI.TI_ka1 },
	{ "key_a3",			"ka3",		"K3",	CAP_TYPE_STR,	&TI.TI_ka3 },
	{ "key_b2",			"kb2",		"K2",	CAP_TYPE_STR,	&TI.TI_kb2 },
	{ "key_c1",			"kc1",		"K4",	CAP_TYPE_STR,	&TI.TI_kc1 },
	{ "key_c3",			"kc3",		"K5",	CAP_TYPE_STR,	&TI.TI_kc3 },
	{ "prtr_non",			"mc5p",		"pO",	CAP_TYPE_STR,	&TI.TI_mc5p },
	{ "char_padding",		"rmp",		"rP",	CAP_TYPE_STR,	&TI.TI_rmp },
	{ "acs_chars",			"acsc",		"ac",	CAP_TYPE_STR,	&TI.TI_acsc },
	{ "plab_norm",			"pln",		"pn",	CAP_TYPE_STR,	&TI.TI_pln },
	{ "key_btab",			"kcbt",		"kB",	CAP_TYPE_STR,	&TI.TI_kcbt },
	{ "enter_xon_mode",		"smxon",	"SX",	CAP_TYPE_STR,	&TI.TI_smxon },
	{ "exit_xon_mode",		"rmxon",	"RX",	CAP_TYPE_STR,	&TI.TI_rmxon },
	{ "enter_am_mode",		"smam",		"SA",	CAP_TYPE_STR,	&TI.TI_smam },
	{ "exit_am_mode",		"rmam",		"RA",	CAP_TYPE_STR,	&TI.TI_rmam },
	{ "xon_character",		"xonc",		"XN",	CAP_TYPE_STR,	&TI.TI_xonc },
	{ "xoff_character",		"xoffc",	"XF",	CAP_TYPE_STR,	&TI.TI_xoffc },
	{ "ena_acs",			"enacs",	"eA",	CAP_TYPE_STR,	&TI.TI_enacs },
	{ "label_on",			"smln",		"LO",	CAP_TYPE_STR,	&TI.TI_smln },
	{ "label_off",			"rmln",		"LF",	CAP_TYPE_STR,	&TI.TI_rmln },
	{ "key_beg",			"kbeg",		"@1",	CAP_TYPE_STR,	&TI.TI_kbeg },
	{ "key_cancel",			"kcan",		"@2",	CAP_TYPE_STR,	&TI.TI_kcan },
	{ "key_close",			"kclo",		"@3",	CAP_TYPE_STR,	&TI.TI_kclo },
	{ "key_command",		"kcmd",		"@4",	CAP_TYPE_STR,	&TI.TI_kcmd },
	{ "key_copy",			"kcpy",		"@5",	CAP_TYPE_STR,	&TI.TI_kcpy },
	{ "key_create",			"kcrt",		"@6",	CAP_TYPE_STR,	&TI.TI_kcrt },
	{ "key_end",			"kend",		"@7",	CAP_TYPE_STR,	&TI.TI_kend },
	{ "key_enter",			"kent",		"@8",	CAP_TYPE_STR,	&TI.TI_kent },
	{ "key_exit",			"kext",		"@9",	CAP_TYPE_STR,	&TI.TI_kext },
	{ "key_find",			"kfnd",		"@0",	CAP_TYPE_STR,	&TI.TI_kfnd },
	{ "key_help",			"khlp",		"%1",	CAP_TYPE_STR,	&TI.TI_khlp },
	{ "key_mark",			"kmrk",		"%2",	CAP_TYPE_STR,	&TI.TI_kmrk },
	{ "key_message",		"kmsg",		"%3",	CAP_TYPE_STR,	&TI.TI_kmsg },
	{ "key_move",			"kmov",		"%4",	CAP_TYPE_STR,	&TI.TI_kmov },
	{ "key_next",			"knxt",		"%5",	CAP_TYPE_STR,	&TI.TI_knxt },
	{ "key_open",			"kopn",		"%6",	CAP_TYPE_STR,	&TI.TI_kopn },
	{ "key_options",		"kopt",		"%7",	CAP_TYPE_STR,	&TI.TI_kopt },
	{ "key_previous",		"kprv",		"%8",	CAP_TYPE_STR,	&TI.TI_kprv },
	{ "key_print",			"kprt",		"%9",	CAP_TYPE_STR,	&TI.TI_kprt },
	{ "key_redo",			"krdo",		"%0",	CAP_TYPE_STR,	&TI.TI_krdo },
	{ "key_reference",		"kref",		"&1",	CAP_TYPE_STR,	&TI.TI_kref },
	{ "key_refresh",		"krfr",		"&2",	CAP_TYPE_STR,	&TI.TI_krfr },
	{ "key_replace",		"krpl",		"&3",	CAP_TYPE_STR,	&TI.TI_krpl },
	{ "key_restart",		"krst",		"&4",	CAP_TYPE_STR,	&TI.TI_krst },
	{ "key_resume",			"kres",		"&5",	CAP_TYPE_STR,	&TI.TI_kres },
	{ "key_save",			"ksav",		"&6",	CAP_TYPE_STR,	&TI.TI_ksav },
	{ "key_suspend",		"kspd",		"&7",	CAP_TYPE_STR,	&TI.TI_kspd },
	{ "key_undo",			"kund",		"&8",	CAP_TYPE_STR,	&TI.TI_kund },
	{ "key_sbeg",			"kBEG",		"&9",	CAP_TYPE_STR,	&TI.TI_kBEG },
	{ "key_scancel",		"kCAN",		"&0",	CAP_TYPE_STR,	&TI.TI_kCAN },
	{ "key_scommand",		"kCMD",		"*1",	CAP_TYPE_STR,	&TI.TI_kCMD },
	{ "key_scopy",			"kCPY",		"*2",	CAP_TYPE_STR,	&TI.TI_kCPY },
	{ "key_screate",		"kCRT",		"*3",	CAP_TYPE_STR,	&TI.TI_kCRT },
	{ "key_sdc",			"kDC",		"*4",	CAP_TYPE_STR,	&TI.TI_kDC },
	{ "key_sdl",			"kDL",		"*5",	CAP_TYPE_STR,	&TI.TI_kDL },
	{ "key_select",			"kslt",		"*6",	CAP_TYPE_STR,	&TI.TI_kslt },
	{ "key_send",			"kEND",		"*7",	CAP_TYPE_STR,	&TI.TI_kEND },
	{ "key_seol",			"kEOL",		"*8",	CAP_TYPE_STR,	&TI.TI_kEOL },
	{ "key_sexit",			"kEXT",		"*9",	CAP_TYPE_STR,	&TI.TI_kEXT },
	{ "key_sfind",			"kFND",		"*0",	CAP_TYPE_STR,	&TI.TI_kFND },
	{ "key_shelp",			"kHLP",		"#1",	CAP_TYPE_STR,	&TI.TI_kHLP },
	{ "key_shome",			"kHOM",		"#2",	CAP_TYPE_STR,	&TI.TI_kHOM },
	{ "key_sic",			"kIC",		"#3",	CAP_TYPE_STR,	&TI.TI_kIC },
	{ "key_sleft",			"kLFT",		"#4",	CAP_TYPE_STR,	&TI.TI_kLFT },
	{ "key_smessage",		"kMSG",		"%a",	CAP_TYPE_STR,	&TI.TI_kMSG },
	{ "key_smove",			"kMOV",		"%b",	CAP_TYPE_STR,	&TI.TI_kMOV },
	{ "key_snext",			"kNXT",		"%c",	CAP_TYPE_STR,	&TI.TI_kNXT },
	{ "key_soptions",		"kOPT",		"%d",	CAP_TYPE_STR,	&TI.TI_kOPT },
	{ "key_sprevious",		"kPRV",		"%e",	CAP_TYPE_STR,	&TI.TI_kPRV },
	{ "key_sprint",			"kPRT",		"%f",	CAP_TYPE_STR,	&TI.TI_kPRT },
	{ "key_sredo",			"kRDO",		"%g",	CAP_TYPE_STR,	&TI.TI_kRDO },
	{ "key_sreplace",		"kRPL",		"%h",	CAP_TYPE_STR,	&TI.TI_kRPL },
	{ "key_sright",			"kRIT",		"%i",	CAP_TYPE_STR,	&TI.TI_kRIT },
	{ "key_srsume",			"kRES",		"%j",	CAP_TYPE_STR,	&TI.TI_kRES },
	{ "key_ssave",			"kSAV",		"!1",	CAP_TYPE_STR,	&TI.TI_kSAV },
	{ "key_ssuspend",		"kSPD",		"!2",	CAP_TYPE_STR,	&TI.TI_kSPD },
	{ "key_sundo",			"kUND",		"!3",	CAP_TYPE_STR,	&TI.TI_kUND },
	{ "req_for_input",		"rfi",		"RF",	CAP_TYPE_STR,	&TI.TI_rfi },
	{ "key_f11",			"kf11",		"F1",	CAP_TYPE_STR,	&TI.TI_kf11 },
	{ "key_f12",			"kf12",		"F2",	CAP_TYPE_STR,	&TI.TI_kf12 },
	{ "key_f13",			"kf13",		"F3",	CAP_TYPE_STR,	&TI.TI_kf13 },
	{ "key_f14",			"kf14",		"F4",	CAP_TYPE_STR,	&TI.TI_kf14 },
	{ "key_f15",			"kf15",		"F5",	CAP_TYPE_STR,	&TI.TI_kf15 },
	{ "key_f16",			"kf16",		"F6",	CAP_TYPE_STR,	&TI.TI_kf16 },
	{ "key_f17",			"kf17",		"F7",	CAP_TYPE_STR,	&TI.TI_kf17 },
	{ "key_f18",			"kf18",		"F8",	CAP_TYPE_STR,	&TI.TI_kf18 },
	{ "key_f19",			"kf19",		"F9",	CAP_TYPE_STR,	&TI.TI_kf19 },
	{ "key_f20",			"kf20",		"FA",	CAP_TYPE_STR,	&TI.TI_kf20 },
	{ "key_f21",			"kf21",		"FB",	CAP_TYPE_STR,	&TI.TI_kf21 },
	{ "key_f22",			"kf22",		"FC",	CAP_TYPE_STR,	&TI.TI_kf22 },
	{ "key_f23",			"kf23",		"FD",	CAP_TYPE_STR,	&TI.TI_kf23 },
	{ "key_f24",			"kf24",		"FE",	CAP_TYPE_STR,	&TI.TI_kf24 },
	{ "key_f25",			"kf25",		"FF",	CAP_TYPE_STR,	&TI.TI_kf25 },
	{ "key_f26",			"kf26",		"FG",	CAP_TYPE_STR,	&TI.TI_kf26 },
	{ "key_f27",			"kf27",		"FH",	CAP_TYPE_STR,	&TI.TI_kf27 },
	{ "key_f28",			"kf28",		"FI",	CAP_TYPE_STR,	&TI.TI_kf28 },
	{ "key_f29",			"kf29",		"FJ",	CAP_TYPE_STR,	&TI.TI_kf29 },
	{ "key_f30",			"kf30",		"FK",	CAP_TYPE_STR,	&TI.TI_kf30 },
	{ "key_f31",			"kf31",		"FL",	CAP_TYPE_STR,	&TI.TI_kf31 },
	{ "key_f32",			"kf32",		"FM",	CAP_TYPE_STR,	&TI.TI_kf32 },
	{ "key_f33",			"kf33",		"FN",	CAP_TYPE_STR,	&TI.TI_kf33 },
	{ "key_f34",			"kf34",		"FO",	CAP_TYPE_STR,	&TI.TI_kf34 },
	{ "key_f35",			"kf35",		"FP",	CAP_TYPE_STR,	&TI.TI_kf35 },
	{ "key_f36",			"kf36",		"FQ",	CAP_TYPE_STR,	&TI.TI_kf36 },
	{ "key_f37",			"kf37",		"FR",	CAP_TYPE_STR,	&TI.TI_kf37 },
	{ "key_f38",			"kf38",		"FS",	CAP_TYPE_STR,	&TI.TI_kf38 },
	{ "key_f39",			"kf39",		"FT",	CAP_TYPE_STR,	&TI.TI_kf39 },
	{ "key_f40",			"kf40",		"FU",	CAP_TYPE_STR,	&TI.TI_kf40 },
	{ "key_f41",			"kf41",		"FV",	CAP_TYPE_STR,	&TI.TI_kf41 },
	{ "key_f42",			"kf42",		"FW",	CAP_TYPE_STR,	&TI.TI_kf42 },
	{ "key_f43",			"kf43",		"FX",	CAP_TYPE_STR,	&TI.TI_kf43 },
	{ "key_f44",			"kf44",		"FY",	CAP_TYPE_STR,	&TI.TI_kf44 },
	{ "key_f45",			"kf45",		"FZ",	CAP_TYPE_STR,	&TI.TI_kf45 },
	{ "key_f46",			"kf46",		"Fa",	CAP_TYPE_STR,	&TI.TI_kf46 },
	{ "key_f47",			"kf47",		"Fb",	CAP_TYPE_STR,	&TI.TI_kf47 },
	{ "key_f48",			"kf48",		"Fc",	CAP_TYPE_STR,	&TI.TI_kf48 },
	{ "key_f49",			"kf49",		"Fd",	CAP_TYPE_STR,	&TI.TI_kf49 },
	{ "key_f50",			"kf50",		"Fe",	CAP_TYPE_STR,	&TI.TI_kf50 },
	{ "key_f51",			"kf51",		"Ff",	CAP_TYPE_STR,	&TI.TI_kf51 },
	{ "key_f52",			"kf52",		"Fg",	CAP_TYPE_STR,	&TI.TI_kf52 },
	{ "key_f53",			"kf53",		"Fh",	CAP_TYPE_STR,	&TI.TI_kf53 },
	{ "key_f54",			"kf54",		"Fi",	CAP_TYPE_STR,	&TI.TI_kf54 },
	{ "key_f55",			"kf55",		"Fj",	CAP_TYPE_STR,	&TI.TI_kf55 },
	{ "key_f56",			"kf56",		"Fk",	CAP_TYPE_STR,	&TI.TI_kf56 },
	{ "key_f57",			"kf57",		"Fl",	CAP_TYPE_STR,	&TI.TI_kf57 },
	{ "key_f58",			"kf58",		"Fm",	CAP_TYPE_STR,	&TI.TI_kf58 },
	{ "key_f59",			"kf59",		"Fn",	CAP_TYPE_STR,	&TI.TI_kf59 },
	{ "key_f60",			"kf60",		"Fo",	CAP_TYPE_STR,	&TI.TI_kf60 },
	{ "key_f61",			"kf61",		"Fp",	CAP_TYPE_STR,	&TI.TI_kf61 },
	{ "key_f62",			"kf62",		"Fq",	CAP_TYPE_STR,	&TI.TI_kf62 },
	{ "key_f63",			"kf63",		"Fr",	CAP_TYPE_STR,	&TI.TI_kf63 },
	{ "clr_bol",			"el1",		"cb",	CAP_TYPE_STR,	&TI.TI_el1 },
	{ "clear_margins",		"mgc",		"MC",	CAP_TYPE_STR,	&TI.TI_mgc },
	{ "set_left_margin",		"smgl",		"ML",	CAP_TYPE_STR,	&TI.TI_smgl },
	{ "set_right_margin",		"smgr",		"MR",	CAP_TYPE_STR,	&TI.TI_smgr },
	{ "label_format",		"fln",		"Lf",	CAP_TYPE_STR,	&TI.TI_fln },
	{ "set_clock",			"sclk",		"SC",	CAP_TYPE_STR,	&TI.TI_sclk },
	{ "display_clock",		"dclk",		"DK",	CAP_TYPE_STR,	&TI.TI_dclk },
	{ "remove_clock",		"rmclk",	"RC",	CAP_TYPE_STR,	&TI.TI_rmclk },
	{ "create_window",		"cwin",		"CW",	CAP_TYPE_STR,	&TI.TI_cwin },
	{ "goto_window",		"wingo",	"WG",	CAP_TYPE_STR,	&TI.TI_wingo },
	{ "hangup",			"hup",		"HU",	CAP_TYPE_STR,	&TI.TI_hup },
	{ "dial_phone",			"dial",		"DI",	CAP_TYPE_STR,	&TI.TI_dial },
	{ "quick_dial",			"qdial",	"QD",	CAP_TYPE_STR,	&TI.TI_qdial },
	{ "tone",			"tone",		"TO",	CAP_TYPE_STR,	&TI.TI_tone },
	{ "pulse",			"pulse",	"PU",	CAP_TYPE_STR,	&TI.TI_pulse },
	{ "flash_hook",			"hook",		"fh",	CAP_TYPE_STR,	&TI.TI_hook },
	{ "fixed_pause",		"pause",	"PA",	CAP_TYPE_STR,	&TI.TI_pause },
	{ "wait_tone",			"wait",		"WA",	CAP_TYPE_STR,	&TI.TI_wait },
	{ "user0",			"u0",		"u0",	CAP_TYPE_STR,	&TI.TI_u0 },
	{ "user1",			"u1",		"u1",	CAP_TYPE_STR,	&TI.TI_u1 },
	{ "user2",			"u2",		"u2",	CAP_TYPE_STR,	&TI.TI_u2 },
	{ "user3",			"u3",		"u3",	CAP_TYPE_STR,	&TI.TI_u3 },
	{ "user4",			"u4",		"u4",	CAP_TYPE_STR,	&TI.TI_u4 },
	{ "user5",			"u5",		"u5",	CAP_TYPE_STR,	&TI.TI_u5 },
	{ "user6",			"u6",		"u6",	CAP_TYPE_STR,	&TI.TI_u6 },
	{ "user7",			"u7",		"u7",	CAP_TYPE_STR,	&TI.TI_u7 },
	{ "user8",			"u8",		"u8",	CAP_TYPE_STR,	&TI.TI_u8 },
	{ "user9",			"u9",		"u9",	CAP_TYPE_STR,	&TI.TI_u9 },
	{ "orig_pair",			"op",		"op",	CAP_TYPE_STR,	&TI.TI_op },
	{ "orig_colors",		"oc",		"oc",	CAP_TYPE_STR,	&TI.TI_oc },
	{ "initialize_color",		"initc",	"Ic",	CAP_TYPE_STR,	&TI.TI_initc },
	{ "initialize_pair",		"initp",	"Ip",	CAP_TYPE_STR,	&TI.TI_initp },
	{ "set_color_pair",		"scp",		"sp",	CAP_TYPE_STR,	&TI.TI_scp },
	{ "set_foreground",		"setf",		"Sf",	CAP_TYPE_STR,	&TI.TI_setf },
	{ "set_background",		"setb",		"Sb",	CAP_TYPE_STR,	&TI.TI_setb },
	{ "change_char_pitch",		"cpi",		"ZA",	CAP_TYPE_STR,	&TI.TI_cpi },
	{ "change_line_pitch",		"lpi",		"ZB",	CAP_TYPE_STR,	&TI.TI_lpi },
	{ "change_res_horz",		"chr",		"ZC",	CAP_TYPE_STR,	&TI.TI_chr },
	{ "change_res_vert",		"cvr",		"ZD",	CAP_TYPE_STR,	&TI.TI_cvr },
	{ "define_char",		"defc",		"ZE",	CAP_TYPE_STR,	&TI.TI_defc },
	{ "enter_doublewide_mode",	"swidm",	"ZF",	CAP_TYPE_STR,	&TI.TI_swidm },
	{ "enter_draft_quality",	"sdrfq",	"ZG",	CAP_TYPE_STR,	&TI.TI_sdrfq },
	{ "enter_italics_mode",		"sitm",		"ZH",	CAP_TYPE_STR,	&TI.TI_sitm },
	{ "enter_leftward_mode",	"slm",		"ZI",	CAP_TYPE_STR,	&TI.TI_slm },
	{ "enter_micro_mode",		"smicm",	"ZJ",	CAP_TYPE_STR,	&TI.TI_smicm },
	{ "enter_near_letter_quality",	"snlq",		"ZK",	CAP_TYPE_STR,	&TI.TI_snlq },
	{ "enter_normal_quality",	"snrmq",	"ZL",	CAP_TYPE_STR,	&TI.TI_snrmq },
	{ "enter_shadow_mode",		"sshm",		"ZM",	CAP_TYPE_STR,	&TI.TI_sshm },
	{ "enter_subscript_mode",	"ssubm",	"ZN",	CAP_TYPE_STR,	&TI.TI_ssubm },
	{ "enter_superscript_mode",	"ssupm",	"ZO",	CAP_TYPE_STR,	&TI.TI_ssupm },
	{ "enter_upward_mode",		"sum",		"ZP",	CAP_TYPE_STR,	&TI.TI_sum },
	{ "exit_doublewide_mode",	"rwidm",	"ZQ",	CAP_TYPE_STR,	&TI.TI_rwidm },
	{ "exit_italics_mode",		"ritm",		"ZR",	CAP_TYPE_STR,	&TI.TI_ritm },
	{ "exit_leftward_mode",		"rlm",		"ZS",	CAP_TYPE_STR,	&TI.TI_rlm },
	{ "exit_micro_mode",		"rmicm",	"ZT",	CAP_TYPE_STR,	&TI.TI_rmicm },
	{ "exit_shadow_mode",		"rshm",		"ZU",	CAP_TYPE_STR,	&TI.TI_rshm },
	{ "exit_subscript_mode",	"rsubm",	"ZV",	CAP_TYPE_STR,	&TI.TI_rsubm },
	{ "exit_superscript_mode",	"rsupm",	"ZW",	CAP_TYPE_STR,	&TI.TI_rsupm },
	{ "exit_upward_mode",		"rum",		"ZX",	CAP_TYPE_STR,	&TI.TI_rum },
	{ "micro_column_address",	"mhpa",		"ZY",	CAP_TYPE_STR,	&TI.TI_mhpa },
	{ "micro_down",			"mcud1",	"ZZ",	CAP_TYPE_STR,	&TI.TI_mcud1 },
	{ "micro_left",			"mcub1",	"Za",	CAP_TYPE_STR,	&TI.TI_mcub1 },
	{ "micro_right",		"mcuf1",	"Zb",	CAP_TYPE_STR,	&TI.TI_mcuf1 },
	{ "micro_row_address",		"mvpa",		"Zc",	CAP_TYPE_STR,	&TI.TI_mvpa },
	{ "micro_up",			"mcuu1",	"Zd",	CAP_TYPE_STR,	&TI.TI_mcuu1 },
	{ "order_of_pins",		"porder",	"Ze",	CAP_TYPE_STR,	&TI.TI_porder },
	{ "parm_down_micro",		"mcud",		"Zf",	CAP_TYPE_STR,	&TI.TI_mcud },
	{ "parm_left_micro",		"mcub",		"Zg",	CAP_TYPE_STR,	&TI.TI_mcub },
	{ "parm_right_micro",		"mcuf",		"Zh",	CAP_TYPE_STR,	&TI.TI_mcuf },
	{ "parm_up_micro",		"mcuu",		"Zi",	CAP_TYPE_STR,	&TI.TI_mcuu },
	{ "select_char_set",		"scs",		"Zj",	CAP_TYPE_STR,	&TI.TI_scs },
	{ "set_bottom_margin",		"smgb",		"Zk",	CAP_TYPE_STR,	&TI.TI_smgb },
	{ "set_bottom_margin_parm",	"smgbp",	"Zl",	CAP_TYPE_STR,	&TI.TI_smgbp },
	{ "set_left_margin_parm",	"smglp",	"Zm",	CAP_TYPE_STR,	&TI.TI_smglp },
	{ "set_right_margin_parm",	"smgrp",	"Zn",	CAP_TYPE_STR,	&TI.TI_smgrp },
	{ "set_top_margin",		"smgt",		"Zo",	CAP_TYPE_STR,	&TI.TI_smgt },
	{ "set_top_margin_parm",	"smgtp",	"Zp",	CAP_TYPE_STR,	&TI.TI_smgtp },
	{ "start_bit_image",		"sbim",		"Zq",	CAP_TYPE_STR,	&TI.TI_sbim },
	{ "start_char_set_def",		"scsd",		"Zr",	CAP_TYPE_STR,	&TI.TI_scsd },
	{ "stop_bit_image",		"rbim",		"Zs",	CAP_TYPE_STR,	&TI.TI_rbim },
	{ "stop_char_set_def",		"rcsd",		"Zt",	CAP_TYPE_STR,	&TI.TI_rcsd },
	{ "subscript_characters",	"subcs",	"Zu",	CAP_TYPE_STR,	&TI.TI_subcs },
	{ "superscript_characters",	"supcs",	"Zv",	CAP_TYPE_STR,	&TI.TI_supcs },
	{ "these_cause_cr",		"docr",		"Zw",	CAP_TYPE_STR,	&TI.TI_docr },
	{ "zero_motion",		"zerom",	"Zx",	CAP_TYPE_STR,	&TI.TI_zerom },
	{ "char_set_names",		"csnm",		"Zy",	CAP_TYPE_STR,	&TI.TI_csnm },
	{ "key_mouse",			"kmous",	"Km",	CAP_TYPE_STR,	&TI.TI_kmous },
	{ "mouse_info",			"minfo",	"Mi",	CAP_TYPE_STR,	&TI.TI_minfo },
	{ "req_mouse_pos",		"reqmp",	"RQ",	CAP_TYPE_STR,	&TI.TI_reqmp },
	{ "get_mouse",			"getm",		"Gm",	CAP_TYPE_STR,	&TI.TI_getm },
	{ "set_a_foreground",		"setaf",	"AF",	CAP_TYPE_STR,	&TI.TI_setaf },
	{ "set_a_background",		"setab",	"AB",	CAP_TYPE_STR,	&TI.TI_setab },
	{ "pkey_plab",			"pfxl",		"xl",	CAP_TYPE_STR,	&TI.TI_pfxl },
	{ "device_type",		"devt",		"dv",	CAP_TYPE_STR,	&TI.TI_devt },
	{ "code_set_init",		"csin",		"ci",	CAP_TYPE_STR,	&TI.TI_csin },
	{ "set0_des_seq",		"s0ds",		"s0",	CAP_TYPE_STR,	&TI.TI_s0ds },
	{ "set1_des_seq",		"s1ds",		"s1",	CAP_TYPE_STR,	&TI.TI_s1ds },
	{ "set2_des_seq",		"s2ds",		"s2",	CAP_TYPE_STR,	&TI.TI_s2ds },
	{ "set3_des_seq",		"s3ds",		"s3",	CAP_TYPE_STR,	&TI.TI_s3ds },
	{ "set_lr_margin",		"smglr",	"ML",	CAP_TYPE_STR,	&TI.TI_smglr },
	{ "set_tb_margin",		"smgtb",	"MT",	CAP_TYPE_STR,	&TI.TI_smgtb },
	{ "bit_image_repeat",		"birep",	"Xy",	CAP_TYPE_STR,	&TI.TI_birep },
	{ "bit_image_newline",		"binel",	"Zz",	CAP_TYPE_STR,	&TI.TI_binel },
	{ "bit_image_carriage_return",	"bicr",		"Yv",	CAP_TYPE_STR,	&TI.TI_bicr },
	{ "color_names",		"colornm",	"Yw",	CAP_TYPE_STR,	&TI.TI_colornm },
	{ "define_bit_image_region",	"defbi",	"Yx",	CAP_TYPE_STR,	&TI.TI_defbi },
	{ "end_bit_image_region",	"endbi",	"Yy",	CAP_TYPE_STR,	&TI.TI_endbi },
	{ "set_color_band",		"setcolor",	"Yz",	CAP_TYPE_STR,	&TI.TI_setcolor },
	{ "set_page_length",		"slines",	"YZ",	CAP_TYPE_STR,	&TI.TI_slines },
	{ "display_pc_char",		"dispc",	"S1",	CAP_TYPE_STR,	&TI.TI_dispc },
	{ "enter_pc_charset_mode",	"smpch",	"S2",	CAP_TYPE_STR,	&TI.TI_smpch },
	{ "exit_pc_charset_mode",	"rmpch",	"S3",	CAP_TYPE_STR,	&TI.TI_rmpch },
	{ "enter_scancode_mode",	"smsc",		"S4",	CAP_TYPE_STR,	&TI.TI_smsc },
	{ "exit_scancode_mode",		"rmsc",		"S5",	CAP_TYPE_STR,	&TI.TI_rmsc },
	{ "pc_term_options",		"pctrm",	"S6",	CAP_TYPE_STR,	&TI.TI_pctrm },
	{ "scancode_escape",		"scesc",	"S7",	CAP_TYPE_STR,	&TI.TI_scesc },
	{ "alt_scancode_esc",		"scesa",	"S8",	CAP_TYPE_STR,	&TI.TI_scesa },
	{ "enter_horizontal_hl_mode",	"ehhlm",	"Xh",	CAP_TYPE_STR,	&TI.TI_ehhlm },
	{ "enter_left_hl_mode",		"elhlm",	"Xl",	CAP_TYPE_STR,	&TI.TI_elhlm },
	{ "enter_low_hl_mode",		"elohlm",	"Xo",	CAP_TYPE_STR,	&TI.TI_elohlm },
	{ "enter_right_hl_mode",	"erhlm",	"Xr",	CAP_TYPE_STR,	&TI.TI_erhlm },
	{ "enter_top_hl_mode",		"ethlm",	"Xt",	CAP_TYPE_STR,	&TI.TI_ethlm },
	{ "enter_vertical_hl_mode",	"evhlm",	"Xv",	CAP_TYPE_STR,	&TI.TI_evhlm },
	{ "set_a_attributes",		"sgr1",		"sA",	CAP_TYPE_STR,	&TI.TI_sgr1 },
	{ "set_pglen_inch",		"slength",	"sL",	CAP_TYPE_STR,	&TI.TI_slength },
	{ "termcap_init2",		"OTi2",		"i2",	CAP_TYPE_STR,	&TI.TI_OTi2 },
	{ "termcap_reset",		"OTrs",		"rs",	CAP_TYPE_STR,	&TI.TI_OTrs },
	{ "magic_cookie_glitch_ul",	"OTug",		"ug",	CAP_TYPE_INT,	(char **)&TI.TI_OTug },
	{ "backspaces_with_bs",		"OTbs",		"bs",	CAP_TYPE_BOOL,	(char **)&TI.TI_OTbs },
	{ "crt_no_scrolling",		"OTns",		"ns",	CAP_TYPE_BOOL,	(char **)&TI.TI_OTns },
	{ "no_correctly_working_cr",	"OTnc",		"nc",	CAP_TYPE_BOOL,	(char **)&TI.TI_OTnc },
	{ "carriage_return_delay",	"OTdC",		"dC",	CAP_TYPE_INT,	(char **)&TI.TI_OTdC },
	{ "new_line_delay",		"OTdN",		"dN",	CAP_TYPE_INT,	(char **)&TI.TI_OTdN },
	{ "linefeed_if_not_lf",		"OTnl",		"nl",	CAP_TYPE_STR,	&TI.TI_OTnl },
	{ "backspace_if_not_bs",	"OTbc",		"bc",	CAP_TYPE_STR,	&TI.TI_OTbc },
	{ "gnu_has_meta_key",		"OTMT",		"MT",	CAP_TYPE_BOOL,	(char **)&TI.TI_OTMT },
	{ "linefeed_is_newline",	"OTNL",		"NL",	CAP_TYPE_BOOL,	(char **)&TI.TI_OTNL },
	{ "backspace_delay",		"OTdB",		"dB",	CAP_TYPE_INT,	(char **)&TI.TI_OTdB },
	{ "horizontal_tab_delay",	"OTdT",		"dT",	CAP_TYPE_INT,	(char **)&TI.TI_OTdT },
	{ "number_of_function_keys",	"OTkn",		"kn",	CAP_TYPE_INT,	(char **)&TI.TI_OTkn },
	{ "other_non_function_keys",	"OTko",		"ko",	CAP_TYPE_STR,	&TI.TI_OTko },
	{ "arrow_key_map",		"OTma",		"ma",	CAP_TYPE_STR,	&TI.TI_OTma },
	{ "has_hardware_tabs",		"OTpt",		"pt",	CAP_TYPE_BOOL,	(char **)&TI.TI_OTpt },
	{ "return_does_clr_eol",	"OTxr",		"xr",	CAP_TYPE_BOOL,	(char **)&TI.TI_OTxr },
	{ "acs_ulcorner",		"OTG2",		"G2",	CAP_TYPE_STR,	&TI.TI_OTG2 },
	{ "acs_llcorner",		"OTG3",		"G3",	CAP_TYPE_STR,	&TI.TI_OTG3 },
	{ "acs_urcorner",		"OTG1",		"G1",	CAP_TYPE_STR,	&TI.TI_OTG1 },
	{ "acs_lrcorner",		"OTG4",		"G4",	CAP_TYPE_STR,	&TI.TI_OTG4 },
	{ "acs_ltee",			"OTGR",		"GR",	CAP_TYPE_STR,	&TI.TI_OTGR },
	{ "acs_rtee",			"OTGL",		"GL",	CAP_TYPE_STR,	&TI.TI_OTGL },
	{ "acs_btee",			"OTGU",		"GU",	CAP_TYPE_STR,	&TI.TI_OTGU },
	{ "acs_ttee",			"OTGD",		"GD",	CAP_TYPE_STR,	&TI.TI_OTGD },
	{ "acs_hline",			"OTGH",		"GH",	CAP_TYPE_STR,	&TI.TI_OTGH },
	{ "acs_vline",			"OTGV",		"GV",	CAP_TYPE_STR,	&TI.TI_OTGV },
	{ "acs_plus",			"OTGC",		"GC",	CAP_TYPE_STR,	&TI.TI_OTGC },
	{ "memory_lock",		"meml",		"ml",	CAP_TYPE_STR,	&TI.TI_meml },
	{ "memory_unlock",		"memu",		"mu",	CAP_TYPE_STR,	&TI.TI_memu },
	{ "box_chars_1",		"box1",		"bx",	CAP_TYPE_STR,	&TI.TI_box1 },
};


struct my_term *current_term = &TI;
static const	int	numcaps = sizeof tcaps / sizeof tcaps[0];
static	int	term_echo_flag = 1;
static	int	li;
static	int	co;

static	void	term_establish_last_column (void);
static void	term_enable_last_column (void);
static void	term_disable_last_column (void);

/*
 * term_echo: if 0, echo is turned off (all characters appear as blanks), if
 * non-zero, all is normal.  The function returns the old value of the
 * term_echo_flag 
 */
int	term_echo (int flag)
{
	int	echo;

	echo = term_echo_flag;
	term_echo_flag = flag;
	return (echo);
}

/*
 * term_inputline_putchar: This is what handles the outputting of the input 
 * buffer.
 * It used to handle more, but slowly the stuff in screen.c prepared away
 * all of the nasties that this was intended to handle.  Well anyhow, all
 * we need to worry about here is making sure nothing suspcious, like an
 * escape, makes its way to the output stream.
 */
void	term_inputline_putchar (unsigned char c)
{
	if (!term_echo_flag)
	{
		putchar_x(' ');
		return;
	}

	/*
	 * Any nonprintable characters we lop off.  In addition to this,
	 * we catch the very nasty 0x9b which is the escape character with
	 * the high bit set.  
	 */
	if (c < 0x20/* || c == 0x9b*/)
	{
		term_standout_on();
		putchar_x((c | 0x40) & 0x7f);
		term_standout_off();
	}

	/*
	 * The delete character is handled especially as well
	 */
	else if (c == 0x7f) 	/* delete char */
	{
		term_standout_on();
		putchar_x('?');
		term_standout_off();
	}

	/*
	 * Everything else is passed through.
	 */
	else
		putchar_x(c);
}


/*
 * term_reset: sets terminal attributed back to what they were before the
 * program started 
 */
void	term_reset (void)
{
	tcsetattr(tty_des, TCSADRAIN, &oldb);

	if (current_term->TI_csr)
		tputs_x(tparm2(current_term->TI_csr, 0, get_screen_lines(main_screen) - 1));
	term_gotoxy(0, get_screen_lines(main_screen) - 1);
#if use_alt_screen
	if (current_term->TI_rmcup)
		tputs_x(current_term->TI_rmcup);
#endif
	term_disable_last_column();
	term_flush();
}

/*
 * term_cont: sets the terminal back to IRCII stuff when it is restarted
 * after a SIGSTOP.  Somewhere, this must be used in a signal() call 
 */
SIGNAL_HANDLER(term_cont)
{
	use_input = foreground = (tcgetpgrp(0) == getpgrp());
	if (foreground)
	{
#if use_alt_screen
		if (current_term->TI_smcup)
			tputs_x(current_term->TI_smcup);
#endif
		term_establish_last_column();
		need_redraw = 1;
		tcsetattr(tty_des, TCSADRAIN, &newb);
	}
}

/*
 * term_pause: sets terminal back to pre-program days, then SIGSTOPs itself. 
 */
BUILT_IN_KEYBINDING(term_pause)
{
	term_reset();
	kill(getpid(), SIGSTOP);
}



/*
 * term_init: does all terminal initialization... reads termcap info, sets
 * the terminal to CBREAK, no ECHO mode.   Chooses the best of the terminal
 * attributes to use ..  for the version of this function that is called for
 * wserv, we set the termial to RAW, no ECHO, so that all the signals are
 * ignored.. fixes quite a few problems...  -phone, jan 1993..
 */
int	termfeatures = 0;

int 	term_init (void)
{
	int	i,
		desired;
	char	*term;

	if (dumb_mode)
		panic(1, "term_init called in dumb_mode");

	if ((term = getenv("TERM")) == (char *) 0)
	{
		fprintf(stderr, "\n");
		fprintf(stderr, "You do not have a TERM environment variable.\n");
		fprintf(stderr, "So we'll be running in dumb mode...\n");
		return -1;
	}
	else
		printf("Using terminal type [%s]\n", term);

#ifdef HAVE_TERMINFO
	setupterm(NULL, 1, &i);
	if (i != 1)
	{
		fprintf(stderr, "setupterm failed: %d\n", i);
		fprintf(stderr, "So we'll be running in dumb mode...\n");
		return -1;
	}
#else
	if (tgetent(termcap, term) < 1)
	{
		fprintf(stderr, "\n");
		fprintf(stderr, "Your current TERM setting (%s) does not have a termcap entry.\n", term);
		fprintf(stderr, "So we'll be running in dumb mode...\n");
		return -1;
	}
#endif

	for (i = 0; i < numcaps; i++)
	{
		int ival;
		char *cval;

		if (tcaps[i].type == CAP_TYPE_INT)
		{
			ival = Tgetnum(tcaps[i]);
			*(int *)tcaps[i].ptr = ival;
		}
		else if (tcaps[i].type == CAP_TYPE_BOOL)
		{
			ival = Tgetflag(tcaps[i]);
			*(int *)tcaps[i].ptr = ival;
		}
		else
		{
			cval = Tgetstr(tcaps[i], tptr);
			if (cval == (char *) -1)
				*(char * *)tcaps[i].ptr = NULL;
			else
				*(char * *)tcaps[i].ptr = cval;
		}
	}

	li = current_term->TI_lines;
	co = current_term->TI_cols;
	if (!co)
		co = 79;
	if (!li)
		li = 24;

	if (!current_term->TI_nel)
		current_term->TI_nel = "\n";
	if (!current_term->TI_cr)
		current_term->TI_cr = "\r";

	current_term->TI_normal[0] = 0;
	if (current_term->TI_sgr0)
		strlcat(current_term->TI_normal, current_term->TI_sgr0, 
				sizeof current_term->TI_normal);
	if (current_term->TI_rmso)
	{
		if (current_term->TI_sgr0 && 
		    strcmp(current_term->TI_rmso, current_term->TI_sgr0))
			strlcat(current_term->TI_normal, current_term->TI_rmso,
					 sizeof current_term->TI_normal);
	}
	if (current_term->TI_rmul)
	{
		if (current_term->TI_sgr0 && 
		    strcmp(current_term->TI_rmul, current_term->TI_sgr0))
			strlcat(current_term->TI_normal, current_term->TI_rmul, 
					sizeof current_term->TI_normal);
	}
	/*
	 * On some systems, alternate char set mode isn't exited with sgr0
	 */
	if (current_term->TI_rmacs)
		strlcat(current_term->TI_normal, current_term->TI_rmacs, 
				sizeof current_term->TI_normal);


	/*
	 * Finally set up the current_term->TI_sgrstrs array.
	 * Clean it out first...
	 */
	for (i = 0; i < TERM_SGR_MAXVAL; i++)
		current_term->TI_sgrstrs[i] = "";

	/*
	 * Default to no colors. (ick)
	 */
	for (i = 0; i < 256; i++)
		current_term->TI_forecolors[i] = current_term->TI_backcolors[i] = "";
	for (i = 0; i < 8; i++)
		current_term->TI_bold_forecolors[i] = current_term->TI_bold_backcolors[i] = "";

	if (current_term->TI_bold)
		current_term->TI_sgrstrs[TERM_SGR_BOLD_ON-1] = current_term->TI_bold;
	if (current_term->TI_sgr0)
	{
		current_term->TI_sgrstrs[TERM_SGR_BOLD_OFF - 1] = current_term->TI_sgr0;
		current_term->TI_sgrstrs[TERM_SGR_BLINK_OFF - 1] = current_term->TI_sgr0;
	}
	if (current_term->TI_blink)
		current_term->TI_sgrstrs[TERM_SGR_BLINK_ON - 1] = current_term->TI_blink;
	if (current_term->TI_smul)
		current_term->TI_sgrstrs[TERM_SGR_UNDL_ON - 1] = current_term->TI_smul;
	if (current_term->TI_rmul)
		current_term->TI_sgrstrs[TERM_SGR_UNDL_OFF - 1] = current_term->TI_rmul;
	if (current_term->TI_smso)
		current_term->TI_sgrstrs[TERM_SGR_REV_ON - 1] = current_term->TI_smso;
	if (current_term->TI_rmso)
		current_term->TI_sgrstrs[TERM_SGR_REV_OFF - 1] = current_term->TI_rmso;
	if (current_term->TI_smacs)
		current_term->TI_sgrstrs[TERM_SGR_ALTCHAR_ON - 1] = current_term->TI_smacs;
	if (current_term->TI_rmacs)
		current_term->TI_sgrstrs[TERM_SGR_ALTCHAR_OFF - 1] = current_term->TI_rmacs;
	if (current_term->TI_sitm)
		current_term->TI_sgrstrs[TERM_SGR_ITALIC_ON - 1] = current_term->TI_sitm;
	if (current_term->TI_ritm)
		current_term->TI_sgrstrs[TERM_SGR_ITALIC_OFF - 1] = current_term->TI_ritm;

	if (current_term->TI_normal[0])
	{
		current_term->TI_sgrstrs[TERM_SGR_NORMAL - 1] = current_term->TI_normal;
		current_term->TI_sgrstrs[TERM_SGR_RESET - 1] = current_term->TI_normal;
	}

	/*
	 * Now figure out whether or not this terminal is "capable enough"
	 * for the client. This is a rather complicated set of tests, as
	 * we have many ways of doing the same thing.
	 * To keep the set of tests easier, we set up a bitfield integer
	 * which will have the desired capabilities added to it. If after
	 * all the checks we dont have the desired mask, we dont have a
	 * capable enough terminal.
	 */
	desired = TERM_CAN_CUP | TERM_CAN_CLEAR | TERM_CAN_CLREOL |
		TERM_CAN_RIGHT | TERM_CAN_LEFT | TERM_CAN_SCROLL;

	termfeatures = 0;
	if (current_term->TI_cup)
		termfeatures |= TERM_CAN_CUP;
	if (current_term->TI_hpa && current_term->TI_vpa)
		termfeatures |= TERM_CAN_CUP;
	if (current_term->TI_clear)
		termfeatures |= TERM_CAN_CLEAR;
	if (current_term->TI_ed)
		termfeatures |= TERM_CAN_CLEAR;
	if (current_term->TI_dl || current_term->TI_dl1)
		termfeatures |= TERM_CAN_CLEAR;
	if (current_term->TI_il || current_term->TI_il1)
		termfeatures |= TERM_CAN_CLEAR;
	if (current_term->TI_el)
		termfeatures |= TERM_CAN_CLREOL;
	if (current_term->TI_cub || current_term->TI_mrcup || current_term->TI_cub1 || current_term->TI_kbs)
		termfeatures |= TERM_CAN_LEFT;
	if (current_term->TI_cuf  || current_term->TI_cuf1 || current_term->TI_mrcup)
		termfeatures |= TERM_CAN_RIGHT;
	if (current_term->TI_dch || current_term->TI_dch1)
		termfeatures |= TERM_CAN_DELETE;
	if (current_term->TI_ich || current_term->TI_ich1)
		termfeatures |= TERM_CAN_INSERT;
	if (current_term->TI_rep)
		termfeatures |= TERM_CAN_REPEAT;
	if (current_term->TI_csr && (current_term->TI_ri || current_term->TI_rin) && (current_term->TI_ind || current_term->TI_indn))
		termfeatures |= TERM_CAN_SCROLL;
	if (current_term->TI_wind && (current_term->TI_ri || current_term->TI_rin) && (current_term->TI_ind || current_term->TI_indn))
		termfeatures |= TERM_CAN_SCROLL;
	if ((current_term->TI_il || current_term->TI_il1) && (current_term->TI_dl || current_term->TI_dl1))
		termfeatures |= TERM_CAN_SCROLL;
	if (current_term->TI_bold && current_term->TI_sgr0)
		termfeatures |= TERM_CAN_BOLD;
	if (current_term->TI_blink && current_term->TI_sgr0)
		termfeatures |= TERM_CAN_BLINK;
	if (current_term->TI_smul && current_term->TI_rmul)
		termfeatures |= TERM_CAN_UNDL;
	if (current_term->TI_smso && current_term->TI_rmso)
		termfeatures |= TERM_CAN_REVERSE;
	if (current_term->TI_dispc)
		termfeatures |= TERM_CAN_GCHAR;
	if ((current_term->TI_setf && current_term->TI_setb) || (current_term->TI_setaf && current_term->TI_setab))
		termfeatures |= TERM_CAN_COLOR;

	if ((termfeatures & desired) != desired)
	{
		fprintf(stderr, "\nYour terminal (%s) cannot run EPIC in full screen mode.\n", term);
		fprintf(stderr, "The following features are missing from your TERM setting.\n");
		if (!(termfeatures & TERM_CAN_CUP))
			fprintf (stderr, "\tCursor movement\n");
		if (!(termfeatures & TERM_CAN_CLEAR))
			fprintf(stderr, "\tClear screen\n");
		if (!(termfeatures & TERM_CAN_CLREOL))
			fprintf(stderr, "\tClear to end-of-line\n");
		if (!(termfeatures & TERM_CAN_RIGHT))
			fprintf(stderr, "\tCursor right\n");
		if (!(termfeatures & TERM_CAN_LEFT))
			fprintf(stderr, "\tCursor left\n");
		if (!(termfeatures & TERM_CAN_SCROLL))
			fprintf(stderr, "\tScrolling\n");

		fprintf(stderr, "So we'll be running in dumb mode...\n");
		return -1;
	}

	if (!current_term->TI_cub1)
	{
		if (current_term->TI_kbs)
			current_term->TI_cub1 = current_term->TI_kbs;
		else
			current_term->TI_cub1 = "\b";
	}
	if (!current_term->TI_bel)
		current_term->TI_bel = "\007";

	/*
	 * Set up colors.
	 * Absolute fallbacks are for ansi-type colors
	 */
	for (i = 0; i < 16; i++)
	{
		char cbuf[128];

		*cbuf = 0;
		if (current_term->TI_setaf) 
		    strlcat(cbuf, tparm2(current_term->TI_setaf, i & 0x07, 0), sizeof cbuf);
		else if (current_term->TI_setf)
		    strlcat(cbuf, tparm2(current_term->TI_setf, i & 0x07, 0), sizeof cbuf);
		else
		    snprintf(cbuf, sizeof cbuf, "\033[%dm", (i & 0x07) + 30);

		current_term->TI_forecolors[i] = malloc_strdup(cbuf);

		*cbuf = 0;
		if (current_term->TI_setab)
		    strlcat(cbuf, tparm2(current_term->TI_setab, i & 0x07, 0), sizeof cbuf);
		else if (current_term->TI_setb)
		    strlcat(cbuf, tparm2(current_term->TI_setb, i & 0x07, 0), sizeof cbuf);
		else
		    snprintf(cbuf, sizeof cbuf, "\033[%dm", (i & 0x07) + 40);

		current_term->TI_backcolors[i] = malloc_strdup(cbuf);
	}

	/* 
	 * These are the silly "aixterm" colors for emulators that are 
	 * too stubborn to emulate bold+color like real emulators do
	 */
	for (i = 0; i < 8; i++)
	{
		char cbuf[128];

		*cbuf = 0;
		snprintf(cbuf, sizeof cbuf, "\033[9%dm", i);
		current_term->TI_bold_forecolors[i] = malloc_strdup(cbuf);

		*cbuf = 0;
		snprintf(cbuf, sizeof cbuf, "\033[10%dm", i);
		current_term->TI_bold_backcolors[i] = malloc_strdup(cbuf);
	}

	/* I don't know any other way to set 256 colors, so ..... */
	for (i = 16; i < 256; i++)
	{
		char cbuf[128];

		*cbuf = 0;
		snprintf(cbuf, sizeof cbuf, "\033[38;5;%dm", i);
		current_term->TI_forecolors[i] = malloc_strdup(cbuf);

		*cbuf = 0;
		snprintf(cbuf, sizeof cbuf, "\033[48;5;%dm", i);
		current_term->TI_backcolors[i] = malloc_strdup(cbuf);
	}

/* Set up the terminal discipline */
	tty_des = 0;			/* Has to be. */
	tcgetattr(tty_des, &oldb);

	/*
	 * So by default, the kernel intercepts a great deal of keypresses
	 * and does in-kernel input editing.  These keypresses are handled
	 * by the kernel, and the result is a line of input which is then 
	 * passed to the application.  Those special keypresses are never
	 * sent to the application.
	 *
	 * So we need to turn all that off, since we need to be able to 
	 * bind anything.
	 */

	/*
	 * Turning off ICANON tells the kernel not to buffer input from
	 * the user, but to pass it on to us directly.  Further, it tells
	 * the kernel not to process the EOF (^D), EOL (^J), ERASE (^?)
	 * and KILL (^U) keys, but to pass them on.
	 *
	 * Turning off ECHO tells the kernel not to output characters as
	 * they are typed; we take responsibility for that.
	 */
	newb = oldb;
	newb.c_lflag &= ~(unsigned int)(ICANON | ECHO); /* set equ. of CBREAK, no ECHO */

	/*
	 * Turning off IEXTEN tells the kernel not to honor any posix 
	 * extenstions it might know about.  On 4.4BSD, this tells the 
	 * kernel not to withhold the EOL2 (undefined), WERASE (^W), 
	 * REPRINT (^R), LNEXT (^V), DISCARD (^O), and STATUS (^T) 
	 * characters from us.  You wouldn't be able to /bind those keys
	 * if this was left on.
	 */
	newb.c_lflag &= ~(unsigned int)(IEXTEN);  /* Turn off DISCARD/LNEXT */

	/*
	 * Since we turned off ICANON, we have to tell the kernel how
	 * we want the read() call to be satisifed.  We want read() to
	 * return 0 seconds after 1 byte has been received (ie, immediately)
	 */
	newb.c_cc[VMIN] = 1;	          /* read() satified after 1 char */
	newb.c_cc[VTIME] = 0;	          /* No timer */

	/*
	 * Next, we have to reclaim all of the signal-generating keys.
	 * Normally you would do that by turning off ISIG, but we can't do
	 * that, because we want ^C to generate SIGINT; that's how the user
	 * can bail out of infinite recursion.
	 *
	 * So we have to actually turn off all of the signal-generating
	 * keys *except* ^C by hand:  QUIT (^\), DSUSP (^Y), and SUSP (^Z).
	 */
#       if !defined(_POSIX_VDISABLE)
#               if defined(HAVE_FPATHCONF)
#                       define _POSIX_VDISABLE fpathconf(tty_des, _PC_VDISABLE)
#               else
#                       define _POSIX_VDISABLE 0
#               endif
#       endif

	newb.c_cc[VQUIT] = _POSIX_VDISABLE;
#	ifdef VDSUSP
		newb.c_cc[VDSUSP] = _POSIX_VDISABLE;
# 	endif
#	ifdef VSUSP
		newb.c_cc[VSUSP] = _POSIX_VDISABLE;
#	endif

	/*
	 * Now we have to turn off hardware flow control, in order to reclaim
	 * the ^S (XOFF) and ^Q (XON) keys.  Hardware flow control is evil,
	 * because while the flow control is off, epic will block when it
	 * tries to output something, which could lead to you pinging out 
	 * all your servers.
	 */
	newb.c_iflag &= ~(unsigned long)(IXON | IXOFF);	/* No XON/XOFF */

#if use_alt_screen
	/*
	 * Some terminal emulators have two screens; the primary screen,
	 * and the alternate screen.  Applications can optionally switch
	 * to the alternate screen at startup, and then switch back to the
	 * primary screen at shutdown.  I find this feature to be intensely
	 * obnoxious.  It's here, if you want to #define it.
	 */
	if (current_term->TI_smcup)
		tputs_x(current_term->TI_smcup);
#endif

	/*
	 * When you write a character to the last column of a line, most
	 * terminal emulators will advance the cursor to the start of the
	 * next line ("automargins") but some will leave the cursor at the end
	 * of the line ("no automargins").  It's not always obvious which
	 * state you're in, and it's not always obvious whether the emulator
	 * supports one, or both modes.
	 *
	 * The client avoids all this by refusing to use the last column.  
	 * But if you want to, you can define this and epic will turn off 
	 * auto-margins and will attempt to use the final column in each line.
	 * There are no guarantees it will work, since some emulators say they 
	 * support turning off auto-margins, but they lie.
	 */
	term_establish_last_column();

	/*
	 * Next we tell the kernel that it should support 8 bits both
	 * coming in and going out, and it should not strip the high bit
	 * from any keypress.
	 */
	newb.c_cflag |= CS8;
	newb.c_iflag &= ~(unsigned long)ISTRIP;

	/* Commit our changes and we're done! */
	tcsetattr(tty_des, TCSADRAIN, &newb);
	return 0;
}


/*
 * term_resize: gets the terminal height and width.  Trys to get the info
 * from the tty driver about size, if it can't... uses the termcap values. If
 * the terminal size has changed since last time term_resize() has been
 * called, 1 is returned.  If it is unchanged, 0 is returned. 
 */
int	term_resize (void)
{
	static	int	old_li = -1,
			old_co = -1;

#	if defined (TIOCGWINSZ)
	{
		struct winsize window;

		/*
		 * This hack is required by a race condition within screen;
		 * if you have a "caption always" bar, when you reattach to
		 * a session, it will send us a SIGWINCH, before it has 
		 * accounted for the "caption bar" stealing a line from the
		 * screen.  So we race screen, and if we ask for the size of
		 * the screen before it accounts for the caption bar, then
		 * we lose, because we'll get the wrong number of lines, and
		 * that will screw up the status bar.  So we do this very
		 * small sleep to increase the chances of us losing the race,
		 * which means we win.  Got it?
		 *
		 * P.S. -- GNU Screen is all kinds of icky.
		 */
		my_sleep(0.05);

		if (ioctl(tty_des, TIOCGWINSZ, &window) < 0)
		{
			current_term->TI_lines = li;
			current_term->TI_cols = co;
		}
		else
		{
			if ((current_term->TI_lines = window.ws_row) == 0)
				current_term->TI_lines = li;
			if ((current_term->TI_cols = window.ws_col) == 0)
				current_term->TI_cols = co;
		}
	}
#	else
	{
		current_term->TI_lines = li;
		current_term->TI_cols = co;
	}
#	endif

	term_establish_last_column();
	if ((old_li != current_term->TI_lines) || (old_co != current_term->TI_cols))
	{
		old_li = current_term->TI_lines;
		old_co = current_term->TI_cols;
		if (main_screen >= 0)
			set_screen_lines(main_screen, current_term->TI_lines);
		if (main_screen >= 0)
			set_screen_columns(main_screen, current_term->TI_cols);
		return 1;
	}
	return 0;
}

static	void	term_establish_last_column (void)
{
	if (get_int_var(AUTOMARGIN_OVERRIDE_VAR))
		term_enable_last_column();
	else if (current_term->TI_am && current_term->TI_rmam)
		term_enable_last_column();
	else
	{
		term_disable_last_column();
		current_term->TI_cols--;
	}
}

static void	term_enable_last_column (void)
{
	if (current_term->TI_am && current_term->TI_rmam)
		tputs_x(current_term->TI_rmam);
}

static void	term_disable_last_column (void)
{
	if (current_term->TI_am && current_term->TI_smam)
		tputs_x(current_term->TI_smam);
}

/* term_CE_clear_to_eol(): the clear to eol function, right? */
void	term_clreol	(void)
{
	tputs_x(current_term->TI_el);
	return;
}


void	term_beep (void)
{
	if (get_int_var(BEEP_VAR) && global_beep_ok)
	{
		tputs_x(current_term->TI_bel);
		term_flush();
	}
}

void	set_automargin_override (void *stuff)
{
	if (dumb_mode)
		return;

	/*
	 * This is called whenever you /set automargin_override,
	 * either way.  
	 *
	 * ON - The client should assume it can use the last column
	 *	no matter what the circumstances appear to support
	 * OFF - The client should use (or not use) the last column
	 *	based on whether it appears to be safe to do so.
	 */
	term_establish_last_column();
	need_redraw = 1;
}


/* Set the cursor position */
void	term_gotoxy (int col, int row)
{
	if (current_term->TI_cup)
		tputs_x(tparm2(current_term->TI_cup, row, col));
	else
	{
		tputs_x(tparm1(current_term->TI_hpa, col));
		tputs_x(tparm1(current_term->TI_vpa, row));
	}
}

/* A no-brainer. Clear the screen. */
void	term_clrscr (void)
{
	int i;

	/* We have a specific cap for clearing the screen */
	if (current_term->TI_clear)
	{
		tputs_x(current_term->TI_clear);
		term_gotoxy (0, 0);
		return;
	}

	term_gotoxy (0, 0);
	/* We can clear by doing an erase-to-end-of-display */
	if (current_term->TI_ed)
	{
		tputs_x (current_term->TI_ed);
		return;
	}
	/* We can also clear by deleteing lines ... */
	else if (current_term->TI_dl)
	{
		tputs_x(tparm1(current_term->TI_dl, current_term->TI_lines));
		return;
	}
	/* ... in this case one line at a time */
	else if (current_term->TI_dl1)
	{
		for (i = 0; i < current_term->TI_lines; i++)
			tputs_x (current_term->TI_dl1);
		return;
	}
	/* As a last resort we can insert lines ... */
	else if (current_term->TI_il)
	{
		tputs_x (tparm1(current_term->TI_il, current_term->TI_lines));
		term_gotoxy (0, 0);
		return;
	}
	/* ... one line at a time */
	else if (current_term->TI_il1)
	{
		for (i = 0; i < current_term->TI_lines; i++)
			tputs_x (current_term->TI_il1);
		term_gotoxy (0, 0);
	}
}

/*
 * Move the cursor NUM spaces to the right
 */
void	term_right (int num)
{
	if (num == 1 && current_term->TI_cuf1)
		tputs_x(current_term->TI_cuf1);
	else if (current_term->TI_cuf)
		tputs_x (tparm1(current_term->TI_cuf, num));
	else if (current_term->TI_mrcup)
		tputs_x (tparm2(current_term->TI_mrcup, num, 0));
	else if (current_term->TI_cuf1)
		while (num--)
			tputs_x(current_term->TI_cuf1);
}

/*
 * Scroll the screen N lines between lines TOP and BOT.
 */
void	term_scroll (int top, int bot, int n)
{
	int i,oneshot=0,rn,sr,er;
	char thing[128], final[128], start[128];

	/* Some basic sanity checks */
	if (n == 0 || top == bot || bot < top)
		return;

	sr = er = 0;
	final[0] = start[0] = thing[0] = 0;

	if (n < 0)
		rn = -n;
	else
		rn = n;

	/*
	 * First thing we try to do is set the scrolling region on a 
	 * line granularity.  In order to do this, we need to be able
	 * to have termcap 'cs', and as well, we need to have 'sr' if
	 * we're scrolling down, and 'sf' if we're scrolling up.
	 */
	if (current_term->TI_csr && (current_term->TI_ri || current_term->TI_rin) && (current_term->TI_ind || current_term->TI_indn))
	{
		/*
		 * Previously there was a test to see if the entire scrolling
		 * region was the full screen.  That test *always* fails,
		 * because we never scroll the bottom line of the screen.
		 */
		strlcpy(start, tparm2(current_term->TI_csr, top, bot), sizeof start);
		strlcpy(final, tparm2(current_term->TI_csr, 0, current_term->TI_lines-1), sizeof final);

		if (n > 0)
		{
			sr = bot;
			er = top;
			if (current_term->TI_indn)
			{
				oneshot = 1;
				strlcpy(thing, tparm2(current_term->TI_indn, rn, rn), sizeof thing);
			}
			else
				strlcpy(thing, current_term->TI_ind, sizeof thing);
		}
		else
		{
			sr = top;
			er = bot;
			if (current_term->TI_rin)
			{
				oneshot = 1;
				strlcpy(thing, tparm2(current_term->TI_rin, rn, rn), sizeof thing);
			}
			else
				strlcpy(thing, current_term->TI_ri, sizeof thing);
		}
	}

	else if (current_term->TI_wind && (current_term->TI_ri || current_term->TI_rin) && (current_term->TI_ind || current_term->TI_indn))
	{
		strlcpy(start, tparm4(current_term->TI_wind, top, bot, 0, current_term->TI_cols-1), sizeof start);
		strlcpy(final, tparm4(current_term->TI_wind, 0, current_term->TI_lines-1, 0, current_term->TI_cols-1), sizeof final);

		if (n > 0)
		{
			sr = bot;
			er = top;
			if (current_term->TI_indn)
			{
				oneshot = 1;
				strlcpy(thing, tparm2(current_term->TI_indn, rn, rn), sizeof thing);
			}
			else
				strlcpy(thing, current_term->TI_ind, sizeof thing);
		}
		else
		{
			sr = top;
			er = bot;
			if (current_term->TI_rin)
			{
				oneshot = 1;
				strlcpy(thing, tparm2(current_term->TI_rin, rn, rn), sizeof thing);
			}
			else
				strlcpy(thing, current_term->TI_ri, sizeof thing);
		}
	}

	else if ((current_term->TI_il || current_term->TI_il1) && (current_term->TI_dl || current_term->TI_dl1))
	{
		if (n > 0)
		{
			sr = top;
			er = bot;

			if (current_term->TI_dl)
			{
				oneshot = 1;
				strlcpy(thing, tparm2(current_term->TI_dl, rn, rn), sizeof thing);
			}
			else
				strlcpy(thing, current_term->TI_dl1, sizeof thing);

			if (current_term->TI_il)
			{
				oneshot = 1;
				strlcpy(final, tparm2(current_term->TI_il, rn, rn), sizeof final);
			}
			else
				strlcpy(final, current_term->TI_il1, sizeof final);
		}
		else
		{
			sr = bot;
			er = top;
			if (current_term->TI_il)
			{
				oneshot = 1;
				strlcpy(thing, tparm2(current_term->TI_il, rn, rn), sizeof thing);
			}
			else
				strlcpy(thing, current_term->TI_il1, sizeof thing);

			if (current_term->TI_dl)
			{
				oneshot = 1;
				strlcpy(final, tparm2(current_term->TI_dl, rn, rn), sizeof thing);
			}
			else
				strlcpy(final, current_term->TI_dl1, sizeof thing);
		}
	}


	if (!thing[0])
		return;

	/* Do the actual work here */
	if (start[0])
		tputs_x (start);
	term_gotoxy (0, sr);

	if (oneshot)
		tputs_x (thing);
	else
	{
		for (i = 0; i < rn; i++)
			tputs_x(thing);
	}
	term_gotoxy (0, er);
	if (final[0])
		tputs_x(final);
}

/*
 * term_getsgr(int opt, int fore, int back)
 * Return the string required to set the given mode. OPT defines what
 * we really want it to do. It can have these values:
 * TERM_SGR_BOLD_ON     - turn bold mode on
 * TERM_SGR_BOLD_OFF    - turn bold mode off
 * TERM_SGR_BLINK_ON    - turn blink mode on
 * TERM_SGR_BLINK_OFF   - turn blink mode off
 * TERM_SGR_UNDL_ON     - turn underline mode on
 * TERM_SGR_UNDL_OFF    - turn underline mode off
 * TERM_SGR_REV_ON      - turn reverse video on
 * TERM_SGR_REV_OFF     - turn reverse video off
 * TERM_SGR_NORMAL      - turn all attributes off
 * TERM_SGR_RESET       - all attributes off and back to default colors
 * TERM_SGR_FOREGROUND  - set foreground color
 * TERM_SGR_BACKGROUND  - set background color
 * TERM_SGR_COLORS      - set foreground and background colors
 * TERM_SGR_GCHAR       - print graphics character
 *
 * The colors are defined as:
 * 0    - black
 * 1    - red
 * 2    - green
 * 3    - brown
 * 4    - blue
 * 5    - magenta
 * 6    - cyan
 * 7    - white
 * 8    - grey (foreground only)
 * 9    - bright red (foreground only)
 * 10   - bright green (foreground only)
 * 11   - bright yellow (foreground only)
 * 12   - bright blue (foreground only)
 * 13   - bright magenta (foreground only)
 * 14   - bright cyan (foreground only)
 * 15   - bright white (foreground only)
 */
const char *	term_getsgr (int opt, int fore, int back)
{
	const char *ret = empty_string;

	switch (opt)
	{
		case TERM_SGR_BOLD_ON:
		case TERM_SGR_BOLD_OFF:
		case TERM_SGR_BLINK_OFF:
		case TERM_SGR_BLINK_ON:
		case TERM_SGR_UNDL_ON:
		case TERM_SGR_UNDL_OFF:
		case TERM_SGR_REV_ON:
		case TERM_SGR_REV_OFF:
		case TERM_SGR_NORMAL:
		case TERM_SGR_RESET:
			ret = current_term->TI_sgrstrs[opt-1];
			break;
		case TERM_SGR_FOREGROUND:
			ret = current_term->TI_forecolors[fore & 0x0f];
			break;
		case TERM_SGR_BACKGROUND:
			ret = current_term->TI_backcolors[fore & 0x0f];
			break;
		case TERM_SGR_GCHAR:
			if (current_term->TI_dispc)
				ret = tparm1(current_term->TI_dispc, fore);
			break;
		default:
			panic(1, "Unknown option '%d' to term_getsgr", opt);
			break;
	}
	return (ret);
}

static char *	control_mangle (char *text)
{
static 	char	retval[256];
	int 	pos = 0;
	
	*retval = 0;
	if (!text)
		return retval;
		
	for (; *text && (pos < 254); text++, pos++)
	{
		if ((unsigned char)*text < 32) 
		{
			retval[pos++] = '^';
			retval[pos] = *text + 64;
		}
		else if ((unsigned char)*text == 127)
		{
			retval[pos++] = '^';
			retval[pos] = '?';
		}
		else
			retval[pos] = *text;
	}

	retval[pos] = 0;
	return retval;
}

const char *	get_term_capability (const char *name, int querytype, int mangle)
{
static	char		retval[128];
	const char *	compare = empty_string;
	int 		x;
	const cap2info *t;

	for (x = 0; x < numcaps; x++) 
	{
		t = &tcaps[x];
		if (querytype == 0)
			compare = t->longname;
		else if (querytype == 1)
			compare = t->iname;
		else if (querytype == 2)
			compare = t->tname;

		if (!strcmp(name, compare)) 
		{
		    switch (t->type) 
		    {
			case CAP_TYPE_BOOL:
			case CAP_TYPE_INT:
				if (!(int *)t->ptr)
					return NULL;
				strlcpy(retval, ltoa(* (int *)(t->ptr)), sizeof retval);
				return retval;
			case CAP_TYPE_STR:
				if (!(char **)t->ptr || !*(char **)t->ptr)
					return NULL;
				strlcpy(retval, mangle ? 
					control_mangle(*(char **)t->ptr) :
						      (*(char **)t->ptr), sizeof retval);
				return retval;
		    }
		}
	}
	return NULL;	
}

