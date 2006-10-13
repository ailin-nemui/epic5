/* $EPIC: keys.c,v 1.51 2006/10/13 21:58:02 jnelson Exp $ */
/*
 * keys.c:  Keeps track of what happens whe you press a key.
 *
 * Copyright © 2002 EPIC Software Labs.
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
#include "config.h"
#include "commands.h"
#include "functions.h"
#include "hook.h"
#include "ircaux.h"
#include "input.h"
#include "keys.h"
#include "list.h"
#include "names.h"
#include "output.h"
#include "screen.h"
#include "stack.h"
#include "term.h"
#include "vars.h"
#include "window.h"
#include "timer.h"
#include "reg.h"

/* This is a typedef for a function used with the /BIND system.  The
 * functions all live in input.c.  Following it is a macro to quickly define
 * functions to handle keybindings. */
typedef void (*BindFunction) (unsigned char, unsigned char *);

/* This is the structure used to hold binding functions. */
struct Binding {
    struct Binding *next;   /* linked-list stuff. :) */

    char    *name;          /* the name of this binding */
    BindFunction func;      /* function to use ... */
    char    *alias;         /* OR alias to call.  one or the other. */
    char    *filename;      /* the package which added this binding */
};
extern struct Binding *binding_list; /* list head for keybindings */

struct Binding  *add_binding    (const char *, BindFunction, char *);
void            remove_binding  (char *);
struct Binding *find_binding    (const char *);

#define KEYMAP_SIZE 256
struct Key {
    unsigned char val;  /* the key value */
    unsigned char changed; /* set if this binding was changed post-startup */
    struct Binding *bound; /* the function we're bound to. */
    struct Key *map;    /* a map of subkeys (may be NULL) */
    struct Key *owner;  /* the key which contains the map we're in. */
    char    *stuff;     /* 'stuff' associated with our binding */
    char    *filename;  /* the package which added this binding */
};

extern struct Key *head_keymap; /* the head keymap.  the root of the keys.  */
static int bind_compressed_string (unsigned char *cs, int slen, const char *bindstr, char *args);

/* This file is split into two pieces.  The first piece represents bindings.
 * Bindings are now held in a linked list, allowing the user to add new ones
 * at will.  Several management functions are placed here to add/remove
 * bindings, and the default pre-packaged bindings are placed in the
 * init_binds() function. */

/* (From the author):  The following things bother me about this code:
 *
 * #1:  I reuse the code from show_all_bindings in various forms all over
 *      the place.  It might be better to write small functions, and one
 *      'recurse_keys' function which is passed those small functions.
 * #2:  This file is very disorganized and messy.
 */
/* * * * * * * * * * * * * * * BIND SECTION * * * * * * * * * * * * * * * * */

struct Binding *binding_list;

/* Add a binding.  A binding must have either a function, alias, or neither,
 * but never both.  If no binding with this name exists, we create a new one
 * and fill in the details, then add it to the list of available bindings in
 * the client.  Otherwise, we yell and go home. */
struct Binding *add_binding (const char *name, BindFunction func, char *alias) {
    struct Binding *bp;
    if (func && alias) {
	yell("add_binding(): func and alias both defined!");
	return NULL;
    }
    if (!name)
	return NULL; /* no binding name. */

    bp = find_binding(name);
    if (bp) {
	yell("binding %s already exists!", name);
	return NULL;
    }

    bp = new_malloc(sizeof(struct Binding));
    bp->name = malloc_strdup(name);
    if (alias) {
	bp->alias = malloc_strdup(alias);
	bp->func = NULL;
    } else {
	bp->func = func;
	bp->alias = NULL;
    }
    bp->filename = NULL;
    if (current_package())
        bp->filename = malloc_strdup(current_package());

    add_to_list((List **)&binding_list, (List *)bp);

    return bp;
}

static void remove_bound_keys(struct Key *, struct Binding *);
void remove_binding (char *name) {
    struct Binding *bp;

    if (!name)
	return;

    bp = (struct Binding *)remove_from_list((List **)&binding_list, name);
    if (bp) {
        /* be sure to remove any keys bound to this binding first */
        remove_bound_keys(head_keymap, bp);

	new_free(&bp->name);
	if (bp->alias)
	    new_free(&bp->alias);
	if (bp->filename)
	    new_free(&bp->filename);
	new_free(&bp);
    }

    return;
}

static void remove_bound_keys (struct Key *map, struct Binding *binding) {
    int c;

    for (c = 0; c <= KEYMAP_SIZE - 1;c++) {
	if (map[c].bound == binding)
            map[c].bound = NULL;
	if (map[c].map)
            remove_bound_keys(map[c].map, binding);
    }
}

struct Binding *find_binding (const char *name) {
    if (!name)
	return NULL;

    return (struct Binding *)find_in_list((List **)&binding_list, name, 0);
}

