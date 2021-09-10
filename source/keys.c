/*
 * keys.c:  Keeps track of what happens whe you press a key.
 *
 * Copyright 2002, 2015 EPIC Software Labs.
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
#include "termx.h"
#include "vars.h"
#include "window.h"
#include "timer.h"
#include "reg.h"
#include "server.h"

/* 
 * This typedef must match the definition of "BUILT_IN_KEYBINDING" 
 * in irc_std.h!  
 * This is a pointer to a function that may be used as a /BINDing.  
 * The functions themselves live in input.c (and a few other places).
 */
typedef void (*BindFunction) (unsigned int, unsigned char *);


/*
 * A "BINDING" is a named operation that is used to respond to text input
 * from the user, and usually associated with the input line.
 *
 * A Binding can either point at a C function (builtins) or an ircII alias 
 * name (user created).
 *
 * The function or alias will be invoked each time the key sequence is used.
 * The key sequence that caused the binding is passed as an argument.
 *
 * Examples of Bindings:
 *	SELF_INSERT	(insert the key into the input line at the cursor)
 *	BACKSPACE	(delete the character before the cursor)
 *	END_OF_LINE	(move the cursor to the end of the input line)
 *
 * A binding may be used as in
 *	/BIND <sequence> <binding-name>
 */
struct Binding 
{
    struct Binding *next;	/* linked-list stuff. :) */

    char *	name;		/* the name of this binding */
    BindFunction func;		/* function to use ... */
    char *	alias;		/* OR alias to call.  one or the other. */
    char *	filename;	/* the package which added this binding */
};
typedef struct Binding Binding;

	static	Binding *binding_list; 


#define KEYMAP_SIZE 128
struct Key 
{
    char   	val;  		/* the key value */
    char   	changed; 	/* 1 if this binding was changed post-startup */
    Binding *	bound; 		/* the function we're bound to. */
    struct Key *map;    	/* a map of subkeys (may be NULL) */
    struct Key *owner;  	/* the key which contains the map we're in. */
    char *	stuff;     	/* passed as 2nd argument to binding */
    char *	filename;  	/* the package which added this binding */
};
typedef struct Key Key;

	static	Key *	head_keymap = NULL; 

/* this is set when we're post-init to keep track of changed keybindings. */
	static	int	bind_post_init = 0;



/* ************************ */
static Binding *add_binding 		(const char *, BindFunction, char *);
static void	remove_binding 		(char *);
static void	remove_bound_keys 	(Key *, Binding *);
static Binding *find_binding 		(const char *name) ;

static void	key_exec 		(Key *key);
static void	key_exec_bt 		(Key *);
static void *	timeout_keypress 	(void *, Timeval);
static int	do_input_timeouts	(void *ignored);

static Key *	construct_keymap 	(Key *);
static int	clean_keymap 		(Key *);
static char *	bind_string_compress 	(const char *, int *);
static char *	bind_string_decompress 	(char *, const char *, int);
static int	bind_string 		(const char *, const char *, char *);
static int	bind_compressed_string 	(char *, int, const char *, char *);
static Key *	find_sequence 		(Key *, const char *, int);

static void	remove_bindings_recurse (Key **);
static void	unload_bindings_recurse (const char *, Key *);

static void	show_all_bindings 	(Key *, const char *, size_t);
static void	show_key 		(Key *, const char *, int, int);
static void	show_all_rbindings 	(Key *, const char *, int, Binding *);
static void	bindctl_getmap 		(Key *, const char *, int, char **);



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

/* 
 * add_binding -- Create a new operation you can BIND.
 *
 * Arguments:
 *	name	- The name of the BIND operation ie, /BIND <sequence> <name>
 *	func	- A C function to be called when binding is invoked
 *		  This must be a BUILT_IN_KEYBINDING()!
 *	alias	- An ircII alias to be called when binding is invoked
 * Return Value:
 *	The new binding is returned, or NULL if error
 * Errors:
 *	1. Both 'func' and 'alias' are specified
 *	2. 'name' is NULL.
 *	3. There is already a binding for 'name'
 *
 * Notes: 
 *	1. The 'alias' must be an alias name, not a block of ircII code!
 */
static Binding *add_binding (const char *name, BindFunction func, char *alias) 
{
	Binding *	bp;

	if (func && alias) 
		return NULL;	/* Both func and alias specified */

	if (!name)
		return NULL; 	/* no binding name. */

	if (find_binding(name))
	{
		yell("Binding %s already exists!", name);
		return NULL;
	}

	bp = new_malloc(sizeof(Binding));
	bp->name = malloc_strdup(name);
	add_to_list((List **)&binding_list, (List *)bp);

	bp->alias = NULL;
	bp->func = NULL;

	if (alias) 
		bp->alias = malloc_strdup(alias);
	else
		bp->func = func;

	bp->filename = NULL;
	if (current_package())
		bp->filename = malloc_strdup(current_package());

	return bp;
}

/*
 * remove_binding -- Delete a bind operation, making it unavailable for use.
 *
 * Arguments:
 *	name	- The name of a BIND operation
 * Notes:
 *	Any key sequences currently using the BIND will be deleted.
 */