void init_binds (void) {
#define ADDBIND(x, y) add_binding(x, y, NULL);
    /* there is no 'NOTHING' bind anymore. */
    ADDBIND("ALTCHARSET",		    insert_altcharset		    );
    ADDBIND("BACKSPACE",		    input_backspace		    );
    ADDBIND("BACKWARD_CHARACTER",	    backward_character		    );
    ADDBIND("BACKWARD_WORD",		    input_backward_word		    );
    ADDBIND("BEGINNING_OF_LINE",	    input_beginning_of_line	    );
    ADDBIND("BLINK",			    insert_blink		    );
    ADDBIND("BOLD",			    insert_bold			    );
    ADDBIND("CLEAR_SCREEN",		    clear_screen		    );
    ADDBIND("CPU_SAVER",		    cpu_saver_on		    );
    ADDBIND("DELETE_CHARACTER",		    input_delete_character	    );
    ADDBIND("DELETE_NEXT_WORD",		    input_delete_next_word	    );
    ADDBIND("DELETE_PREVIOUS_WORD",	    input_delete_previous_word	    );
    ADDBIND("DELETE_TO_PREVIOUS_SPACE",	    input_delete_to_previous_space  );
    ADDBIND("END_OF_LINE",		    input_end_of_line		    );
    ADDBIND("ERASE_LINE",		    input_clear_line		    );
    ADDBIND("ERASE_TO_BEG_OF_LINE",	    input_clear_to_bol		    );
    ADDBIND("ERASE_TO_END_OF_LINE",	    input_clear_to_eol		    );
    ADDBIND("FORWARD_CHARACTER",	    forward_character		    );
    ADDBIND("FORWARD_WORD",		    input_forward_word		    );
    ADDBIND("HIGHLIGHT_OFF",		    highlight_off		    );
    ADDBIND("NEXT_WINDOW",		    next_window			    );
    ADDBIND("PARSE_COMMAND",		    parse_text			    );
    ADDBIND("PREVIOUS_WINDOW",		    previous_window		    );
    ADDBIND("QUIT_IRC",			    irc_quit			    );
    ADDBIND("QUOTE_CHARACTER",		    quote_char			    );
    ADDBIND("REFRESH_INPUTLINE",	    refresh_inputline		    );
    ADDBIND("REFRESH_SCREEN",		    (BindFunction) refresh_screen   );
    ADDBIND("REFRESH_STATUS",		    (BindFunction) update_all_status);
    ADDBIND("RESET_LINE",		    input_reset_line		    );
    ADDBIND("REVERSE",			    insert_reverse		    );
    ADDBIND("SCROLL_BACKWARD",		    scrollback_backwards	    );
    ADDBIND("SCROLL_END",		    scrollback_end		    );
    ADDBIND("SCROLL_FORWARD",		    scrollback_forwards		    );
    ADDBIND("SCROLL_START",		    scrollback_start		    );
    ADDBIND("SELF_INSERT",		    input_add_character		    );
    ADDBIND("SEND_LINE",		    send_line			    );
    ADDBIND("STOP_IRC",			    term_pause			    );
    ADDBIND("SWAP_LAST_WINDOW",		    swap_last_window		    );
    ADDBIND("SWAP_NEXT_WINDOW",		    swap_next_window		    );
    ADDBIND("SWAP_PREVIOUS_WINDOW",	    swap_previous_window	    );
    ADDBIND("SWITCH_CHANNELS",		    switch_channels		    );
    ADDBIND("SWITCH_QUERY",		    switch_query		    );
    ADDBIND("TOGGLE_INSERT_MODE",	    toggle_insert_mode		    );
    ADDBIND("TOGGLE_STOP_SCREEN",	    toggle_stop_screen		    );
    ADDBIND("TRANSPOSE_CHARACTERS",	    input_transpose_characters	    );
    ADDBIND("TYPE_TEXT",		    type_text			    );
    ADDBIND("UNCLEAR_SCREEN",		    input_unclear_screen	    );
    ADDBIND("UNDERLINE",		    insert_underline		    );
    ADDBIND("UNSTOP_ALL_WINDOWS",	    unstop_all_windows		    );
    ADDBIND("YANK_FROM_CUTBUFFER",	    input_yank_cut_buffer	    );
#undef ADDBIND
}

/* * * * * * * * * * * * * * * KEYS SECTION * * * * * * * * * * * * * * * * */

/* Keys support is below here.  We have functions to add and remove
 * bindings, as well as get the binding for a key in a current input
 * sequence, or a string of keys. */

struct Key	*construct_keymap	(struct Key *);
int		clean_keymap		(struct Key *);
unsigned char	*bind_string_compress	(const unsigned char *, int *);
unsigned char	*bind_string_decompress	(unsigned char *, const unsigned char *, int);
static int	bind_string		(const u_char *, const char *, char *);
struct Key	*find_sequence		(const unsigned char *, int);
void		show_all_bindings	(struct Key *, const unsigned char *, size_t);
void		show_all_rbindings	(struct Key *, const unsigned char *, int, struct Binding *);
void		show_key		(struct Key *, const unsigned char *, int, int);

/* this is set when we're post-init to keep track of changed keybindings. */
unsigned char bind_post_init = 0;
struct Key *head_keymap;

/* this function is used to actually execute the binding for a specific key.
 * it checks to see if the key needs to call an alias or a function, and
 * then makes the appropriate call.  if the key is not bound to any action
 * at all, assume we were called as part of a timeout or a terminator on a
 * sequence that didn't resolve.  if that is the case, use the special
 * 'key_exec_bt' function to walk backwards along the line and execute the
 * keys as if they were individually pressed. */
void key_exec_bt (struct Key *);
static void key_exec (struct Key *key) {

    if (key == NULL) {
	yell("key_exec(): called with NULL key!");
	return; /* nothing to do. */
    }

    /* if the key isn't bound to anything, and it has an owner, assume we
     * got a premature terminator for one or more key sequences.  call
     * key_exec_bt to go back and see about executing the input in smaller
     * chunks. */
    if (key->bound == NULL) {
	if (key->owner != NULL)
	    key_exec_bt(key);
	return;
    }

    if (do_hook(KEYBINDING_LIST, "%s 0 %d", key->bound->name, (int)key->val))
    {
	    /* check alias first, then function */
	    if (key->bound->alias != NULL) {
		/* I don't know if this is right ... */
		char *exec = malloc_strdup(key->bound->alias);
		if (key->stuff)
		    malloc_strcat_wordlist(&exec, " ", key->stuff);
		call_lambda_command("KEY", exec, empty_string);
		new_free(&exec);
	    } else if (key->bound->func != NULL)
		key->bound->func(key->val, key->stuff);
    }
    return;
}
	
/* this function unwinds the current 'stack' of input keys, placing them
 * into a string, and then parses the string looking for the longest
 * possible input combinations and executing them as it goes. */
void key_exec_bt (struct Key *key) {
    const unsigned char *kstr = (const unsigned char *)empty_string;
    const unsigned char *nstr;
    int len = 1, kslen;
    struct Key *kp;

    /* now walk backwards, growing kstr as necessary */
    while (key != NULL) {
        unsigned char *buf;

	buf = alloca(len + 1);
	memcpy(buf + 1, kstr, len);
	buf[0] = key->val;
	kstr = buf;
	len++;
	key = key->owner;
    }

    /* kstr should contain our keystring now, so walk along it and find the
     * longest patterns available *that terminate*.  what this means is that
     * we need to go backwards along kstr until we find something that
     * terminates, then we need to lop off that part of kstr, and start
     * again.  this is not particularly efficient. :/ */
    kslen = len;
    while (*kstr) {
	kp = NULL;
	nstr = kstr;
	kslen--;
	while (nstr != (kstr + kslen)) {
	    if (nstr == kstr) /* beginning of string */
		kp = &head_keymap[*nstr];
	    else if (kp->map != NULL)
		kp = &kp->map[*nstr];
	    else
		break; /* no luck here */
	    nstr++;
	}
	/* did we get to the end?  if we did and found a key that executes,
	 * go ahead and execute it.  if kslen is equal to 1, and we didn't
	 * have any luck, simply discard the key and continue plugging
	 * forward. */
	if (nstr == (kstr + kslen)) {
	    if (kp->bound != NULL || kslen == 1) {
		if (kp->bound != NULL)
		    key_exec(kp);
		len -= (nstr - kstr);
		kslen = len; 
		kstr = nstr; /* move kstr forward */
		continue; /* now move along */
	    }
	}
	/* otherwise, we'll just continue above (where kslen is decremented
	 * and nstr is re-set */
    }
}

/* this function tries to retrieve the binding for a key pressed on the
 * input line.  depending on the circumstances, we may need to execute the
 * previous key's action (if there has been a timeout).  The timeout factor
 * is set in milliseconds by the KEY_INTERVAL variable.  See further for
 * instructions. :) */
void *	handle_keypress (void *lastp, Timeval pressed, unsigned char key) {
    struct Key *kp, *last;

    /* we call the timeout code here, too, just to be safe. */
    last = timeout_keypress(lastp, pressed);

    /* if last is NULL (meaning we're in a fresh state), pull from the head
     * keymap.  if last has a map, pull from that map.  if last has no map,
     * something went wrong (we should never return a 'last' that is
     * mapless!) */
    if (last == NULL)
	kp = &head_keymap[key];
    else if (last->map != NULL)
	kp = &last->map[key];
    else {
	yell("handle_keypress(): last is not NULL but has no map!");
	return NULL;
    }

    /* If there is a map and a keybinding, schedule a timeout */
    if (kp->map && kp->bound)
	add_timer(0, empty_string, get_int_var(KEY_INTERVAL_VAR) / 1000.0, 1,
			do_input_timeouts, NULL, NULL, GENERAL_TIMER, -1, 0);

    /* if the key has a map associated, we can't automatically execute the
     * action.  return kp and wait quietly. */
    if (kp->map != NULL)
	return (void *)kp;

    /* otherwise, we can just exec our key and return nothing. */
    key_exec(kp);
    return NULL;
}

void *	timeout_keypress (void *lastp, Timeval pressed) {
    int mpress = 0; /* ms count since last pressing */
    Timeval tv;
    Timeval right_now;
    struct Key *last;

    if (lastp == NULL)
	return NULL; /* fresh state, we need not worry about timeouts */

    last = (struct Key *)lastp;
    if (last->bound == NULL)
	return (void *)last; /* wait unconditionally if this key is unbound. */

    get_time(&right_now);
    tv = time_subtract(pressed, right_now);
    mpress = tv.tv_sec * 1000;
    mpress += tv.tv_usec / 1000;

    if (mpress >= get_int_var(KEY_INTERVAL_VAR)) {
	/* we timed out.  if the last key had some action associated,
	 * execute that action. */
	key_exec(last);
	return NULL; /* we're no longer waiting on this key's map */
    }
    return (void *)last; /* still waiting.. */
}

struct Key *construct_keymap (struct Key *owner) {
    int c;
    struct Key *map = new_malloc(sizeof(struct Key) * KEYMAP_SIZE);

    for (c = 0;c <= KEYMAP_SIZE - 1;c++) {
	map[c].val = c;
	map[c].bound = NULL;
	map[c].map = NULL;
	map[c].owner = owner;
	map[c].stuff = NULL;
	map[c].filename = NULL;
    }

    return map;
}

/* this function recursively 'cleans' keymaps.  which is to say, if a map
 * has no viable members, it will destroy the map, but not before it calls
 * itself on sub-maps.  this should be used whenever keys have been unbound
 * to keep memory clear and (more importantly) to make sure that artifacts
 * are not left around in the timeout system.  the function returns positive
 * if *the map passed* was removed, negative otherwise. */
int clean_keymap (struct Key *map) {
    int	c;
    int save = 0;

    /* walk through the map to see if things are in use.  if something is
     * bound, the keymap will be saved.  also if a key has a submap and that
     * submap cannot be cleaned, the keymap will be saved.  we return 1 if
     * the map is saved, 0 otherwise.  we walk through all keys here, even
     * if we know we're going to save the map early on.  this allows the
     * cleaner to catch dead submaps in the current map. */
    for (c = 0; c <= KEYMAP_SIZE - 1;c++) {
	if (map[c].bound)
	    save = 1; /* key in use.  save map. */
	if (map[c].map) {
	    if (clean_keymap(map[c].map))
		save = 1; /* map still in use. */
	    else
		map[c].map = NULL; /* map destroyed, make sure to unlink. */
	}
    }

    if (!save)
	new_free(&map); /* free the memory. */
    return save;
}

/* this function compresses a user-input string of key sequences into
 * something interally useable.  the following notations are supported:
 * 
 * ^C (or ^c): control-character (c - 64).  if 'c' is not >= 64, this
 * is treated as a literal sequence of ^ and then c.  If 'c' is '?', we
 * treat the sequence as \177 (the DEL sequence, ascii 127, etc. :)
 *
 * \X: where X may (or may not) have special meaning.  the following may be
 * useful as common shorthand:
 * \e: equivalent to ^[ (escape)
 * \xxx: octal sequence.
 * \^: escape the caret (hat, whatever.. :)
 * \\: the \ character. ;)
 */