static void	remove_binding (char *name) 
{
	Binding *bp;

	if (!name)
		return;

	if ((bp = (Binding *)remove_from_list((List **)&binding_list, name)))
	{
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

/*
 * remove_bound_keys -- Remove all key sequences using a given BINDing
 *
 * Arguments:
 *	map	- A key sequence space
 *	binding	- A binding that is going away
 * Notes:
 *	Upon return, no sequence in 'map' will be pointing to 'binding'.
 */
static void	remove_bound_keys (Key *map, Binding *binding) 
{
	int c;

	for (c = 0; c <= KEYMAP_SIZE - 1; c++) 
	{
		if (map[c].bound == binding)
			map[c].bound = NULL;
		if (map[c].map)
			remove_bound_keys(map[c].map, binding);
	}
}

/*
 * find_binding -- Return the object for a given BINDing name
 *
 * Arguments: 
 *	name	- The BINDing we're interested in
 * Return value:
 *	A (Binding *) for 'name', or NULL if it doesn't exist.
 * Errors:
 *	NULL is returned if 'name' is NULL
 */
static Binding *	find_binding (const char *name) 
{
	if (!name)
		return NULL;

	return (Binding *)find_in_list((List **)&binding_list, name, 0);
}

/*
 * init_binds -- Create all hardcoded BINDs at startup
 *
 * This function is called at startup before any user scripts are loaded
 * to ensure that the builtin binds will be available for use.
 */
void 	init_binds (void) 
{
#define ADDBIND(x, y) add_binding(x, y, NULL);
    /* there is no 'NOTHING' bind anymore. */
    ADDBIND("ALTCHARSET",		    insert_altcharset		    );
    ADDBIND("BACKSPACE",		    input_backspace		    );
    ADDBIND("BACKWARD_CHARACTER",	    backward_character		    );
    ADDBIND("BACKWARD_WORD",		    input_backward_word		    );
    ADDBIND("BEGINNING_OF_LINE",	    input_beginning_of_line	    );
    ADDBIND("BLINK",			    insert_blink		    );
    ADDBIND("BOLD",			    insert_bold			    );
    ADDBIND("CLEAR_SCREEN",		    my_clear_screen		    );
    ADDBIND("CPU_SAVER",		    cpu_saver_on		    );
    ADDBIND("DEBUG_INPUT_LINE",		    debug_input_line	            );
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
    ADDBIND("ITALIC",			    insert_italic		    );
    ADDBIND("NEXT_WINDOW",		    next_window			    );
    ADDBIND("PARSE_COMMAND",		    parse_text			    );
    ADDBIND("PREVIOUS_WINDOW",		    previous_window		    );
    ADDBIND("QUIT_IRC",			    irc_quit			    );
    ADDBIND("QUOTE_CHARACTER",		    quote_char			    );
    ADDBIND("REFRESH_INPUTLINE",	    refresh_inputline		    );
    ADDBIND("REFRESH_SCREEN",		    refresh_screen   		    );
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
#if 0
    ADDBIND("TRANSPOSE_CHARACTERS",	    input_transpose_characters	    );
#endif
    ADDBIND("TYPE_TEXT",		    type_text			    );
    ADDBIND("UNCLEAR_SCREEN",		    input_unclear_screen	    );
    ADDBIND("UNDERLINE",		    insert_underline		    );
    ADDBIND("UNSTOP_ALL_WINDOWS",	    unstop_all_windows		    );
    ADDBIND("YANK_FROM_CUTBUFFER",	    input_yank_cut_buffer	    );
#undef ADDBIND
}

/* * * * * * * * * * * * * * * KEYS SECTION * * * * * * * * * * * * * * * * */

/*
 * "KEYS" are sequences of 7-bit characters which may be associated with
 * a BIND operation.  Each time the user types a sequence, the associated
 * bind operation will be called.
 */


/* this function is used to actually execute the binding for a specific key.
 * it checks to see if the key needs to call an alias or a function, and
 * then makes the appropriate call.  if the key is not bound to any action
 * at all, assume we were called as part of a timeout or a terminator on a
 * sequence that didn't resolve.  if that is the case, use the special
 * 'key_exec_bt' function to walk backwards along the line and execute the
 * keys as if they were individually pressed. */

/*
 * key_exec -- Invoke the callback of a specific key sequence
 *
 * Arguments:
 *	key 	A terminal key sequence that has been typed by the user
 *
 * Notes:
 *	It is entirely possible for a terminal key sequence to not be bound
 *	to any operation!  This would happen for an incomplete portion of
 * 	a key sequence, for example:
 *		^[[A is <cursor up>
 *	but what if the user types <escape> + "["?  
 * 	That would not be bound to anything.
 *	So if that happens, we "backtrack", and decompose whatever we have
 *	with whatever makes sense (in the above case, whatever <escape> is
 *	bound to, plus whatever '[' is bound to (ie, SELF_INSERT)
 *	(This is handled by key_exec_bt())
 */
static void	key_exec (Key *key) 
{
	if (!key)
		return; /* nothing to do. */

	/* 
	 * if the key isn't bound to anything, and it has an owner, assume we
	 * got a premature terminator for one or more key sequences.  call
	 * key_exec_bt to go back and see about executing the input in smaller
	 * chunks. 
	 */
	if (!key->bound)
	{
		if (key->owner)
		    key_exec_bt(key);
		return;
	}

	/*
	 * First, offer it to the user.  If they grab it, then we're done.
	 */
	if (do_hook(KEYBINDING_LIST, "%s 0 %d", 
				key->bound->name, (int)key->val))
	{
		/* check alias first, then function */
		if (key->bound->alias)
		{
			/*
			 * Your callback is invoked as
			 *	/<alias-name><space><sequence>
			 *
			 * It is possible (but would break backwards
			 * compatability) to pass key->stuff as the subargs
			 * to call_lambda_command() instead of appending it
			 * to 'exec', which would allow you to use any 
			 * arbitrary ircII code.  Oh well!
			 *
			 * I suppose it's possible we could auto-detect that,
			 * but i'm not sure if I care that much.
			 */
			char *exec = malloc_strdup(key->bound->alias);
			if (key->stuff)
			    malloc_strcat_wordlist(&exec, space, key->stuff);
			call_lambda_command("KEY", exec, empty_string);
			new_free(&exec);
		} 
		else if (key->bound->func)
			key->bound->func(key->val, key->stuff);
	}
}
	
/* 
 * this function unwinds the current 'stack' of input keys, placing them
 * into a string, and then parses the string looking for the longest
 * possible input combinations and executing them as it goes. 
 */
static void	key_exec_bt (Key *key) 
{
	const char *	kstr = (const char *)empty_string;
	const char *	nstr;
	int 		len = 1, 
			kslen;
	Key *		kp;

	/* now walk backwards, growing kstr as necessary */
	while (key != NULL) 
	{
		char *buf;

		buf = alloca(len + 1);
		memcpy(buf + 1, kstr, len);
		buf[0] = key->val;
		kstr = buf;
		len++;
		key = key->owner;
	}

	/* 
	 * kstr should contain our keystring now, so walk along it and find the
	 * longest patterns available *that terminate*.  what this means is that
	 * we need to go backwards along kstr until we find something that
	 * terminates, then we need to lop off that part of kstr, and start
	 * again.  this is not particularly efficient. :/ 
	 */
	kslen = len;
	while (*kstr) 
	{
		kp = NULL;
		nstr = kstr;
		kslen--;

		while (nstr != (kstr + kslen)) 
		{
			/* This used to be a panic, but that seems excessive */
			if (*nstr < 0)
				return;		/* Very bad -- bail */

			if (nstr == kstr) /* beginning of string */
				kp = &head_keymap[(unsigned char)*nstr];
			else if (kp->map != NULL)
				kp = &kp->map[(unsigned char)*nstr];
			else
				break; /* no luck here */
			nstr++;
		}

		/* 
		 * did we get to the end?  if we did and found a key that 
		 * executes, go ahead and execute it.  if kslen is equal 
		 * to 1, and we didn't have any luck, simply discard the key 
		 * and continue plugging forward. 
		 */
		if (nstr == (kstr + kslen)) 
		{
			if (kp && (kp->bound != NULL || kslen == 1)) 
			{
				if (kp->bound != NULL)
					key_exec(kp);
				len -= (nstr - kstr);
				kslen = len; 
				kstr = nstr; /* move kstr forward */
				continue; /* now move along */
			}
		}
		/* 
		 * otherwise, we'll just continue above (where kslen is 
		 * decremented and nstr is re-set 
		 */
	}
}


/* 
 * this function tries to retrieve the binding for a key pressed on the
 * input line.  depending on the circumstances, we may need to execute the
 * previous key's action (if there has been a timeout).  The timeout factor
 * is set in milliseconds by the KEY_INTERVAL variable.  See further for
 * instructions. :) 
 */
/*
 * handle_keypress -- Handle each logical keypress from the user
 *
 * Arguments:
 *	lastp	- The return value from the previous call to 
 *		  handle_keypress().  Used to maintain state about what keys
 *		  the user has pressed but haven't resulted in a complete 
 *		  key sequence.  This value should be per-screen.
 *		  If NULL, assume this is a new key sequence.
 *	pressed	- The time of the previous call to handle_keypress().
 *		  This is used to manage timeouts for ambiguous key sequences.
 *	keyx	- The logical key that the user pressed.  This _MUST_ be a
 *		  32 bit unicode code point.  The caller is responsible for
 *		  converting the user's input into unicode.
 *	quote_override (Temporary) - Ignore the key binding for this 
 *		  character, and treat it as though it were bound to 
 *		  SELF_INSERT.  This happens after you do a QUOTE_CHARACTER.
 *		  XXX - Some day, this argument will go away when the bind
 *		        system grows keyspaces.
 *
 * Return value:
 *	The return value should be passed to the next call to handle_keypress().
 *	It does not matter what the value is. ;-)
 */
/*
 * HOW KEYBINDINGS WORK:
 *
 * "head_keymap" is a pointer to an array that contains the current
 * keybinding space.  In theory, we could switch what head_keymap
 * points to, in order to implement alternate keyspaces (ie, by
 * screen, by window, by server, whatever).
 *
 * Key maps are recursive data structures that are nodes.
 * Each node contains 127 pointers to other nodes/keymaps (I will
 * call these "submaps").  Each ordinal position points to the
 * keybindings that are "downwind" of that key.
 * For example, head_keymap[27] contains all of the keybindings that
 * start with an escape.
 *
 * In addition to 127 pointers to other submaps, each node contains 
 * a keybinding for *this* position.  Being able to contain both 
 * a binding and pointers to submaps allows us to support ambiguous
 * keybindings:
 *	/bind ^[ toggle_insert_mode
 *	/bind ^[[A cursor_left
 * Traditionally, ircII has used a deterministic state machine to 
 * handle keybindings, so it was not possible to have ambiguous
 * keybindings like this.
 *
 * In order to disambiguate, there is a "timeout" that is triggered
 * each time you pass through an ambiguous node.  If you haven't
 * left that node by the time you press the next key, then it will
 * trigger that node.   FOR THIS REASON if you have ambiguous binds,
 * there will be a short pause when you use the shorter sequence.
 * You can use /SET KEY_INTERVAL <millisecs> to control this
 *
 * Each time you press a key, the client walks from the current 
 * node to the next node until it finds a terminal keybinding
 * (one without any children nodes), or the timeout occurs.
 * Each time a terminal keybinding is found, that keybinding is 
 * executed, and the node resets to the top (head_keymap)
 *
 * Clear as mud?
 */
void *	handle_keypress (void *lastp, Timeval pressed, u_32int_t keyx, int quote_override) 
{
	Key 	*kp, 
		*last;
	unsigned char 	key;

	/* we call the timeout code here, too, just to be safe. */
	last = timeout_keypress(lastp, pressed);

	/*
	 * First off, under the new regime, any code point > 127 is 
	 * unconditionally bound to SELF_INSERT.
	 *
	 * Additionally, if the "quote_override" flag is 1, then this
	 * key is unconditionally bound to SELF_INSERT.
	 */
	if (keyx > 127 || quote_override)
	{
		input_add_character(keyx, NULL);
		return NULL;
	}

	/*
	 * Otherwise, it's a 7 bit char, and we process it like we 
	 * always have...
	 */
	key = (char)(keyx & 0x7F);

	/* 
	 * if last is NULL (meaning we're in a fresh state), pull from the head
	 * keymap.  if last has a map, pull from that map.  if last has no map,
	 * something went wrong (we should never return a 'last' that is
	 * mapless!) 
	 */
	if (last == NULL)
		kp = &head_keymap[key];
	else if (last->map != NULL)
		kp = &last->map[key];
	else 
		return NULL;	/* No map?  Give up */

	/*
	 * If this node is ambiguous, there is a binding here, but there
	 * is also a binding that contains this place as a substring.
	 * Scheduler a timeout, and if you haven't pressed any more keys
	 * by then, this node will be executed.
	 */
	if (kp->map && kp->bound)
		add_timer(0, empty_string, 
				get_int_var(KEY_INTERVAL_VAR) / 1000.0, 1,
				do_input_timeouts, NULL, NULL, GENERAL_TIMER, 
				-1, 0, 0);

	/*
	 * If this node is NOT ambiguous, but it is not a terminal node
	 * (it has a map), then return this node back to our caller and let
	 * them tell us next time they have another key to work with.
	 */
	if (kp->map != NULL)
		return (void *)kp;

	/*
	 * If this node is NOT ambiguous, and it is a terminal node
	 * (it does not have a map) then execute this node and reset 
	 * the state back to the top.
	 */
	key_exec(kp);

	/*
	 * XXX - Ideally, we should return head_map here, because that is
	 * what NULL is taken as next time through.  By returning the top
	 * of the key space, we could implement multiple spaces, and then
	 * return the appropriate one here.  For example, we could implement
	 * the QUOTE_CHARACTER as a space where every key is bound to 
	 * a special SELF_INSERT that turns off QUOTE_CHARACTER, and then 
	 * switch to the default key space.  Just an idea...
	 * (Or vi modes. or other neat things)
	 */
	return NULL;
}

/*
 * timeout_keypress -- check if an ambiguous key sequence should be resolved.
 *
 * Arguments:
 *	lastp	- A keymap node representing the current state of input
 *	pressed - When the user last pressed a key
 *
 * Return Value:
 *	The return value should be passed to the next call to handle_keypress().
 *	It does not matter what the value is. ;-)
 */
static void *	timeout_keypress (void *lastp, Timeval pressed) 
{
	int 	mpress = 0; /* ms count since last pressing */
	Timeval tv;
	Timeval right_now;
	Key *	last;

	/* If there is no outstanding key sequence, we have nothing to check */
	if (lastp == NULL)
		return NULL; 

	/* 
	 * If this node is not ambiguous (it is not bound here, but requires
	 * more keys to complete, we have nothing to resolve.
	 */
	last = (Key *)lastp;
	if (last->bound == NULL)
		return (void *)last; 

	/*
	 * Otherwise, the current node is ambiguous.  See how long it has 
	 * been since the user pressed a key...
	 */
	get_time(&right_now);
	tv = time_subtract(pressed, right_now);
	mpress = tv.tv_sec * 1000;
	mpress += tv.tv_usec / 1000;

	/*
	 * If it has been more than /set key_interval milliseconds since 
	 * the user last pressed a key the sequence has "timed out" and we
	 * will resolve the sequence right here.
	 */
	if (mpress >= get_int_var(KEY_INTERVAL_VAR)) 
	{
		/* Fire off the current node and reset the state */
		key_exec(last);
		return NULL; 
	}
	return (void *)last; /* still waiting.. */
}

/* 
 * do_input_timeouts -- A /TIMER callback set up whenever the user presses
 *			an ambiguous key sequence.  We must check all screens 
 *			for input timeouts
 *
 * Arguments:
 *	ignored	- Not used (Mandatory argument for TIMER callback)
 *
 * Return Value:
 *	This function returns 0, which is ignored by the TIMER system.
 */
static int	do_input_timeouts (void *ignored)
{
	Screen *oldscreen = last_input_screen;
	int 	server = from_server;
	Screen *screen;

	/*
	 * Walk each screen and call timeout_keypress() on it.
	 * timeout_keypress() checks to see if it has an ambiguous
	 * keybinding and then resolves it
	 */
	for (screen = screen_list; screen; screen = screen->next) 
	{
		/* Ignore inactive screens */
		if (!screen->alive) 
			continue;

		/* Ignore screens without ambiguous sequences */
		if (!screen->last_key || screen->last_key == head_keymap)
			continue;

		/* Set up context and check the sequence */
		last_input_screen = output_screen = screen;
		make_window_current(screen->current_window);
		from_server = current_window->server;

		screen->last_key = timeout_keypress(screen->last_key,
						    screen->last_press);
	}

	output_screen = last_input_screen = oldscreen;
	from_server = server;
	return 0;
}


/**************************************************************************/

/*
 * construct_keymap -- Create a submap linked from a parent node ("owner").
 * 
 * Arguments:
 *	owner	- A Key node that has bindings underneath it.
 *
 * Return Value:
 *	An array of Key nodes that point to 'owner' which is suitable
 *	for assigning back with 'owner->map = retval'.
 *
 * Notes:
 * 	Submaps are created only on-demand, so a node that is a terminal
 *	node does not get a submap.
 *	All of the keys in the submap are not bound to anything.
 */
static Key *	construct_keymap (Key *owner) 
{
	int c;
	Key *map = new_malloc(sizeof(Key) * KEYMAP_SIZE);

	for (c = 0;c <= KEYMAP_SIZE - 1;c++) 
	{
		map[c].val = c;
		map[c].bound = NULL;
		map[c].map = NULL;
		map[c].owner = owner;
		map[c].stuff = NULL;
		map[c].filename = NULL;
	}

	return map;
}

/*
 * clean_keymap	-- Determine if any bindings reference the Key node 'map'.
 *		   If there are none, it is deleted.
 *
 * Arguments:
 *	key 	- A Key node that will be checked for use.
 *		  IF 0 IS RETURNED, THIS POINTER HAS BEEN INVALIDATED.
 *		  YOU MUST NOT USE THIS POINTER AFTER THIS CALL IN THAT CASE.
 *
 * Return value:
 *	The number of bindings that reference 'map':
 *	0	- No keybindings pointed at 'key' so it was deleted and
 *		  invalidated.  DO NOT USE 'key' IF ZERO IS RETURNED.
 *	1	- Some keybindings reference 'map', so it is unchanged.
 *
 * XXX - I think 'map' should be a (Key **).
 */
static int	clean_keymap (Key *map) 
{
	int	c;
	int 	save = 0;

	/* 
	 * Check our direct children nodes, and recursively their children.
	 */
	for (c = 0; c <= KEYMAP_SIZE - 1; c++) 
	{
		/*
		 * If a direct child is bound to something, we are in use!
		 */
		if (map[c].bound)
			save = 1; 

		/* 
		 * Check each of our children recursively to see if they
		 * have any bindings downwind of them.
		 */ 
		if (map[c].map) 
		{
			/*
			 * If this child has any bindings in it, then
			 * that means we do too -- propogate this upwards.
			 * But if the child did not have any bindings,
			 * then it was destroyed, so we need to discard
			 * that pointer.
			 */
			if (clean_keymap(map[c].map))
				save = 1; 
			else
				map[c].map = NULL; 
		}
	}

	/* Invalidate this node if there is nothing here */
	if (!save)
		new_free(&map);

	return save;
}

/*
 * bind_string_compress	-- Convert a user-friendly key sequence into 
 *			   a literal byte sequence
 * Arguments:
 *	str	- An input string with special characters in user-friendly
 *		  format.
 *	len	- How the return value is.
 *
 * Return Value:
 *	A string containing the collapsed version of 'str'.
 *	THIS STRING IS NEW_MALLOC()ED AND MUST BE NEW_FREE()d.
 *	Or, if the input string is empty, then NULL is returned.
 *
 * Note:
 *	These are the transformations that are supported:
 *	   ^C (or ^c)	- control characters between 0 and 31 (and 127)
 *		^@		0
 *		^A (or ^a)	1
 *		^B (or ^b)	2
 *		^C (or ^c)	3
 *		...		...
 *		^X (or ^x)	24
 *		^Y (or ^y)	25
 *		^Z (or ^z)	26
 *		^[		27
 *		^\		28
 *		^]		29
 *		^^		30
 *		^_		31
 *		^?		127
 *
 * 	  \e			27 (escape)
 *	  \xxx	(where 'xxx' is between 000 and 177)
 *	  \^			94 (caret)
 *	  \\			92 (backslash)
 *
 * Other notes:
 *	A \ at the end of the string does not need to be escaped.
 *	\x where 'x' is anything other than 'e' results in 'x'
 *	Any string ^c where c is not listed above is treated literally
 */
static char *	bind_string_compress (const char *str, int *len) 
{
	char 	*new, 
		*s,
		c;

#define isoctaln(x) ((x) > 47 && (x) < 56)

	if (!str)
		return NULL;
	s = new = new_malloc(strlen(str) + 1); 
			/* we will always make the string smaller. */

	*len = 0;
	while (*str) 
	{
	    switch (*str) 
	    {
	        case '^':
		    str++; /* pass over the caret */

		    /*
		     * ^? is DEL (127)
		     */
		    if (*str == '?') 
		    {
			s[(*len)++] = '\177'; /* ^? is DEL */
			str++;
		    } 

		    /*
		     * Any invalid ^x sequence is treated literally as
		     * the two characters ^ and <x>.
		     */
		    else if (toupper(*str) < 64 || toupper(*str) > 95) 
			s[(*len)++] = '^';

		    /*
		     * Otherwise, ^x if <x> is a letter is the ordinal
		     * value of the uppercase letter minus 64
		     * (ie, A is 65, minus 64, so ^A is 1.
		     * Non-letters are just the ordinal value - 64.
		     */
		    else 
		    {
			if (isalpha(*str))
				s[(*len)++] = toupper(*str) - 64;
			else
				s[(*len)++] = *str - 64;
			str++;
		    }
		    break;

		case '\\':
		    str++;
		    if (isoctaln(*str)) 
		    {
			c = (*str - 48);
			str++;
			if (isoctaln(*str)) 
			{
				c *= 8;
				c += (*str - 48);
				str++;
				if (isoctaln(*str)) 
				{
					c *= 8;
					c += (*str - 48);
					str++;
				}
			}

			/* No matter what, strip the high bit */
			s[(*len)++] = c & 0x7F;
		    } 
		    else if (*str == 'e')  
		    {
			s[(*len)++] = '\033'; /* ^[ (escape) */
			str++;
		    } 
		    else if (*str) /* anything else that was escaped */
			s[(*len)++] = *str++;
		    else 
		    	s[(*len)++] = '\\'; /* end-of-string.  no escape. */

		    break;

		default:
		    s[(*len)++] = *str++;
	    }
	}

	s[*len] = 0;

	/* If the resulting string was empty, return NULL */
	if (!*len) 
	{
		new_free(&new);
		return NULL;
	}

	return new;
}

/*
 * bind_string_decompress -- Convert a certain number of bytes into something
 *				that can be passed to /BIND
 *
 * Arguments:
 *	dst 	- The place which will contain the decompressed string.
 *		  THIS MUST BE AT LEAST TWICE AS BIG AS 'srclen'!
 *	src	- The bytes to be converted
 *	srclen	- How many bytes in 'src' to decompress
 *
 * Return value:
 *	'dst' is returned.
 *
 * Notes:
 *	dst MUST BE TWICE AS BIG AS 'srclen'!  Otherwise it will buffer
 *	overflow.  XXX There should be a dstlen argument.
 */
static char *	bind_string_decompress (char *dst, const char *src, int srclen) 
{
	char *ret = dst;

	while (srclen) 
	{
		if (*src < 32) 
		{
			*dst++ = '^';
			*dst++ = *src + 64;
		} 
		else if (*src == 127) 
		{
			*dst++ = '^';
			*dst++ = '?';
		} 
		else
			*dst++ = *src;
		src++;
		srclen--;
	}
	*dst = 0;

	return ret;
}

/*
 * bind_string -- Bind a DECOMPRESSED bind sequence (human-readable) to a 
 *		  given bind action. 
 *
 * Arguments:
 *	sequence - A DECOMPRESSED bind sequence (ie, human readable), 
 *		   ie, the 1st argument to /BIND.
 *	bindstr	- The action this sequence shall be bound to, ie, the 
 *		  2nd argument to /BIND.
 *	args	- Any extra params to /BIND (ie, for parse_command {...})
 *
 * Return value:
 *	0	The key sequence was not bound to the action	
 *		Either:
 *		* 'sequence' or 'bindstr' are null, or 
 *		* 'bindstr' is not a valid bind action.
 *	1	The key sequence was bound to the action
 *
 * Notes:
 *	This function is only used by start-up stuff.
 */
static	int	bind_string (const char *sequence, const char *bindstr, char *args) 
{
	char *	cs;
	int	slen, 
		retval;

	if (!(cs = bind_string_compress(sequence, &slen))) 
	{
		yell("bind_string(): couldn't compress sequence %s", sequence);
		return 0;
	}

	retval = bind_compressed_string(cs, slen, bindstr, args);
	new_free(&cs);
	return retval;
}

/*
 * bind_compressed_string - Bind a COMPRESSED bind sequence (returned by
 *			    bind_string_compress()) to a given bind action.
 *			    Basically the functional guts of /BIND.
 *
 * Arguments:
 *	keyseq	- A DECOMPRESSED bind sequence (ie, human readable), 
 *		   ie, the 1st argument to /BIND.
 *	slen	- How many bytes are in 'keyseq'
 *	bindstr	- The action this sequence shall be bound to, ie, the 
 *		  2nd argument to /BIND.
 *	args	- Any extra params to /BIND (ie, for parse_command {...})
 *
 * Return value:
 *	0	The key sequence was not bound to the action	
 *		Either:
 *		* 'sequence' or 'bindstr' are null, or 
 *		* 'bindstr' is not a valid bind action.
 *	1	The key sequence was bound to the action
 */ 
static int	bind_compressed_string (char *keyseq, int slen, const char *bindstr, char *args) 
{
	char *s;
	Key *node;
	Key *map;
	Binding *bp = NULL;

	if (!keyseq || !slen || !bindstr) 
	{
		yell("bind_compressed_string(): called without sequence or bind function!");
		return 0;
	}

	/*
	 * Determine what the bind action will be.
	 * NOTHING is not a bind action, but means "remove this sequence"
	 */
	if (!my_stricmp(bindstr, "NOTHING"))
		bp = NULL;
	else if (!(bp = find_binding(bindstr)))
	{
		say("No such function %s", bindstr);
		return 0;
	}

	for (s = keyseq, map = head_keymap; slen > 0; slen--, s++)
	{
		if (*s < 0)
		{
			yell("Cannot bind sequences containing high bit chars");
			return 0;
		}
		node = &map[(unsigned char)*s];

		/*
		 * As long as we are not at the final key of the sequence,
		 * keep diving through the key space, one character at a 
		 * time.  If this next key does not exist, create a new 
		 * submap for it to live in.
		 */
		if (slen > 1)
		{
			/* create a new map if necessary.. */
			if (!node->map)
				node->map = construct_keymap(node);

			map = node->map;
			continue;
		}

		/*
		 * At this point we're at the final character in our sequence
		 * so we need to bind it right here.
		 * If there is anything already here, we clobber it.
		 */
		if (node->stuff)
			new_free(&node->stuff);
		if (node->filename)
			new_free(&node->filename);

		node->changed = bind_post_init;

		if ((node->bound = bp))
		{
		    if (args)
			node->stuff = malloc_strdup(args);
		    if (current_package())
			malloc_strcpy(&node->filename, current_package());
		}
	}

	/*
	 * Do garbage collection (because 'bp' may have been NOTHING and that
	 * may have resulted in one or more key nodes being empty)
	 */
	if (bind_post_init)
		clean_keymap(head_keymap);

	/* Success! */
	return 1;
}

/*
 * find_sequence -- Locate a key node in a key space from a COMPRESSED sequence
 *
 * Arguments:
 *	map	- The keyspace to look into
 *	seq	- A COMPRESSED key sequence to look for 
 *	slen	- The length of 'seq'.
 *
 * Returns:
 *	If successful, the key node for 'seq'
 * 	If not successful, NULL
 */ 
static Key *	find_sequence (Key *top, const char *seq, int slen)
{
	const char *	s;
	Key *		node = NULL;	/* Just in case 'slen' is 0 */
	Key *		map;

	/*
	 * Walk the key sequence.  For any non-terminal point (slen > 0)
	 * if there is no submap here, then the sequence definitely does
	 * not exist, so we can stop right away.
	 */
	for (s = seq, map = top; slen > 0; slen--, s++)
	{
		if (*s < 0)
			return NULL;

		node = &map[(unsigned char)*s];

		if (slen > 1 && !node->map)
			return NULL;
		map = node->map;
	}

	return node;
}

/*
 * init_keys -- Bootstrap the keybinding system.
 *		1. Create the initial keyspace (head_keymap)
 *		2. Set everything 32 <= x <= 127 to SELF_INSERT
 *		3. Set up "hardcoded" keybindings
 *
 *  This function is run at startup and whenever you do /bind -defaults
 */
void	init_keys (void)
{
	int c;
	char s[2];

	head_keymap = construct_keymap(NULL);

#define BIND(x, y) bind_string(x, y, NULL);
	/* bind characters 32 - 255 to SELF_INSERT. */
	s[1] = '\0';
	for (c = 32;c <= KEYMAP_SIZE - 1;c++) 
	{
		s[0] = (char )c;
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
#if 0
	BIND("^T", "TRANSPOSE_CHARACTERS");
#endif
	BIND("^U", "ERASE_LINE");
	BIND("^V", "REVERSE");
	BIND("^W", "NEXT_WINDOW");
        BIND("^X", "SWITCH_CHANNELS");
	BIND("^Y", "YANK_FROM_CUTBUFFER");
	BIND("^Z", "STOP_IRC");
	/* ^[ (was META1_CHARACTER) */
	/* ^\ */
	/* ^^ */
	BIND("^_", "UNDERLINE");
	/* mind the gap .. */
	BIND("^?", "BACKSPACE");

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

/*
 * init_termkeys -- Set up keybindings based on your TERM setting.
 *
 * This function is not rolled into init_keys, because at startup, we have
 * to initialize the keybinding system before we set up the display, so we
 * don't yet know what the TERM is.
 */
void	init_termkeys (void) 
{
#define TBIND(x, y) {                                                     \
	const char *l;							  \
	if ((l = get_term_capability(#x, 0, 1)))			  \
		bind_string(l, #y, NULL);                                 \
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

/*
 * remove_bindings -- Remove all bind actions from the system.
 * 
 * This is used by /bind -defaults to reset to factory defaults
 * This is also used by irc_exit() so we can check for memory leaks.
 */
void	remove_bindings (void) 
{
	while (binding_list != NULL)
		remove_binding(binding_list->name);

	remove_bindings_recurse(&head_keymap);
}

/*
 * remove_bindings_recurse -- Remove all bound key sequences (recursively)
 *
 * Arguments:
 *	mapptr 	- A pointer to a Key node being cleared.  
 *		  It will be set to NULL when it's done.
 */
static void	remove_bindings_recurse (Key **mapptr) 
{
	Key *map = *mapptr;
	int c;

	/* 
	 * Go through our map, clear any memory that might be left lying 
	 * around, recursing if necessary 
	 */
	for (c = 0; c <= KEYMAP_SIZE - 1;c++) 
	{
		if (map[c].map)
			remove_bindings_recurse(&(map[c].map));
		if (map[c].stuff)
			new_free(&map[c].stuff);
		if (map[c].filename)
			new_free(&map[c].filename);
	}
	new_free((char **)mapptr);
}

/*
 * unload_bindings -- Remove all bind actions and keybindings created 
 *		      by a package
 *
 * Arguments:
 *	pkg	- A /PACKAGE being /UNLOADed.
 */
void	unload_bindings (const char *pkg) 
{
	Binding *bp, *bp2;

	/*
	 * First, delete all bind actions created by the package.
	 */
	for (bp = binding_list; bp; bp = bp2)
	{
		bp2 = bp->next;
		if (bp->filename && !my_stricmp(bp->filename, pkg))
			remove_binding(bp->name);
	}

	/*
	 * Then, delete all the keybindings created by the package.
	 */
	unload_bindings_recurse(pkg, head_keymap);

	/* Clean up the mess */
	clean_keymap(head_keymap);
}

/*
 * unload_bindings_recurse -- Delete all keybindings in a space created 
 *			      by a package.
 *
 * Arguments:
 *	pkg	- A /PACKAGE being /UNLOADed.
 *	map	- A keyspace to be cleaned
 */
static void	unload_bindings_recurse (const char *pkg, Key *map) 
{
	int c;

	for (c = 0; c <= KEYMAP_SIZE - 1; c++) 
	{
		/*
		 * If this key is bound to our package, unbind it...
		 */
		if (map[c].bound && map[c].filename && 
				!my_stricmp(map[c].filename, pkg)) 
		{
			if (map[c].stuff)
				new_free(&map[c].stuff);
			if (map[c].filename)
				new_free(&map[c].filename);
			map[c].bound = NULL;
		}

		/* ... and check our children nodes recursively. */
		if (map[c].map)
			unload_bindings_recurse(pkg, map[c].map);
	}
}

/*
 * set_key_interval -- A /SET callback for /SET KEY_INTERVAL, used to control
 *		       how many milliseconds we should wait after an ambiguous
 *		       key sequence before resolving to the shorter sequence.
 *
 * Arguments:
 *	stuff	- a pointer to our /SET object.
 */
void	set_key_interval (void *stuff) 
{
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
	char *	sequence; 	/* the (compressed) sequence of keys. */
	int 	slen; 		/* the length of the compressed sequence. */
	Key 	key; 		/* the key's structure. */
};
typedef struct BindStack BindStack;

static BindStack *bind_stack = NULL;

void	do_stack_bind (int type, char *arg) 
{
	Key *key = NULL;
	Key *map;
	BindStack *bsp = NULL;
	BindStack *bsptmp = NULL;
	char *cs, *s;
	int slen;

	if (!bind_stack && (type == STACK_POP || type == STACK_LIST)) 
	{
		say("BIND stack is empty!");
		return;
	}

	if (type == STACK_PUSH) 
	{
		/* compress the keysequence, then find the key represented by that
		* sequence. */
		cs = bind_string_compress(arg, &slen);
		if (cs == NULL)
			return; /* yikes! */

		/* find the key represented by the sequence .. */
		key = find_sequence(head_keymap, cs, slen);

		/* key is now.. something.  if it is NULL, assume there was nothing
		* bound.  we still push an empty record on to the stack. */
		bsp = new_malloc(sizeof(BindStack));
		bsp->next = bind_stack;
		bsp->sequence = cs;
		bsp->slen = slen;
		bsp->key.changed = key ? key->changed : 0;
		bsp->key.bound = key ? key->bound : NULL;
		bsp->key.stuff = key ? (key->stuff ? malloc_strdup(key->stuff) : NULL) : NULL;
		bsp->key.filename = key ? malloc_strdup(key->filename) : NULL;

		bind_stack = bsp;
		return;
	} 
	else if (type == STACK_POP) 
	{
		char *compstr = bind_string_compress(arg, &slen);

		if (compstr == NULL)
			return; /* yikes! */

		for (bsp = bind_stack;bsp;bsptmp = bsp, bsp = bsp->next) 
		{
			if (slen == bsp->slen && !memcmp(bsp->sequence, compstr, slen)) 
			{
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
		if (bsp == NULL) 
		{
			say("no bindings for %s are on the stack", arg);
			new_free(&compstr);
			return;
		}

		/* okay, we need to push this key back in to place.  we have to
		* replicate bind_string since bind_string has some undesirable
		* effects.  */
		s = compstr;
		map = head_keymap;
		while (slen) 
		{
			if (*s < 0)
				return;

			key = &map[(unsigned char)*s++];
			slen--;
			if (slen) 
			{
				/* create a new map if necessary.. */
				if (key->map == NULL)
					key->map = map = construct_keymap(key);
				else
					map = key->map;
			} 
			else 
			{
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
	} 
	else if (type == STACK_LIST) 
	{
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
BUILT_IN_COMMAND(bindcmd) 
{
	const char *seq;
	char *function;
	int recurse = 0;
	char *cs;
	int	slen, retval;

	if ((seq = new_next_arg(args, &args)) == NULL) 
	{
		show_all_bindings(head_keymap, "", 0);
		return;
	}

	/* look for flags */
	if (*seq == '-') 
	{
		if (!my_strnicmp(seq + 1, "DEFAULTS", 1)) 
		{
			remove_bindings();
			init_binds();
			init_keys();
			init_termkeys();
			return;
		} 
		else if (!my_strnicmp(seq + 1, "SYMBOLIC", 1)) 
		{
			char * symbol;

			if ((symbol = new_next_arg(args, &args)) == NULL)
				return;
			if ((seq = get_term_capability(symbol, 0, 1)) == NULL) 
			{
				say("Symbolic name [%s] is not supported in your TERM type.",
				symbol);
				return;
			}
		} 
		else if (!my_strnicmp(seq + 1, "RECURSIVE", 1)) 
		{
			recurse = 1;
			if ((seq = new_next_arg(args, &args)) == NULL) 
			{
				show_all_bindings(head_keymap, "", 0);
				return;
			}
		}
	}

	if (!(cs = bind_string_compress(seq, &slen))) 
	{
		yell("BIND: couldn't compress sequence %s", seq);
		return;
	}

	if ((function = new_next_arg(args, &args)) == NULL) 
	{
		show_key(NULL, cs, slen, recurse);
		new_free(&cs);
		return;
	}

	/* bind_string() will check any errors for us. */
	retval = bind_compressed_string(cs, slen, function, *args ? args : NULL);
	if (retval) 
	{
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
static void	show_all_bindings (Key *map, const char *str, size_t len) 
{
	int c;
	char *newstr;
	Binding *self_insert;
	size_t size;

	self_insert = find_binding("SELF_INSERT");
	size = len + 2;
	newstr = alloca(size);
	strlcpy(newstr, str, size);

	/* show everything in our map.  recurse down. */
	newstr[len + 1] = '\0';
	for (c = 0; c <= KEYMAP_SIZE - 1;c++) 
	{
		newstr[len] = c;
		if (map[c].map || (map[c].bound && map[c].bound != self_insert))
			show_key(&map[c], newstr, len + 1, 1);
	}
}

static void	show_key (Key *key, const char *str, int slen, int recurse) 
{
	Binding *bp;
	char *clean = alloca(((strlen(str) + 1) * 2) + 1);

	if (key == NULL) 
	{
		key = find_sequence(head_keymap, str, slen);
		if (key == NULL)
		{
			yell("Can't find key sequence in show_key");	
			key = head_keymap;
		}
	}

	bp = key->bound;
	if (!bp && (!recurse || !key->map))
		say("[*] \"%s\" is bound to NOTHING",
		(slen ? bind_string_decompress(clean, str, slen) : str));
	else 
	{
		if (bp) 
		{
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
BUILT_IN_COMMAND(rbindcmd) 
{
	char *function;
	Binding *bp;

	if ((function = new_next_arg(args, &args)) == NULL)
		return;

	if ((bp = find_binding(function)) == NULL) 
	{
		if (!my_stricmp(function, "NOTHING"))
			say("You cannot list all unbound keys.");
		else
			say("No such function %s", function);
		return;
	}

	show_all_rbindings(head_keymap, "", 0, bp);
}

static void	show_all_rbindings (Key *map, const char *str, int len, Binding *binding) 
{
	int c;
	char *newstr;
	size_t size;

	size = len + 2;
	newstr = alloca(size);
	strlcpy(newstr, str, size);

	/* this time, show only those things bound to our function, and call on
	* ourselves to recurse instead. */
	newstr[len + 1] = '\0';
	for (c = 0; c <= KEYMAP_SIZE - 1;c++) 
	{
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
BUILT_IN_COMMAND(parsekeycmd) 
{
	Key fake;
	Binding *bp;
	char *func;

	if ((func = new_next_arg(args, &args)) != NULL) 
	{
		bp = find_binding(func);
		if (bp == NULL) 
		{
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

char *	bindctl (char *input)
{
    char *listc;
    char *retval = NULL;

    GET_FUNC_ARG(listc, input);
    if (!my_strnicmp(listc, "FUNCTION", 1)) {
	Binding *bp;
	char *func;

	GET_FUNC_ARG(func, input);
	bp = find_binding(func);

	GET_FUNC_ARG(listc, input);
	if (!my_strnicmp(listc, "CREATE", 1)) {
	    char *alias;

	    /* XXX Ugh, for backwards compatability */
	    if (*input == '"')
		GET_DWORD_ARG(alias, input)
	    else
		alias = input;

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
	Key *	key;
	char *	seq;
	int	slen;

	GET_DWORD_ARG(seq, input);
	GET_FUNC_ARG(listc, input);
	if (!my_stricmp(listc, "SET")) {
	    GET_FUNC_ARG(listc, input);

	    RETURN_INT(bind_string(seq, listc, (*input ? input : NULL)));
	}
	if (!(seq = bind_string_compress(seq, &slen)))
		RETURN_EMPTY;
	key = find_sequence(head_keymap, seq, slen);
	new_free(&seq);
	if (!my_stricmp(listc, "GET")) {
	    if (key == NULL || key->bound == NULL)
		RETURN_EMPTY;

	    retval = malloc_strdup(key->bound->name);
	    if (key->stuff)
		malloc_strcat_wordlist(&retval, " ", key->stuff);
	    RETURN_STR(retval);
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
	char *seq;
	int slen;
	Key *key;

	seq = new_next_arg(input, &input);
	if (seq == NULL) {
	    bindctl_getmap(head_keymap, "", 0, &retval);
	    RETURN_STR(retval);
	}
	seq = bind_string_compress(seq, &slen);
	key = find_sequence(head_keymap, seq, slen);

	listc = new_next_arg(input, &input);
	if (listc == NULL) {
	    if (key == NULL || key->map == NULL) {
		new_free(&seq);
		RETURN_EMPTY;
	    }
	    bindctl_getmap(key->map, seq, slen, &retval);
	    new_free(&seq);
	    RETURN_STR(retval);
	}
	new_free(&seq);
	if (!my_strnicmp(listc, "CLEAR", 1)) {
	    if (key == NULL || key->map == NULL)
		RETURN_INT(0);
	    remove_bindings_recurse(&key->map);
	    RETURN_INT(1);
	}
    }

    RETURN_EMPTY;
}

static void	bindctl_getmap (Key *map, const char *str, int len, char **ret) 
{
	int c;
	char *newstr;
	char *decomp;
	size_t size;

	size = len + 2;
	newstr = alloca(size);
	strlcpy(newstr, str, size);
	decomp = alloca(((len + 1) * 2) + 1);

	/* grab all keys that are bound, put them in ret, and continue. */
	newstr[len + 1] = '\0';
	for (c = 1; c <= KEYMAP_SIZE - 1;c++) 
	{
		newstr[len] = c;
		if (map[c].bound)
			malloc_strcat_wordlist(ret, " ", bind_string_decompress(decomp, newstr, len + 1));
		if (map[c].map)
			bindctl_getmap(map[c].map, newstr, len + 1, ret);
	}
}

#if 0
void    help_topics_bind (FILE *f)                                         
{
	Binding *b;

	for (b = binding_list; b; b = b->next)
	{
		if (b->func)
			fprintf(f, "bind %s\n", b->name);
	}
}                                                                               
#endif