unsigned char *bind_string_compress (const unsigned char *str, int *len) {
    unsigned char *new, *s;
    const unsigned char *oldstr;
    unsigned char c;

#define isoctaln(x) ((x) > 47 && (x) < 56)

    if (!str)
	return NULL;
    s = new = new_malloc(strlen(str) + 1); /* we will always make the string
					      smaller. */

    oldstr = str;
    *len = 0;
    while (*str) {
	switch (*str) {
	    case '^':
		str++; /* pass over the caret */
		if (*str == '?') {
		    s[*len] = '\177'; /* ^? is DEL */
		    *len += 1;
		    str++;
		} else if (toupper(*str) < 64 || toupper(*str) > 95) {
		    s[*len] = '^';
		    *len += 1;
		    /* don't increment, since we're treating this normally. */
		} else {
		    if (isalpha(*str))
			s[*len] = toupper(*str) - 64;
		    else
			s[*len] = *str - 64;
		    *len += 1;
		    str++;
		}
		break;
	    case '\\':
		str++;
		if (isoctaln(*str)) {
		    c = (*str - 48);
		    str++;
		    if (isoctaln(*str)) {
			c *= 8;
			c += (*str - 48);
			str++;
			if (isoctaln(*str)) {
			    c *= 8;
			    c += (*str - 48);
			    str++;
			}
		    }
		    s[*len] = c;
		    *len += 1;
		} else if (*str == 'e')  {
		    s[*len] = '\033'; /* ^[ (escape) */
		    *len += 1;
		    str++;
		} else if (*str) {/* anything else that was escaped */
		    s[*len] = *str++;
		    *len += 1;
		} else {
		    s[*len] = '\\'; /* end-of-string.  no escape. */
		    *len += 1;
		}

		break;
	    default:
		s[*len] = *str++;
		*len += 1;
	}
    }

    s[*len] = '\0';
    if (!*len) {
	yell("bind_string_compress(): sequence [%s] compressed to nothing!",
		oldstr);
	new_free(&new);
	return NULL;
    }
    return new; /* should never be reached! */
}

/* this decompresses a compressed bind string into human-readable form.  it
 * assumes sufficient memory has already been allocated for it. */
unsigned char *	bind_string_decompress (unsigned char *dst, const unsigned char *src, int srclen) 
{
    unsigned char *ret = dst;

    while (srclen) {
	if (*src < 32) {
	    *dst++ = '^';
	    *dst++ = *src + 64;
	} else if (*src == 127) {
	    *dst++ = '^';
	    *dst++ = '?';
	} else
	    *dst++ = *src;
	src++;
	srclen--;
    }
    *dst = '\0';

    return ret;
}

static	int	bind_string (const unsigned char *sequence, const char *bindstr, char *args) 
{
	unsigned char *cs;
	int	slen, retval;

	cs = bind_string_compress(sequence, &slen);
	if (cs == NULL) {
		yell("bind_string(): couldn't compress sequence %s", sequence);
		return 0;
	}

	retval = bind_compressed_string(cs, slen, bindstr, args);
	new_free(&cs);
	return retval;
}

/* this function takes a key sequence (user-input style), a function to bind
 * to, and optionally arguments to that function, and does all the work
 * necessary to bind it.  it will create new keymaps as it goes, if
 * necessary, etc. */
static int bind_compressed_string (unsigned char *cs, int slen, const char *bindstr, char *args) {
    unsigned char *s;
    struct Key *kp = NULL;
    struct Key *map = head_keymap;
    struct Binding *bp = NULL;

    if (!cs || !slen || !bindstr) {
	yell("bind_compressed_string(): called without sequence or bind function!");
	return 0;
    }

    /* nothing (the binding) is special, it's okay if they request
     * 'NOTHING', we just do some other work. */
    if (my_stricmp(bindstr, "NOTHING") && (bp = find_binding(bindstr)) == NULL) {
	say("No such function %s", bindstr);
	return 0;
    }

    s = cs;
    while (slen) {
	kp = &map[*s++];
	slen--;
	if (slen) {
	    /* create a new map if necessary.. */
	    if (kp->map == NULL)
		kp->map = map = construct_keymap(kp);
	     else
		map = kp->map;
	} else {
	    /* we're binding over whatever was here.  check various things
	     * to see if we're overwriting them. */
	    if (kp->stuff)
		new_free(&kp->stuff);
	    if (kp->filename)
		new_free(&kp->filename);
	    kp->bound = bp;
	    kp->changed = bind_post_init;
	    if (bp != NULL) {
		if (args)
		    kp->stuff = malloc_strdup(args);
	        if (current_package())
		    malloc_strcpy(&kp->filename, current_package());
	    }
	}
    }

    /* if we're post-initialization, clean out the keymap with each call. */
    if (bind_post_init)
	clean_keymap(head_keymap);
    return 1;
}

/* this tries to find the key identified by 'seq' which may be uncompressed.
 * if we can find the key bound to this sequence, return it, otherwise
 * return NULL.  If slen is 0, assume this is an uncompressed sequence.  If
 * it is not, assume it was compressed for us. */
struct Key *find_sequence (const unsigned char *seq, int slen) {
    unsigned char *cs = NULL;
    const unsigned char *s;
    struct Key *map = head_keymap;
    struct Key *key = NULL;

    if (!slen) {
	cs = bind_string_compress(seq, &slen);
	if (cs == NULL)
	    return NULL;
	s = cs;
    } else
	s = seq;

    /* we have to find the key.  this should only happen at the top
     * level, otherwise it's not going to act right! */
    while (slen) {
	key = &map[*s++];
	slen--;
	if (slen) {
	    map = key->map;
	    if (map == NULL) {
		new_free(&cs);
		return NULL;
	    }
	}
    }
    if (cs != NULL)
	new_free(&cs);
    return key;
}
    
/* init_keys:  initialize default keybindings that apply without terminal
 * specificity.  we use the above functions to take care of this */
void init_keys (void) {
    int c;
    unsigned char s[2];
    head_keymap = construct_keymap(NULL);

#define BIND(x, y) bind_string(x, y, NULL);
    /* bind characters 32 - 255 to SELF_INSERT. */
    s[1] = '\0';
    for (c = 32;c <= KEYMAP_SIZE - 1;c++) {
	s[0] = (unsigned char )c;
	BIND(s, "SELF_INSERT");
    }

    /* now bind the special single-character inputs */
    BIND("^A", "BEGINNING_OF_LINE");
    BIND("^B", "BOLD");
    BIND("^C", "SELF_INSERT");
    BIND("^D", "DELETE_CHARACTER");
    BIND("^E", "END_OF_LINE");
    BIND("^F", "BLINK");
    BIND("^G", "SELF_INSERT");
    BIND("^H", "BACKSPACE");
    BIND("^I", "TOGGLE_INSERT_MODE");
    BIND("^J", "SEND_LINE");
    BIND("^K", "ERASE_TO_END_OF_LINE");
    BIND("^L", "REFRESH_SCREEN");
    BIND("^M", "SEND_LINE");
    BIND("^O", "HIGHLIGHT_OFF");
    BIND("^Q", "QUOTE_CHARACTER");
    /* ^R */
    BIND("^S", "TOGGLE_STOP_SCREEN");
    BIND("^T", "TRANSPOSE_CHARACTERS");
    BIND("^U", "ERASE_LINE");
    BIND("^V", "REVERSE");
    BIND("^W", "NEXT_WINDOW");
    /* ^X (was META2_CHARACTER) */
    BIND("^Y", "YANK_FROM_CUTBUFFER");
    BIND("^Z", "STOP_IRC");
    /* ^[ (was META1_CHARACTER) */
    /* ^\ */
    /* ^^ */
    BIND("^_", "UNDERLINE");
    /* mind the gap .. */
    BIND("^?", "BACKSPACE");

#ifdef EMACS_KEYBINDS
    BIND("\\274", "SCROLL_START");
    BIND("\\276", "SCROLL_END");
    BIND("\\342", "BACKWARD_WORD");
    BIND("\\344", "DELETE_NEXT_WORD");
    BIND("\\345", "SCROLL_END");
    BIND("\\346", "FORWARD_WORD");
    BIND("\\350", "DELETE_PREVIOUS_WORD");
    BIND("\\377", "DELETE_PREVIOUS_WORD");
#endif

    /* now for what was formerly meta1 (escape) sequences. */
    BIND("^[.", "CLEAR_SCREEN");
    BIND("^[<", "SCROLL_START");
    BIND("^[>", "SCROLL_END");
    /* ^[O and ^[[ were both META2_CHARACTER, see below .. */
    BIND("^[b", "BACKWARD_WORD");
    BIND("^[d", "DELETE_NEXT_WORD");
    BIND("^[e", "SCROLL_END");
    BIND("^[f", "FORWARD_WORD");
    BIND("^[h", "DELETE_PREVIOUS_WORD");
    BIND("^[n", "SCROLL_FORWARD");
    BIND("^[p", "SCROLL_BACKWARD");
    BIND("^[^?", "DELETE_PREVIOUS_WORD");

    /* meta2 stuff. */
    BIND("^[O^Z", "STOP_IRC");
    BIND("^[[^Z", "STOP_IRC");
    BIND("^[OC", "FORWARD_CHARACTER");
    BIND("^[[C", "FORWARD_CHARACTER");
    BIND("^[OD", "BACKWARD_CHARACTER");
    BIND("^[[D", "BACKWARD_CHARACTER");
    BIND("^[OF", "SCROLL_END");
    BIND("^[[F", "SCROLL_END");
    BIND("^[OG", "SCROLL_FORWARD");
    BIND("^[[G", "SCROLL_FORWARD");
    BIND("^[OH", "SCROLL_START");
    BIND("^[[H", "SCROLL_START");
    BIND("^[OI", "SCROLL_BACKWARD");
    BIND("^[[I", "SCROLL_BACKWARD");
    BIND("^[On", "NEXT_WINDOW");
    BIND("^[[n", "NEXT_WINDOW");
    BIND("^[Op", "PREVIOUS_WINDOW");
    BIND("^[[p", "PREVIOUS_WINDOW");
    BIND("^[O1~", "SCROLL_START");     /* these were meta30-33 before */
    BIND("^[[1~", "SCROLL_START"); 
    BIND("^[O4~", "SCROLL_END");
    BIND("^[[4~", "SCROLL_END");
    BIND("^[O5~", "SCROLL_BACKWARD");
    BIND("^[[5~", "SCROLL_BACKWARD");
    BIND("^[O6~", "SCROLL_FORWARD");
    BIND("^[[6~", "SCROLL_FORWARD");

    bind_post_init = 1; /* we're post init, now (except for init_termkeys,
			   but see below for special handling) */
#undef BIND
}

/* init_termkeys:  formerly init_keys2, this is called after we can get
 * terminal-specific key-sequences. */
void init_termkeys (void) {

#define TBIND(x, y) {                                                     \
    const char *l = get_term_capability(#x, 0, 1);                        \
    if (l)                                                                \
	bind_string(l, #y, NULL);                                         \
}

    bind_post_init = 0;
    TBIND(key_ppage, SCROLL_BACKWARD);
    TBIND(key_npage, SCROLL_FORWARD);
    TBIND(key_home, SCROLL_START);
    TBIND(key_end, SCROLL_END);
    TBIND(key_ic, TOGGLE_INSERT_MODE);
    TBIND(key_dc, DELETE_CHARACTER);
    bind_post_init = 1;
#undef TBIND
}

/* this is called only by irc_exit, and its purpose is to free
 * all our allocated stuff. */
void remove_bindings_recurse (struct Key **);
void remove_bindings (void) {

    while (binding_list != NULL)
	remove_binding(binding_list->name);

    remove_bindings_recurse(&head_keymap);
}

void remove_bindings_recurse (struct Key **mapptr) {
    struct Key *map = *mapptr;
    int c;

    /* go through our map, clear any memory that might be left lying around.
     * recurse as necessary */
    for (c = 0; c <= KEYMAP_SIZE - 1;c++) {
	if (map[c].map)
	    remove_bindings_recurse(&(map[c].map));
	if (map[c].stuff)
	    new_free(&map[c].stuff);
	if (map[c].filename)
	    new_free(&map[c].filename);
    }
    new_free((char **)mapptr);
}

/* this is called when a package is unloaded.  we should unset any
 * package-specific keybindings, and also remove any package-specific bind
 * functions. */
void unload_bindings_recurse (const char *, struct Key *);
void unload_bindings (const char *pkg) {
    struct Binding *bp, *bp2;

    /* clean the binds out first. */
    bp = binding_list;
    while (bp != NULL) {
	bp2 = bp->next;
	if (bp->filename && !my_stricmp(bp->filename, pkg))
	    remove_binding(bp->name);
	bp = bp2;
    }

    unload_bindings_recurse(pkg, head_keymap);
    clean_keymap(head_keymap);
}

void unload_bindings_recurse (const char *pkg, struct Key *map) {
    int c;

    /* go through, see which keys are package specific, unload them. */
    for (c = 0; c <= KEYMAP_SIZE - 1;c++) {
	/* if the key is explicitly bound to something, and it was done in
	 * our package, unbind it. */
	if (map[c].bound && map[c].filename && !my_stricmp(map[c].filename, pkg)) {
	    if (map[c].stuff)
		new_free(&map[c].stuff);
	    if (map[c].filename)
		new_free(&map[c].filename);
	    map[c].bound = NULL;
	}
	if (map[c].map)
	    unload_bindings_recurse(pkg, map[c].map);
    }
}

/* set_key_interval:  this is used to construct a new Timeval when the
 * 'KEY_INTERVAL' /set is changed.  We modify an external variable which
 * defines how long the client will wait to timeout, at most. */
void set_key_interval (void *stuff) {
    VARIABLE *v;
    int msec;

    v = (VARIABLE *)stuff;
    msec = v->integer;

    if (msec < 10) {
	say("Setting KEY_INTERVAL below 10ms is not recommended.");
	msec = 10;
    }

    v->integer = msec;
}

/* do_stack_bind:  this handles the /stack .. bind command.  below is the
 * function itself, as well as a structure used to hold keysequences which
 * have been stacked.  it is currently not possible to stack entire maps of
 * keys. */
struct BindStack {
    struct BindStack *next;
    unsigned char *sequence; /* the (compressed) sequence of keys. */
    int slen; /* the length of the compressed sequence. */
    struct Key key; /* the key's structure. */
};

static struct BindStack *bind_stack = NULL;
void do_stack_bind (int type, char *arg) {
    struct Key *key = NULL;
    struct Key *map = head_keymap;
    struct BindStack *bsp = NULL;
    struct BindStack *bsptmp = NULL;
    unsigned char *cs, *s;
    int slen;

    if (!bind_stack && (type == STACK_POP || type == STACK_LIST)) {
	say("BIND stack is empty!");
	return;
    }

    if (type == STACK_PUSH) {
	
	/* compress the keysequence, then find the key represented by that
	 * sequence. */
	cs = bind_string_compress(arg, &slen);
	if (cs == NULL)
	    return; /* yikes! */

	/* find the key represented by the sequence .. */
	key = find_sequence(cs, slen);

	/* key is now.. something.  if it is NULL, assume there was nothing
	 * bound.  we still push an empty record on to the stack. */
	bsp = new_malloc(sizeof(struct BindStack));
	bsp->next = bind_stack;
	bsp->sequence = cs;
	bsp->slen = slen;
	bsp->key.changed = key ? key->changed : 0;
	bsp->key.bound = key ? key->bound : NULL;
	bsp->key.stuff = key ? (key->stuff ? malloc_strdup(key->stuff) : NULL) :
	    NULL;
	bsp->key.filename = key ? malloc_strdup(key->filename) : NULL;

	bind_stack = bsp;
	return;
    } else if (type == STACK_POP) {
	unsigned char *compstr = bind_string_compress(arg, &slen);

	if (compstr == NULL)
	    return; /* yikes! */

	for (bsp = bind_stack;bsp;bsptmp = bsp, bsp = bsp->next) {
	    if (slen == bsp->slen && !memcmp(bsp->sequence, compstr, slen)) {
		/* a winner! */
		if (bsp == bind_stack)
		    bind_stack = bsp->next;
		else
		    bsptmp->next = bsp->next;

		break;
	    }
	}

	/* we'll break out when we find our first binding, or if there is
	 * nothing.  we handle it below. */
	if (bsp == NULL) {
	    say("no bindings for %s are on the stack", arg);
	    new_free(&compstr);
	    return;
	}

	/* okay, we need to push this key back in to place.  we have to
	 * replicate bind_string since bind_string has some undesirable
	 * effects.  */
	s = compstr;
	map = head_keymap;
	while (slen) {
	    key = &map[*s++];
	    slen--;
	    if (slen) {
		/* create a new map if necessary.. */
		if (key->map == NULL)
		    key->map = map = construct_keymap(key);
		else
		    map = key->map;
	    } else {
		/* we're binding over whatever was here.  check various
		   things to see if we're overwriting them. */
		if (key->stuff)
		    new_free(&key->stuff);
		if (key->filename)
		    new_free(&key->filename);
		key->bound = bsp->key.bound;
		key->changed = bsp->key.changed;
		key->stuff = bsp->key.stuff;
		key->filename = bsp->key.filename;
	    }
	}

	new_free(&compstr);
	new_free(&bsp->sequence);
	new_free(&bsp);
	return;
    } else if (type == STACK_LIST) {
	say("BIND STACK LISTING");
	for (bsp = bind_stack;bsp;bsp = bsp->next)
	    show_key(&bsp->key, bsp->sequence, bsp->slen, 0);
	say("END OF BIND STACK LISTING");
	return;
    }
    say("Unknown STACK type ??");
}

/* bindcmd:  The /BIND command.  The general syntax is:
 *
 *	/BIND ([key-descr] ([bind-command] ([args])))
 * Where:
 *	KEY-DESCR    := (Any string of keys, subject to bind_string_compress())
 *	BIND-COMMAND := (Any binding available)
 *
 * If given no arguments, this command shows all non-empty bindings which
 * are currently registered.
 *
 * If given one argument, that argument is to be a description of a valid
 * key sequence.  The command will show the binding of that sequence,
 *
 * If given two arguments, the first argument is to be a description of a
 * valid key sequence and the second argument is to be a valid binding
 * command followed by any optionally appropriate arguments.  The key
 * sequence is then bound to that action.
 *
 * The special binding command "NOTHING" actually unbinds the key.
 */
BUILT_IN_COMMAND(bindcmd) {
    const unsigned char *seq;
    char *function;
    int recurse = 0;
    unsigned char *cs;
    int	slen, retval;

    if ((seq = new_next_arg(args, &args)) == NULL) {
	show_all_bindings(head_keymap, "", 0);
	return;
    }

    /* look for flags */
    if (*seq == '-') {
	if (!my_strnicmp(seq + 1, "DEFAULTS", 1)) {
	    init_keys();
	    init_termkeys();
	    return;
	} else if (!my_strnicmp(seq + 1, "SYMBOLIC", 1)) {
	    char * symbol;

	    if ((symbol = new_next_arg(args, &args)) == NULL)
		return;
	    if ((seq = get_term_capability(symbol, 0, 1)) == NULL) {
		say("Symbolic name [%s] is not supported in your TERM type.",
			symbol);
		return;
	    }
	} else if (!my_strnicmp(seq + 1, "RECURSIVE", 1)) {
	    recurse = 1;
	    if ((seq = new_next_arg(args, &args)) == NULL) {
		show_all_bindings(head_keymap, "", 0);
		return;
	    }
	}
    }

    if ((function = new_next_arg(args, &args)) == NULL) {
	cs = bind_string_compress(seq, &slen);
	if (cs == NULL)
	    return; /* umm.. */

	show_key(NULL, cs, slen, recurse);
	return;
    }

    if (!(cs = bind_string_compress(seq, &slen))) {
	yell("BIND: couldn't compress sequence %s", seq);
	return;
    }

    /* bind_string() will check any errors for us. */
    retval = bind_compressed_string(cs, slen, function, *args ? args : NULL);
    if (!retval) {
	if (!my_strnicmp(function, "meta", 4))
	    yell(
"Please note that the META binding functions are no longer available.  \
For more information please see the bind(4) helpfile and the file \
doc/keys distributed with the EPIC source."
		);
    }
    else
        show_key(NULL, cs, slen, 0);
    new_free(&cs);
}

/* support function for /bind:  this function shows, recursively, all the
 * keybindings.  given a map and a string to work from.  if the string is
 * NULL, the function recurses through the entire map. */
void show_all_bindings (struct Key *map, const unsigned char *str, size_t len) {
    int c;
    unsigned char *newstr;
    struct Binding *self_insert;
    size_t size;

    self_insert = find_binding("SELF_INSERT");
    size = len + 2;
    newstr = alloca(size);
    strlcpy(newstr, str, size);

    /* show everything in our map.  recurse down. */
    newstr[len + 1] = '\0';
    for (c = 0; c <= KEYMAP_SIZE - 1;c++) {
	newstr[len] = c;
	if (map[c].map || (map[c].bound && map[c].bound != self_insert))
	    show_key(&map[c], newstr, len + 1, 1);
    }
}

void show_key (struct Key *key, const unsigned char *str, int slen, int recurse) {
    struct Binding *bp;
    unsigned char *clean = alloca(((strlen(str) + 1) * 2) + 1);

    if (key == NULL) {
	key = find_sequence(str, slen);
	if (key == NULL)
	    key = head_keymap;
    }

    bp = key->bound;
    if (!bp && ((recurse && !key->map) || !recurse))
	say("[*] \"%s\" is bound to NOTHING",
		(slen ? bind_string_decompress(clean, str, slen) : str));
    else {
	if (bp) {
	    say("[%s] \"%s\" is bound to %s%s%s",
		    (empty(key->filename) ? "*" : key->filename),
		    (slen ? bind_string_decompress(clean, str, slen) : str), 
		    bp->name,
		    (key->stuff ? " " : ""), (key->stuff ? key->stuff : ""));
	}
	if (recurse && key->map)
	    show_all_bindings(key->map, str, slen);
    }
}

/* the /rbind command.  This command allows you to pass in the name of a
 * binding and find all the keys which are bound to it.  we make use of a
 * function similar to 'show_all_bindings', but not quite the same, to
 * handle recursion. */
BUILT_IN_COMMAND(rbindcmd) {
    char *function;
    struct Binding *bp;

    if ((function = new_next_arg(args, &args)) == NULL)
	return;

    if ((bp = find_binding(function)) == NULL) {
	if (!my_stricmp(function, "NOTHING"))
	    say("You cannot list all unbound keys.");
	else
	    say("No such function %s", function);
	return;
    }

    show_all_rbindings(head_keymap, "", 0, bp);
}

void show_all_rbindings (struct Key *map, const unsigned char *str, int len, struct Binding *binding) {
    int c;
    unsigned char *newstr;
    size_t size;

    size = len + 2;
    newstr = alloca(size);
    strlcpy(newstr, str, size);

    /* this time, show only those things bound to our function, and call on
     * ourselves to recurse instead. */
    newstr[len + 1] = '\0';
    for (c = 0; c <= KEYMAP_SIZE - 1;c++) {
	newstr[len] = c;
	if (map[c].bound == binding)
	    show_key(&map[c], newstr, len + 1, 0);
	if (map[c].map)
	    show_all_rbindings(map[c].map, newstr, len + 1, binding);
    }
}

/* the parsekey command:  this command allows the user to execute a
 * keybinding, regardless of whether it is bound or not.  some keybindings
 * make more sense than others. :)  we look for the function, build a fake
 * Key item, then call key_exec().  Unlike its predecessor this version
 * allows the user to pass extra data to the keybinding as well, thus making
 * things like /parsekey parse_command ... possible (if not altogether
 * valuable in that specific case) */
BUILT_IN_COMMAND(parsekeycmd) {
    struct Key fake;
    struct Binding *bp;
    char *func;

    if ((func = new_next_arg(args, &args)) != NULL) {
	bp = find_binding(func);
	if (bp == NULL) {
	    say("No such function %s", func);
	    return;
	}

	fake.val = '\0';
	fake.bound = bp;
	fake.map = NULL;
	if (*args)
	    fake.stuff = malloc_strdup(args);
	else
	    fake.stuff = NULL;
	fake.filename = LOCAL_COPY("");

	key_exec(&fake);

	if (fake.stuff != NULL)
	    new_free(&fake.stuff);
    }
}

/* Used by function_bindctl */
/*
 * $bindctl(FUNCTION [FUNC] ...)
 *                          CREATE [ALIAS])
 *                          DESTROY)
 *                          EXISTS)
 *                          GET)
 *                          MATCH)
 *                          PMATCH)
 *                          GETPACKAGE)
 *                          SETPACKAGE [PACKAGE})
 * $bindctl(SEQUENCE [SEQ] ...)
 *                         GET)
 *                         SET [FUNC] [EXTRA])
 *                         GETPACKAGE)
 *                         SETPACKAGE [PACKAGE})
 * $bindctl(MAP [SEQ])
 * $bindctl(MAP [SEQ] CLEAR)
 * Where [FUNC] is the name of a binding function and [ALIAS] is any alias
 * name (we do not check to see if it exists!) and [SEQ] is any valid /bind
 * key sequence and [PACKAGE] is any free form package string.
 */

void bindctl_getmap (struct Key *, const unsigned char *, int, char **);
char *bindctl (char *input)
{
    char *listc;
    char *retval = NULL;

    GET_FUNC_ARG(listc, input);
    if (!my_strnicmp(listc, "FUNCTION", 1)) {
	struct Binding *bp;
	char *func;

	GET_FUNC_ARG(func, input);
	bp = find_binding(func);

	GET_FUNC_ARG(listc, input);
	if (!my_strnicmp(listc, "CREATE", 1)) {
	    char *alias;

	    GET_FUNC_ARG(alias, input);

	    if (bp) {
		if (bp->func)
		    RETURN_INT(0);
		remove_binding(bp->name);
	    }
	    RETURN_INT(add_binding(func, NULL, alias) ? 1 : 0);
	} else if (!my_strnicmp(listc, "DESTROY", 1)) {
	    bp = find_binding(func);
	    if (bp == NULL)
		RETURN_INT(0);
	    if (bp->func != NULL)
		RETURN_INT(0);

	    remove_binding(func);
	    RETURN_INT(1);
	} else if (!my_strnicmp(listc, "EXISTS", 1)) {
	    if (!my_stricmp(func, "NOTHING"))
		RETURN_INT(1); /* special case. */

	    RETURN_INT(find_binding(func) ? 1 : 0);
	} else if (!my_stricmp(listc, "GET")) {

	    if (bp == NULL)
		RETURN_EMPTY;
	    else if (bp->func)
		malloc_sprintf(&retval, "internal %p", bp->func);
	    else
		malloc_sprintf(&retval, "alias %s", bp->alias);

	    RETURN_STR(retval);
	} else if (!my_strnicmp(listc, "MATCH", 1)) {
	    int len;
	    len = strlen(func);
	    for (bp = binding_list;bp;bp = bp->next) {
		if (!my_strnicmp(bp->name, func, len))
		    malloc_strcat_word(&retval, space, bp->name, DWORD_NO);
	    }

	    RETURN_STR(retval);
	} else if (!my_strnicmp(listc, "PMATCH", 1)) {
	    for (bp = binding_list;bp;bp = bp->next) {
		if (wild_match(func, bp->name))
		    malloc_strcat_word(&retval, space, bp->name, DWORD_NO);
	    }

	    RETURN_STR(retval);
	} else if (!my_strnicmp(listc, "GETPACKAGE", 1)) {
	    if (bp != NULL)
		RETURN_STR(bp->filename);
	} else if (!my_strnicmp(listc, "SETPACKAGE", 1)) {

	    if (bp == NULL)
		RETURN_INT(0);

	    malloc_strcpy(&bp->filename, input);
	    RETURN_INT(1);
	}
    } else if (!my_strnicmp(listc, "SEQUENCE", 1)) {
	struct Key *key;
	unsigned char *seq;

	GET_DWORD_ARG(seq, input);
	key = find_sequence(seq, 0);
	GET_FUNC_ARG(listc, input);
	if (!my_stricmp(listc, "GET")) {
	    if (key == NULL || key->bound == NULL)
		RETURN_EMPTY;

	    retval = malloc_strdup(key->bound->name);
	    if (key->stuff)
		malloc_strcat_wordlist(&retval, " ", key->stuff);
	    RETURN_STR(retval);
	} else if (!my_stricmp(listc, "SET")) {
	    GET_FUNC_ARG(listc, input);

	    RETURN_INT(bind_string(seq, listc, (*input ? input : NULL)));
	} else if (!my_strnicmp(listc, "GETPACKAGE", 4)) {
	    if (key == NULL)
		RETURN_EMPTY;

	    RETURN_STR(key->filename);
	} else if (!my_strnicmp(listc, "SETPACKAGE", 4)) {
	    if (key == NULL || key->bound == NULL)
		RETURN_INT(0);

	    new_free(&key->filename);
	    key->filename = malloc_strdup(input);
	}
    } else if (!my_strnicmp(listc, "MAP", 1)) {
	unsigned char *seq;
	int slen;
	struct Key *key;

	seq = new_next_arg(input, &input);
	if (seq == NULL) {
	    bindctl_getmap(head_keymap, "", 0, &retval);
	    RETURN_STR(retval);
	}
	seq = bind_string_compress(seq, &slen);
	key = find_sequence(seq, slen);

	listc = new_next_arg(input, &input);
	if (listc == NULL) {
	    if (key == NULL || key->map == NULL) {
		new_free(&seq);
		RETURN_EMPTY;
	    }
	    bindctl_getmap(key->map, seq, slen, &retval);
	    new_free(&seq);
	    RETURN_STR(retval);
	} else if (!my_strnicmp(listc, "CLEAR", 1)) {
	    if (key == NULL || key->map == NULL)
		RETURN_INT(0);
	    remove_bindings_recurse(&key->map);
	    RETURN_INT(1);
	}
    }

    RETURN_EMPTY;
}

void bindctl_getmap (struct Key *map, const unsigned char *str, int len, char **ret) {
    int c;
    unsigned char *newstr;
    unsigned char *decomp;
    size_t size;

    size = len + 2;
    newstr = alloca(size);
    strlcpy(newstr, str, size);
    decomp = alloca(((len + 1) * 2) + 1);

    /* grab all keys that are bound, put them in ret, and continue. */
    newstr[len + 1] = '\0';
    for (c = 1; c <= KEYMAP_SIZE - 1;c++) {
	newstr[len] = c;
	if (map[c].bound)
	    malloc_strcat_wordlist(ret, " ", bind_string_decompress(decomp, newstr, len + 1));
	if (map[c].map)
	    bindctl_getmap(map[c].map, newstr, len + 1, ret);
    }
}
