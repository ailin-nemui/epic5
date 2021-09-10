/*
 * hook.c: Does those naughty hook functions. 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1993, 2003 EPIC Software Labs.
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
#include "hook.h"
#include "ircaux.h"
#define __need_ArgList_t__
#include "alias.h"
#include "window.h"
#include "output.h"
#include "commands.h"
#include "ifcmd.h"
#include "stack.h"
#include "reg.h"
#include "functions.h"

/*
 * The various ON levels: SILENT means the DISPLAY will be OFF and it will
 * suppress the default action of the event, QUIET means the display will be
 * OFF but the default action will still take place, NORMAL means you will be
 * notified when an action takes place and the default action still occurs,
 * NOISY means you are notified when an action occur plus you see the action
 * in the display and the default actions still occurs 
 */
struct NoiseInfo {
	const char *name;
	int	display;	/*  0 = suppress display, 1 = don't */
	int	alert;		/*  1 = "ON MSG hooked", 0 = shut up */
	int	suppress;	/*  0 = don't suppress default action
				 *  1 = suppress default action
				 * -1 = Let on hook decide */
	int	value;
	char	identifier;
	int	custom;
};

struct NoiseInfo noise_info_templates[] = {
	{ "UNKNOWN", 0, 0, -1, 0, '?', 0 },
	{ "SILENT",  0, 0,  1, 1, '^', 0 },
	{ "QUIET",   0, 0,  0, 2, '-', 0 },
	{ "NORMAL",  0, 1,  0, 3, 0,   0 },
	{ "NOISY",   1, 1,  0, 4, '+', 0 },
	{ "SYSTEM",  1, 0,  1, 5, '%', 0 },
	{ NULL, 	 0, 0,  0, 0,   0, 0 }
};

#define HF_NORECURSE	0x0001

/* Hook: The structure of the entries of the hook functions lists */
typedef struct	hook_stru
{
	struct	hook_stru *next;

	int 	type;		/* /on #TYPE sernum nick (arglist) stuff */
	int 	sernum;		/* /on #type SERNUM nick (arglist) stuff */
	char *	nick;		/* /on #type sernum NICK (arglist) stuff */
	ArgList *arglist;	/* /on #type sernum nick (ARGLIST) stuff */
	char *	stuff;		/* /on #type sernum nick (arglist) STUFF */

	int	not;		/* /on #type sernum ^NICK */
	int	noisy;		/* /on #[^-+]TYPE sernum nick (arglist) stuff */
	int	flexible;	/* /on #type sernum 'NICK' (arglist) stuff */

	int	userial;	/* Unique serial for this hook */
	int	skip;		/* hook will be treated like it doesn't exist */
	char *	filename;	/* Where it was loaded */
}	Hook;

/* 
 * Current executing hook, yay! 
 * Silly name, but that can be fixed, can't it? :P
 */
struct Current_hook
{
	/* Which hook we're "under", if we are under a hook */
	struct Current_hook *under;
	
	/* Hook's userial */
	int userial;

	/* If set to 1, the chain for the current hook should halt */
	int halt;

	/* The hook's retval */
	int retval;

	/* The buffer (i.e. the arguments) of the hook */
	char *buffer;

	/* Set to 1 if buffer has been changed */
	int buffer_changed;

	/* 
	 * User-supplied information.
	 */
	char *user_supplied_info;
};

/* A list of all the hook functions available */
typedef struct Hookables
{
	const char *name;		/* The name of the hook type */
	Hook	*list;			/* The list of events for type */
	int	params;			/* Number of parameters expected */
	int	mark;			/* Hook type is currently active */
	unsigned flags;			/* Anything else needed */
	char *	implied;		/* Implied output if unhooked */
	int	implied_protect;	/* Do not re-expand implied hook */
} Hookables;

Hookables hook_function_templates[] =
{
	{ "ACTION",		NULL,	3,	0,	0,	NULL, 0 },
	{ "CHANNEL_CLAIM", 	NULL, 	2,  	0,  	0,  	NULL, 0 },
	{ "CHANNEL_LOST", 	NULL, 	2,  	0,  	0,  	NULL, 0 },
	{ "CHANNEL_NICK",	NULL,	3,	0,	0,	NULL, 0 },
	{ "CHANNEL_SIGNOFF",	NULL,	3,	0,	0,	NULL, 0 },
	{ "CHANNEL_SYNC",	NULL,	3,	0,	0,	NULL, 0 },
	{ "CONNECT",		NULL,	2,	0,	0,	NULL, 0 },
	{ "CTCP",		NULL,	4,	0,	0,	NULL, 0 },
	{ "CTCP_REPLY",		NULL,	4,	0,	0,	NULL, 0 },
	{ "CTCP_REQUEST",	NULL,	4,	0,	0,	NULL, 0 },
	{ "DCC_ACTIVITY",	NULL,	1,	0,	0,	NULL, 0 },
	{ "DCC_CHAT",		NULL,	2,	0,	0,	NULL, 0 },
        { "DCC_CONNECT",        NULL,   2,      0,      0,	NULL, 0 },
	{ "DCC_LIST",		NULL,	8,	0,	0,	NULL, 0 },
        { "DCC_LOST",           NULL,   2,      0,      0,	NULL, 0 },
	{ "DCC_OFFER", 		NULL,	2,	0,	0,	NULL, 0 },
	{ "DCC_RAW",		NULL,	3,	0,	0,	NULL, 0 },
        { "DCC_REQUEST",        NULL,   4,      0,      0,	NULL, 0 },
	{ "DISCONNECT",		NULL,	1,	0,	0,	NULL, 0 },
        { "ENCRYPTED_NOTICE",   NULL,   3,      0,      0,	NULL, 0 },
        { "ENCRYPTED_PRIVMSG",  NULL,   3,      0,      0,	NULL, 0 },
	{ "ERROR",		NULL,	1,	0,	0,	NULL, 0 },
	{ "EXEC",		NULL,	2,	0,	0,	NULL, 0 },
	{ "EXEC_ERRORS",	NULL,	2,	0,	0,	NULL, 0 },
	{ "EXEC_EXIT",		NULL,	3,	0,	0,	NULL, 0 },
	{ "EXEC_PROMPT",	NULL,	2,	0,	0,	NULL, 0 },
        { "EXIT",               NULL,   1,      0,      0,	NULL, 0 },
	{ "FLOOD",		NULL,	5,	0,	0,	NULL, 0 },
	{ "GENERAL_NOTICE",	NULL,	3,	0,	0,	NULL, 0 },
	{ "GENERAL_PRIVMSG",	NULL,	3,	0,	0,	NULL, 0 },
	{ "HELP",		NULL,	2,	0,	0,	NULL, 0 },
	{ "HOOK",		NULL,	1,	0,	0,	NULL, 0 },
	{ "IDLE",		NULL,	1,	0,	0,	NULL, 0 },
	{ "INPUT",		NULL,	1,	0,	HF_NORECURSE,	NULL, 0 },
	{ "INVITE",		NULL,	3,	0,	0,	NULL, 0 },
	{ "JOIN",		NULL,	4,	0,	0,	NULL, 0 },
	{ "KEYBINDING",		NULL,	3,	0,	0,	NULL, 0 },
	{ "KICK",		NULL,	3,	0,	0,	NULL, 0 },
	{ "KILL",		NULL,	5,	0,	0,	NULL, 0 },
	{ "LIST",		NULL,	3,	0,	0,	NULL, 0 },
	{ "MAIL",		NULL,	2,	0,	0,	NULL, 0 },
	{ "MODE",		NULL,	3,	0,	0,	NULL, 0 },
	{ "MODE_STRIPPED",	NULL,	3,	0,	0,	NULL, 0 },
	{ "MSG",		NULL,	2,	0,	0,	NULL, 0 },
	{ "MSG_GROUP",		NULL,	3,	0,	0,	NULL, 0 },
	{ "NAMES",		NULL,	2,	0,	0,	NULL, 0 },
	{ "NEW_NICKNAME",	NULL,	2,	0,	HF_NORECURSE,	NULL, 0 },
	{ "NICKNAME",		NULL,	2,	0,	0,	NULL, 0 },
	{ "NOTE",		NULL,	3,	0,	0,	NULL, 0 },
	{ "NOTICE",		NULL,	2,	0,	0,	NULL, 0 },
	{ "NOTIFY_SIGNOFF",	NULL,	1,	0,	0,	NULL, 0 },
	{ "NOTIFY_SIGNON",	NULL,	2,	0,	0,	NULL, 0 },
	{ "NUMERIC",		NULL,	3,	0,	0,	NULL, 0 },
	{ "ODD_SERVER_STUFF",	NULL,	3,	0,	0,	NULL, 0 },
	{ "OPERWALL",		NULL,	2,	0,	0,	NULL, 0 },
	{ "OPER_NOTICE",	NULL,	2,	0,	0,	NULL, 0 },
	{ "PART",		NULL,	2,	0,	0,	NULL, 0 },
	{ "PONG",		NULL,	2,	0,	0,	NULL, 0 },
	{ "PUBLIC",		NULL,	3,	0,	0,	NULL, 0 },
	{ "PUBLIC_MSG",		NULL,	3,	0,	0,	NULL, 0 },
	{ "PUBLIC_NOTICE",	NULL,	3,	0,	0,	NULL, 0 },
	{ "PUBLIC_OTHER",	NULL,	3,	0,	0,	NULL, 0 },
	{ "RAW_IRC",		NULL,	1,	0,	0,	NULL, 0 },
	{ "RAW_IRC_BYTES",	NULL,	1,	0,	0,	NULL, 0 },
	{ "REDIRECT",		NULL,	2,	0,	HF_NORECURSE,	NULL, 0 },
	{ "SEND_ACTION",	NULL,	2,	0,	HF_NORECURSE,	NULL, 0 },
	{ "SEND_CTCP",		NULL,	3,	0,	HF_NORECURSE,	NULL, 0 },
	{ "SEND_DCC_CHAT",	NULL,	2,	0,	HF_NORECURSE,	NULL, 0 },
	{ "SEND_EXEC",		NULL,	2,	0,	HF_NORECURSE,	NULL, 0 },
	{ "SEND_MSG",		NULL,	2,	0,	HF_NORECURSE,	NULL, 0 },
	{ "SEND_NOTICE",	NULL,	2,	0,	HF_NORECURSE,	NULL, 0 },
	{ "SEND_PUBLIC",	NULL,	2,	0,	HF_NORECURSE,	NULL, 0 },
	{ "SEND_TO_SERVER",	NULL,	3,	0,	HF_NORECURSE,	NULL, 0 },
	{ "SERVER_ESTABLISHED",	NULL,	2,	0,	0,	NULL, 0 },
	{ "SERVER_LOST",	NULL,	2,	0,	0,	NULL, 0 },
	{ "SERVER_NOTICE",	NULL,	1,	0,	0,	NULL, 0 },
	{ "SERVER_SSL_EVAL",	NULL,	6,	0,	0,	NULL, 0 },
	{ "SERVER_STATE",	NULL,	3,	0,	0,	NULL, 0 },
	{ "SERVER_STATUS",	NULL,	3,	0,	0,	NULL, 0 },
	{ "SET",		NULL,	2,	0,	0,	NULL, 0 },
	{ "SIGNAL",		NULL,	1,	0,	0,	NULL, 0 },
	{ "SIGNOFF",		NULL,	1,	0,	0,	NULL, 0 },
	{ "SILENCE",		NULL,	2,	0,	0,	NULL, 0 },
	{ "SSL_SERVER_CERT",	NULL,	9,	0,	0,	NULL, 0 },
	{ "STATUS_UPDATE",	NULL,	3,	0,	0,	NULL, 0 },
	{ "SWITCH_CHANNELS",	NULL,	3,	0,	0,	NULL, 0 },
	{ "SWITCH_QUERY",	NULL,	3,	0,	0,	NULL, 0 },
	{ "SWITCH_WINDOWS",	NULL,	4,	0,	0,	NULL, 0 },
	{ "TIMER",		NULL,	1,	0,	0,	NULL, 0 },
	{ "TOPIC",		NULL,	2,	0,	0,	NULL, 0 },
	{ "UNKNOWN_COMMAND",	NULL,	2,	0,	HF_NORECURSE, 	NULL, 0},
	{ "UNKNOWN_SET",	NULL,	2,	0,	HF_NORECURSE,	NULL, 0},
	{ "UNLOAD",		NULL,	1,	0,	0,	NULL, 0 },
	{ "WALL",		NULL,	2,	0,	0,	NULL, 0 },
	{ "WALLOP",		NULL,	3,	0,	0,	NULL, 0 },
	{ "WHO",		NULL,	6,	0,	0,	NULL, 0 },
	{ "WINDOW",		NULL,	2,	0,	HF_NORECURSE,	NULL, 0 },
	{ "WINDOW_COMMAND",	NULL,	1, 	0,	0,	NULL, 0 },
	{ "WINDOW_CREATE",	NULL,	1, 	0,	0,	NULL, 0 },
	{ "WINDOW_BEFOREKILL",	NULL,	1,	0,	0,	NULL, 0 },
	{ "WINDOW_KILL",	NULL,	2,	0,	0,	NULL, 0 },
	{ "WINDOW_NOTIFIED",	NULL,	2,	0,	HF_NORECURSE,	NULL, 0 },
	{ "WINDOW_SERVER",	NULL,	3,	0,	0,	NULL, 0 },
	{ "YELL",		NULL,	1,	0,	0,	NULL, 0 },
};

static Hookables *hook_functions = NULL;
static int	 hook_functions_initialized = 0;

static Hook **hooklist = NULL;
static int hooklist_size = 0;
static int	last_created_hook = -2;
static struct Current_hook *current_hook = NULL;
/*
 * If deny_all_hooks is set to 1, no action is taken for any hook.
 */
int deny_all_hooks = 0;

static	struct NoiseInfo **	noise_info = NULL;
static	int 			noise_level_num = 0;
static	int 			default_noise;
static	const char *		current_implied_on_hook = NULL;

extern char *	    function_cparse	(char *);
static void 	    add_to_list 	(Hook **list, Hook *item);
static Hook *	    remove_from_list 	(Hook **list, char *item, int sernum);

static void	initialize_hook_functions (void)
{
	int	i, b;
	char *  p;

	hook_functions = malloc(NUMBER_OF_LISTS * sizeof(Hookables));
	p = malloc(4050);

	for (i = 0; i < FIRST_NAMED_HOOK; i++, p += 4)
	{
		snprintf(p, 4, "%03d", i);
		hook_functions[i].name = p;
		hook_functions[i].list = NULL;
		hook_functions[i].params = 1;
		hook_functions[i].mark = 0;
		hook_functions[i].flags = 0;
		hook_functions[i].implied = NULL;
		hook_functions[i].implied_protect = 0;
	}

	for (b = 0, i = FIRST_NAMED_HOOK; i < NUMBER_OF_LISTS; b++, i++)
	{
		hook_functions[i].name = hook_function_templates[b].name;
		hook_functions[i].list = hook_function_templates[b].list;
		hook_functions[i].params = hook_function_templates[b].params;
		hook_functions[i].mark = hook_function_templates[b].mark;
		hook_functions[i].flags = hook_function_templates[b].flags;
		hook_functions[i].implied = NULL;
		hook_functions[i].implied_protect = 0;
	}

	if (noise_info == NULL)
	{
		for (b = 0; noise_info_templates[b].name != NULL; b++)
		{
			noise_level_num++;
			if (noise_level_num == 1)
				noise_info = new_malloc(sizeof(struct NoiseInfo *));
			else
				RESIZE(noise_info, struct NoiseInfo *, noise_level_num);
			
			noise_info[b] = new_malloc(sizeof(struct NoiseInfo));
			noise_info[b]->name = noise_info_templates[b].name;
			noise_info[b]->display = noise_info_templates[b].display;
			noise_info[b]->alert = noise_info_templates[b].alert;
			noise_info[b]->suppress = noise_info_templates[b].suppress;
			noise_info[b]->value = noise_info_templates[b].value;
			noise_info[b]->identifier = noise_info_templates[b].identifier;
			noise_info[b]->custom = 0;
			if (!strcmp(noise_info[b]->name, "NORMAL"))
				default_noise = b;
		}
	}
	hook_functions_initialized = 1;
}

/*
 * 	Add a hook to the hooklist
 *	Returns -1 on error, or size of list if successful.
 */
static int	inc_hooklist (int size)
{
	int newsize, n;
	if (size < 1)
		return -1;
	newsize = hooklist_size + size;
	if (hooklist_size == 0)
		hooklist = new_malloc(sizeof(Hook) * newsize);
	else
		RESIZE(hooklist, Hook, newsize);
	for (n = hooklist_size; n < newsize; n++)
		hooklist[n] = NULL;
	hooklist_size = newsize;
	return hooklist_size;
}

/*
 * Removes n NULL-pointers from the end of hooklist.
 * Returns -1 on error, and size of list if successful
 */
static int	dec_hooklist (int n)
{
	int size, newsize;
	if (n < 1)
	{
		size = hooklist_size -1;
		while (hooklist[size] == NULL)
			size--;
		n = hooklist_size - size - 1;
	}
	size = hooklist_size;
	newsize = size - n;
	for (n = newsize; n < hooklist_size; n++)
		if (hooklist[n] != NULL)
			return -1;
	RESIZE(hooklist, Hook, newsize);
	hooklist_size = newsize;
	return hooklist_size;
}

/*
 * Will return the next empty slot in the hooklist
 */
static int	next_empty_hookslot (void)
{
	int n;
	for (n = 0; n < hooklist_size; n++)
		if (hooklist[n] == NULL)
			break;
	return n;
}

/*
 * find_hook: returns the numerical value for a specified hook name
 */
static int 	find_hook (char *name, int *first, int quiet)
{
	int 	which = INVALID_HOOKNUM, i, cnt;
	size_t	len;

	if (first)
		*first = -1;

	if (!name || !(len = strlen(name)))
	{
		if (!quiet)
			say("You must specify an event type!");
		return INVALID_HOOKNUM;
	}

	upper(name);

	for (cnt = 0, i = FIRST_NAMED_HOOK; i < NUMBER_OF_LISTS; i++)
	{
		if (!strncmp(name, hook_functions[i].name, len))
		{
			if (first && *first == -1)
				*first = i;

			if (strlen(hook_functions[i].name) == len)
			{
				cnt = 1;
				which = i;
				break;
			}
			else
			{
				cnt++;
				which = i;
			}
		}
		else if (cnt)
			break;
	}

	if (cnt == 0)
	{
		if (is_number(name))
		{
			which = atol(name);

			if ((which < 0) || (which >= FIRST_NAMED_HOOK))
			{
				if (!quiet)
					say("Numerics must be between 001 and %3d", FIRST_NAMED_HOOK - 1);
				return INVALID_HOOKNUM;
			}
		}
		else
		{
			if (!quiet)
				say("No such ON function: %s", name);
			return INVALID_HOOKNUM;
		}
	}
	else if (cnt > 1)
	{
		if (!quiet)
			say("Ambiguous ON function: %s", name);
		return INVALID_HOOKNUM;
	}

	return which;
}




/* * * * * ADDING A HOOK * * * * * */
/*
 * add_hook: Given an index into the hook_functions array, this adds a new
 * entry to the list as specified by the rest of the parameters.  The new
 * entry is added in alphabetical order (by nick). 
 */
static int	add_hook (int which, char *nick, ArgList *arglist, char *stuff, int noisy, int not, int sernum, int flexible)
{
	Hook	*new_h;
	
	if (!(new_h = remove_from_list(&hook_functions[which].list, nick, sernum)))
	{
		new_h = (Hook *)new_malloc(sizeof(Hook));
		new_h->nick = NULL;
		new_h->stuff = NULL;
		new_h->filename = NULL;
	
		if ((new_h->userial = next_empty_hookslot()) == hooklist_size)
			inc_hooklist(3);
	}

	new_h->type = which;
	malloc_strcpy(&new_h->nick, nick);
	malloc_strcpy(&new_h->stuff, stuff);
	new_h->noisy = noisy;
	new_h->not = not;
	new_h->sernum = sernum;
	new_h->flexible = flexible;
	new_h->skip = 0;
	new_h->arglist = arglist;
	if (current_package())
	    malloc_strcpy(&new_h->filename, current_package());
	new_h->next = NULL;

	upper(new_h->nick);

	hooklist[new_h->userial] = new_h;
	add_to_list(&hook_functions[which].list, new_h);

	last_created_hook = new_h->userial;

	return new_h->userial;
}




/* * * * * * REMOVING A HOOK * * * * * * * */
static void remove_hook (int which, char *nick, int sernum, int quiet)
{
	Hook	*tmp,
		*next;
	Hook 	*prev = NULL,
		*top = NULL;

	if (nick)
	{
		if ((tmp = remove_from_list(&hook_functions[which].list, nick, sernum)))
		{
			if (!quiet)
				say("%c%s%c removed from %s list", 
					(tmp->flexible?'\'':'"'), nick,
					(tmp->flexible?'\'':'"'),
					hook_functions[which].name);

			new_free(&(tmp->nick));
			new_free(&(tmp->stuff));
			new_free(&(tmp->filename));
			if (tmp->arglist != NULL)
                    destroy_arglist(&(tmp->arglist));
					
	
			hooklist[tmp->userial] = NULL;
			if (tmp->userial == hooklist_size -1)
			{
				dec_hooklist(0);
			}
			tmp->next = NULL;
			new_free((char **)&tmp); /* XXX why? */
		}
		else if (!quiet)
			say("\"%s\" is not on the %s list", nick,
					hook_functions[which].name);
		return;
	}

	top = hook_functions[which].list;
	for (tmp = top; tmp; tmp = next)
	{
		next = tmp->next;

		/* 
		 * If given a non-zero sernum, then we clean out
		 * only those hooks that are at that level.
		 */
		if (sernum && tmp->sernum != sernum)
		{
			prev = tmp;
			continue;
		}

		if (prev)
			prev->next = tmp->next;
		else
			top = tmp->next;
		tmp->not = 1;
		new_free(&(tmp->nick));
		new_free(&(tmp->stuff));
		new_free(&(tmp->filename));
		tmp->next = NULL;
		
		new_free((char **)&tmp);
	}
	hook_functions[which].list = top;
	if (!quiet)
	{
		if (sernum)
			say("The %s <%d> list is empty", hook_functions[which].name, sernum);
		else
			say("The %s list is empty", hook_functions[which].name);
	}
}

/* Used to bulk-erase all of the currently scheduled ONs */
void    flush_on_hooks (void)
{
        int x;
        int old_display = window_display;

        window_display = 0;
        for (x = 0; x < NUMBER_OF_LISTS; x++)
		remove_hook(x, NULL, 0, 1); 
        window_display = old_display;

		new_free(&hooklist);
		hooklist_size = 0;
		last_created_hook = -2;

}

void	unload_on_hooks (char *filename)
{
	int		x;
	Hook		*list, *next;

	int old_display = window_display;
	window_display = 0;

	for (x = 0; x < NUMBER_OF_LISTS; x++)
	{
		for (list = hook_functions[x].list; list; list = next)
		{
		    next = list->next;
		    if (list->filename && !strcmp(list->filename, filename))
			remove_hook(x, list->nick, list->sernum, 1);
		}
	}

	window_display = old_display;
}


/* * * * * * SHOWING A HOOK * * * * * * */
/* show_hook shows a single hook */
static void 	show_hook (Hook *list, const char *name)
{
	char *arglist;

	if ((arglist = print_arglist(list->arglist)))
	{
		say ("[%s] On %s from %c%s%c (%s) do %s [%s%s] <%d/#%d>",
			empty(list->filename) ? "*" : list->filename,
			name,
			(list->flexible ? '\'' : '"'),
			list->nick,
			(list->flexible ? '\'' : '"'),
			arglist,
			(list->not ? "nothing" : list->stuff),
			noise_info[list->noisy]->name,
			list->skip ? "/DISABLED" : "",
			list->sernum, list->userial);
		new_free(&arglist);
	}
	else
		say("[%s] On %s from %c%s%c do %s [%s%s] <%d/#%d>",
		    empty(list->filename) ? "*" : list->filename,
	    	name,
  	  	(list->flexible ? '\'' : '"'), list->nick, 
 		   (list->flexible ? '\'' : '"'), 
    		(list->not ? "nothing" : list->stuff),
	 	   noise_info[list->noisy]->name,
		list->skip ? "/DISABLED" : "",
	    	list->sernum, list->userial);
}

/*
 * show_list: Displays the contents of the list specified by the index into
 * the hook_functions array.  This function returns the number of entries in
 * the list displayed 
 */
static int show_list (int which)
{
	Hook	*list;
	int	cnt = 0;

	/* Less garbage when issueing /on without args. (lynx) */
	for (list = hook_functions[which].list; list; list = list->next, cnt++)
		show_hook(list, hook_functions[which].name);
	return (cnt);
}

static int show_all_numerics (int numeric)
{
	int	cnt = 0;
	int	tot = 0;

	for (cnt = 0; cnt < FIRST_NAMED_HOOK; cnt++)
		tot += show_list(cnt);
	return tot;
}



/* * * * * * * * EXECUTING A HOOK * * * * * * */
#define NO_ACTION_TAKEN		-1
#define SUPPRESS_DEFAULT	 0
#define DONT_SUPPRESS_DEFAULT	 1
#define RESULT_PENDING		 2
static int 	do_hook_internal (int which, char **result, const char *format, va_list args);

/*
 * do_hook: This is what gets called whenever a MSG, INVITES, WALL, (you get
 * the idea) occurs.  The nick is looked up in the appropriate list. If a
 * match is found, the stuff field from that entry in the list is treated as
 * if it were a command. First it gets expanded as though it were an alias
 * (with the args parameter used as the arguments to the alias).  After it
 * gets expanded, it gets parsed as a command.  This will return as its value
 * the value of the noisy field of the found entry, or -1 if not found. 
 */
/* huh-huh.. this sucks.. im going to re-write it so that it works */
int	do_hook (int which, const char *format, ...)
{
	char *	result = NULL;
	int	retval;
	va_list	args;

	va_start(args, format);
	retval = do_hook_internal(which, &result, format, args);
	new_free(&result);
	va_end(args);
	return retval;
}

int	do_hook_with_result (int which, char **result, const char *format, ...)
{
	int	retval;
	va_list	args;

	va_start(args, format);
	retval = do_hook_internal(which, result, format, args);
	va_end(args);
	return retval;
}

static int 	do_hook_internal (int which, char **result, const char *format, va_list args)
{
	Hook		*tmp;
	const char	*name 		= (char *) 0;
	int		retval;
	char *		buffer		= NULL;
	unsigned	display		= window_display;
	char *		stuff_copy;
	int		noise, old;
	char		quote;
	int		serial_number;
	struct Current_hook *hook;
	Hookables *	h;

	*result = NULL;

	if (!hook_functions_initialized)
		initialize_hook_functions();
	h = &hook_functions[which];

	/*
	 * Press the buffer using the specified format string and args
	 * We have to do this first because even if there is no /on, the
	 * caller might still want to know what the result of $* is.
	 */
	if (!format)
		panic(1, "do_hook: format is NULL (hook type %d)", which);

	malloc_vsprintf(&buffer, format, args);


	/*
	 * Decide whether to post this event.  Events are suppressed if:
	 *   1) $hookctl(DENY_ALL_HOOKS 1) has been turned on
	 *   2) There are no /on's and no implied hooks
	 *   3) The /on has recursed and that is forbidden.
	 */
	if (deny_all_hooks || 
	    (!h->list && !h->implied) ||
	    (h->mark && h->flags & HF_NORECURSE))
	{
		retval = NO_ACTION_TAKEN;
		*result = buffer;
		return retval;
	}

	/*
	 * If there are no /on's, but there is an implied hook, skip
	 * right to the implied hook.
	 */
	if (!h->list && h->implied)
	{
		retval = NO_ACTION_TAKEN;
		*result = buffer;
		goto implied_hook;
	}

	/*
	 * Set current_hook
	 */
	hook = new_malloc(sizeof(struct Current_hook));
	hook->userial = -1;
	hook->halt = 0;
	hook->under = current_hook;
	hook->retval = DONT_SUPPRESS_DEFAULT;
	hook->buffer = buffer;
	hook->buffer_changed = 0;
	hook->user_supplied_info = NULL;
	current_hook = hook;

	/*
	 * Mark the event as being executed.  This is used to suppress
 	 * unwanted recursion in some /on's.
	 */
	if (which >= 0)
		h->mark++;

        serial_number = INT_MIN;
        for (;!hook->halt;serial_number++)
	{
	    for (tmp = h->list; !hook->halt && tmp; tmp = tmp->next)
            {
		Hook *besthook = NULL;
		ArgList *tmp_arglist;
		char *buffer_copy;
		int bestmatch = 0;
		int currmatch;

		if (tmp->sernum < serial_number)
		    continue;

		if (tmp->sernum > serial_number)
		    serial_number = tmp->sernum;

		for (; 
			!hook->halt && tmp && tmp->sernum == serial_number && !tmp->skip;
			tmp = tmp->next)
		{
		    if (tmp->flexible)
		    {
			/* XXX What about context? */
			char *tmpnick;
			tmpnick = expand_alias(tmp->nick, hook->buffer);
		        currmatch = wild_match(tmpnick, hook->buffer);
			new_free(&tmpnick);
		    }
		    else
		        currmatch = wild_match(tmp->nick, hook->buffer);

		    if (currmatch > bestmatch)
		    {
			besthook = tmp;
			bestmatch = currmatch;
		    }
		}

		/* If nothing matched, then run the next serial number. */
		if (!besthook)
			break;

		/* Run the hook */
		tmp = besthook;

		/*
		 * If the winning event is a "excepting" event, then move 
		 * on to the next serial number.
		 */
		if (tmp->not)
			break;

		/* Copy off everything important from 'tmp'. */
		noise = tmp->noisy;
		if (!name)
			name = LOCAL_COPY(h->name);
		stuff_copy = LOCAL_COPY(tmp->stuff);
		quote = tmp->flexible ? '\'' : '"';

		hook->userial = tmp->userial;
		tmp_arglist = clone_arglist(tmp->arglist);

		/*
		 * YOU CAN'T TOUCH ``tmp'' AFTER THIS POINT!!!
		 */

		/* 
		 * Check to see if this hook is supposed to supress the
		 * default action for the event.
		 */
		if (noise_info[noise]->suppress == 1 && serial_number == 0)
			hook->retval = SUPPRESS_DEFAULT;
		else if (noise_info[noise]->suppress == -1 && serial_number == 0)
			hook->retval = RESULT_PENDING;

		/*
		 * If this is a NORMAL or NOISY hook, then we tell the user
		 * that we're going to execute the hook.
		 */
		if (noise_info[noise]->alert)
			say("%s #%d activated by %c%s%c", 
				name, hook->userial, quote, hook->buffer, quote);

		/*
		 * Save some information that may be reset in the 
		 * execution, turn off the display if the user specified.
		 */
		if (noise_info[noise]->display == 0)
			window_display = 0;
		else
			window_display = 1;
		old = system_exception;

		buffer_copy = LOCAL_COPY(hook->buffer);

		if (hook->retval == RESULT_PENDING)
		{
			char *xresult;

			xresult = call_user_function(name, stuff_copy,
							buffer_copy,
							tmp_arglist);

			if (xresult && atol(xresult))
				hook->retval = SUPPRESS_DEFAULT;
			else
				hook->retval = DONT_SUPPRESS_DEFAULT;
			if (tmp_arglist)
				destroy_arglist(&tmp_arglist);
			new_free(&xresult);
		}
		else
		{
			/*
			 * Ok.  Go and run the code.  It is imperitive to note
			 * that "tmp" may be deleted by the code executed here,
			 * so it is absolutely forbidden to reference "tmp" 
			 * after this point.
			 */
			call_user_command(name, stuff_copy, 
						buffer_copy, tmp_arglist);
			if (tmp_arglist)
				destroy_arglist(&tmp_arglist);
		}

		/*
		 * Clean up the stuff that may have been mangled by the
		 * execution.
		 */
		system_exception = old;
		window_display = display;

		/* Move onto the next serial number. */
		break;
	    }

	    /* If 'tmp' is null here, we've processed all of them. */
	    if (!tmp)
		break;
	}

	/*
	 * Mark the event as not currently being done here.
	 */
	if (which >= 0)
		h->mark--;

	/*
	 * Reset current_hook to its previous value.
	 */
	retval = hook->retval;
	*result = hook->buffer;
	hook->buffer = NULL;
	if (hook->user_supplied_info)
		new_free(&hook->user_supplied_info);
	current_hook = hook->under;
	new_free(&hook);

	/*
	 * And return the user-specified suppression level
	 */
	if (retval == SUPPRESS_DEFAULT || !h->implied)
		return retval;

    implied_hook:
#ifdef IMPLIED_ON_HOOKS
    do
    {
	char *	func_call = NULL;
	char *	func_retval;
	char	my_buffer[BIG_BUFFER_SIZE * 10 + 1];
	const char *old_current_implied_on_hook = current_implied_on_hook;

	if (!h->implied)
		break;

	if (!format)
		panic(1, "do_hook: format is NULL");

	strlcpy(my_buffer, *result, sizeof(my_buffer));

	if (which >= 0)
		h->mark++;

	current_implied_on_hook = h->name;
	if (h->implied_protect)
	{
	    malloc_sprintf(&func_call, "\"%s\" %s", h->implied, my_buffer);
	    func_retval = function_cparse(func_call);
	}
	else
	{
	    malloc_sprintf(&func_call, "cparse(\"%s\" $*)", h->implied);
	    func_retval = call_function(func_call, my_buffer);
	}
	current_implied_on_hook = old_current_implied_on_hook;

	put_echo(func_retval);

	new_free(&func_call);
	new_free(&func_retval);
	retval = SUPPRESS_DEFAULT;

	if (which >= 0)
		h->mark--;
    }
    while (0);
#endif

	if (!result || !*result)
		panic(1, "do_hook: Didn't set result anywhere.");
	return retval;
}

/* 
 * shook: the SHOOK command -- this probably doesnt belong here,
 * and shook is probably a stupid name.  It simply asserts a fake
 * hook event for a given type.  Fraught with peril!
 */
BUILT_IN_COMMAND(shookcmd)
{
	int which;
	char *arg = next_arg(args, &args);

	if ((which = find_hook(arg, NULL, 0)) == INVALID_HOOKNUM)
		return;
	else
		do_hook(which, "%s", args);
}


/* * * * * * SCHEDULING AN EVENT * * * * * * * */
/*
 * The ON command:
 * Format:		/ON [#][+-^]TYPE ['] [SERNUM] NICK ['] [{] STUFF [}]
 *
 * The "ON" command mainly takes three arguments.  The first argument
 * is the "type" of callback that you want to schedule.  This is either
 * a three digit number, of it is one of the strings so enumerated at the
 * top of this file in hook_list.  The second argument is the "nick" or
 * "pattern" that is to be used to match against future events.  If the
 * "nick" matches the text that is later passed to do_hook() with the given
 * "type", then the commands in "stuff" will be executed.
 *
 * If "nick" is enclosed in single quotes ('), then it is a "flexible" 
 * pattern, and will be expanded before it is matched against the text
 * in do_hook.  Otherwise, the string so specified is "static", and is
 * used as-is in do_hook().
 *
 * Within each type, there are at least 65,535 different "serial numbers",
 * (there actually are MAX_INT of them, but by convention, only 16 bit
 * serial numbers are used, from -32,768 to 32,767) which may be used to 
 * schedule any number of events at the given serial number.
 *
 * Each time an assertion occurs for a given "type", at most one of the
 * scheduled events is executed for each of the distinct serial numbers that
 * are in use for that event.  The event to be executed is the one at a 
 * given serial number that "best" matches the text passed to do_hook().
 * While in theory, up to MAX_INT events could be executed for a given single
 * assertion, in practice, a hard limit of 2048 events per assertion is 
 * enforced.
 * 
 * The runtime behavior of the event being scheduled can be modified by
 * specifying a character at the beginning of the "type" argument.  If you
 * want to schedule an event at a serial number, then the first character
 * must be a hash (#).  The argument immediately FOLLOWING the "type"
 * argument, and immediately PRECEEDING the "nick" argument must be an 
 * integer number, and is used for the serial number for this event.
 *
 * The "verbosity" of the event may also be modified by specifying at most
 * one of the following characters:
 *	A caret (^) is the SILENT level, and indicates that the event is to
 *		be executed with no output (window_display is turned off),
 *		and the "default action" (whatever that is) for the event is
 *		to be suppressed.  The default action is actually only 
 *		suppressed if the SILENT level is specified for serial number
 *		zero.  This is the most common level used for overriding the
 *		output of most /on's.
 *	A minus (-) is the QUIET level, and is the same as the SILENT level,
 *		except that the default action (whatever that is) is not to
 *		be suppressed.
 *	No character is the "normal" case, and is the same as the "minus"
 *		level, with the addition that the client will inform you that
 *		the event was executed.  This is useful for debugging.
 *	A plus (+) is the same as the "normal" (no character specified),
 *		except that the output is not suppressed (window_display is 
 *		not changed.)
 */
BUILT_IN_COMMAND(oncmd)
{
	char	*func,
		*nick,
		*serial		= NULL;
	int	noisy;
	int	not		= 0,
		sernum		= 0,
		rem		= 0,
		supp		= 0,
		which		= INVALID_HOOKNUM;
	int	flex		= 0;
	int userial;
	char	type;
	int	first;
	ArgList *arglist = NULL;
	char *str;	
	if (!hook_functions_initialized)
		initialize_hook_functions();

	/*
	 * Get the type of event to be scheduled
	 */
	if ((func = next_arg(args, &args)) != NULL)
	{
		int v;
		/*
		 * Check to see if this has a serial number.
		 */
		if (*func == '#')
		{
			if (!(serial = next_arg(args, &args)))
			{
				say("No serial number specified");
				return;
			}
			sernum = atol(serial);
			func++;
		}

		/*
		 * Get the verbosity level, if any.
		 */
		noisy = default_noise;

		for (v = 0; v < noise_level_num; v++)
		{
			if (noise_info[v]->identifier != 0 &&
				noise_info[v]->identifier == *func)
				break;
		}
		if (v != noise_level_num)
		{
			noisy = noise_info[v]->value;
			func++;
		}
		
		/*
		 * Check to see if the event type is valid
		 */
		if ((which = find_hook(func, &first, 0)) == INVALID_HOOKNUM)
		{
			/*
			 * Ok.  So either the user specified an invalid type
			 * or they specified an ambiguous type.  Either way,
			 * we're not going to be going anywhere.  So we have
			 * free reign to mangle 'args' at this point.
			 */

			int len;

			/*
			 * If first is -1, then it was an unknown type.
			 * An error has already been output, just return here
			 */
			if (first == -1)
				return;

			/*
			 * Otherwise, its an ambiguous type.  If they were 
			 * trying to register the hook, then they've already
			 * gotten the error message, just return;
			 */
			if (new_new_next_arg_count(args, &args, &type, 1))
				return;

			/*
			 * Ok.  So they probably want a listing.
			 */
			len = strlen(func);
			while (!my_strnicmp(func, hook_functions[first].name, len))
			{
			    if (!show_list(first))
				say("The %s list is empty.", 
					hook_functions[first].name);
			    first++;
			}

			return;
		}

		/*
		 * If sernum is 0 and serial is "+" or "-" get a serial
		 * number for the event type in question
		 */
		if (sernum == 0 && serial != NULL) {
		    if (!strcmp(serial, "+"))
			sernum = hook_find_free_serial(1, 0, which);
		    else if (!strcmp(serial, "-"))
			sernum = hook_find_free_serial(-1, 0, which);
		}

		/*
		 * Check to see if this is a removal event or if this
		 * is a negated event.
		 */
		switch (*args)
		{
			case '-':
				rem = 1;
				args++;
				break;
			case '^':
				supp = 1;
				args++;
				break;
			case '!':
				not = 1;
				args++;
				break;
		}

		/*
		 * Grab the "nick"
		 */
		if ((nick = new_new_next_arg_count(args, &args, &type, 1)))
		{
			char *exp;

			nick = malloc_strdup(nick);
			
			/*
			 * If nick is empty, something is very wrong.
			 */
			if (!*nick)
			{
				say("No expression specified");
				new_free(&nick);
				return;
			}

			/*
			 * If we're doing a removal, do the deed.
			 */
			if (rem)
			{
				remove_hook(which, nick, sernum, 0);
				new_free(&nick);
				return;
			}

			/*
			 * Take a note if its flexible or not.
			 */
			if (type == '\'')
				flex = 1;
			else
				flex = 0;

			
			/*
			 * If this is a suppressive event, then we dont want
			 * to take any action for it.
			 */
			if (supp)
				args = endstr(args);


			/*
			 * Slurp up any whitespace after the nick
			 */
			while (my_isspace(*args))
				args++;


			/* Ripping the alias.c-stuff mercyless */
			if (*args == '(')
			{
				ssize_t span;
				args++;
				if ((span = MatchingBracket(args, '(', ')')) < 0)
				{
					say("Unmatched lparen in HOOK <-> to be fixed");
					new_free(&nick);
					return;
				}
				else
				{
					exp = args + span;
					*exp++ = 0;
					while (*exp && my_isspace(*exp))
						exp++;
					while (*args && my_isspace(*args))
						args++;
					/* Arglist stuff */
					arglist = parse_arglist(args);
					args = exp;
				}
				
			}

			/*
			 * Then slurp up the body ("text")
			 */
			if (*args == '{') /* } */
			{
				if (!(exp = next_expr(&args, '{'))) /* } */
				{
					say("Unmatched brace in ON");
					new_free(&nick);
					destroy_arglist(&arglist);
					return;
				}
			}
			else
				exp = args;

			/*
			 * Schedule the event
			 */
			userial = add_hook(which, nick, arglist, exp, noisy, not, sernum, flex);

			/*
			 * Tell the user that we're done.
			 */
			str = print_arglist(arglist);
			if (str)
			{
				if (which < 0)
					say ("On %3.3u from %c%s%c (%s) do %s [%s] <%d/#%d>",
					-which, type, nick, type, str, (not ? "nothing" : exp),
					noise_info[noisy]->name, sernum, userial);
				else
					say ("On %s from %c%s%c (%s) do %s [%s] <%d/#%d>",
						hook_functions[which].name,
						type, nick, type, str,
						(not ? "nothing" : exp),
						noise_info[noisy]->name, sernum, userial);
			}
			else
			{
				if (which < 0)
					say("On %3.3u from %c%s%c do %s [%s] <%d/#%d>",
					    -which, type, nick, type, 
					    (not ? "nothing" : exp),
					    noise_info[noisy]->name, sernum, userial);
				else
					say("On %s from %c%s%c do %s [%s] <%d/#%d>",
						hook_functions[which].name, 
						type, nick, type,
						(not ? "nothing" : exp),
						noise_info[noisy]->name, sernum, userial);
			}
			/*
			 * Clean up after the nick
			 */
			new_free(&nick);
		}

		/*
		 * No "nick" argument was specified.  That means the user
		 * either is deleting all of the events of a type, or it
		 * wants to list all the events of a type.
		 */
		else
		{
			/*
			 * if its a removal, do the deed
			 */
			if (rem)
			{
				remove_hook(which, (char *) 0, sernum, 0);
				return;
			}
	
			/*
			 * The help files say that an "/on 0" shows all
			 * of the numeric ONs.  Since the ACTION hook is
			 * number 0, we have to check to see if the first
			 * character of "func" is a zero or not.  If it is,
			 * we output all of the numeric functions.
			 */
			if (!strcmp(func, "0"))
			{
				if (!show_all_numerics(0))
				    say("All numeric ON lists are empty.");
			}
			else if (!show_list(which))
				say("The %s list is empty.", 
					hook_functions[which].name);
		}
	}

	/*
	 * No "Type" argument was specified.  That means the user wants to
	 * list all of the ONs currently scheduled.
	 */
	else
	{
		int	total = 0;

		say("ON listings:");

		/*
		 * Show the named events
		 */
		for (which = FIRST_NAMED_HOOK; which < NUMBER_OF_LISTS; which++)
			total += show_list(which);

		/*
		 * Show the numeric events
		 */
		for (which = 0; which < FIRST_NAMED_HOOK; which++)
			total += show_list(which);

		if (!total)
			say("All ON lists are empty.");
	}
}


/* * * * * * * * * * STACKING A HOOK * * * * * * * * */
typedef struct  onstacklist
{
	int     which;
	Hook    *list;
	struct onstacklist *next;
}       OnStack;

static	OnStack	*	on_stack = NULL;

void	do_stack_on (int type, char *args)
{
	int	which;
	Hook	*list;

	if (!on_stack && (type == STACK_POP || type == STACK_LIST))
	{
		say("ON stack is empty!");
		return;
	}
	if (!args || !*args)
	{
		say("Missing event type for STACK ON");
		return;
	}

	if ((which = find_hook(args, NULL, 0)) == INVALID_HOOKNUM)
		return;		/* Error message already outputted */

	list = hook_functions[which].list;


	if (type == STACK_PUSH)
	{
		OnStack	*new_os;
		new_os = (OnStack *) new_malloc(sizeof(OnStack));
		new_os->which = which;
		new_os->list = list;
		new_os->next = on_stack;
		on_stack = new_os;
		hook_functions[which].list = NULL;
		return;
	}

	else if (type == STACK_POP)
	{
		OnStack	*p, *tmp = (OnStack *) 0;

		for (p = on_stack; p; tmp = p, p = p->next)
		{
			if (p->which == which)
			{
				if (p == on_stack)
					on_stack = p->next;
				else
					tmp->next = p->next;
				break;
			}
		}
		if (!p)
		{
			say("No %s on the stack", args);
			return;
		}

		hook_functions[which].list = p->list;

		new_free((char **)&p);
		return;
	}

	else if (type == STACK_LIST)
	{
		int	slevel = 0;
		OnStack	*osptr;

		for (osptr = on_stack; osptr; osptr = osptr->next)
		{
			if (osptr->which == which)
			{
				Hook	*hptr;

				slevel++;
				say("Level %d stack", slevel);
				for (hptr = osptr->list; hptr; hptr = hptr->next)
					show_hook(hptr, args);
			}
		}

		if (!slevel)
			say("The STACK ON %s list is empty", args);
		return;
	}
	say("Unknown STACK ON type ??");
}



/* List manips especially for on's. */
static void 	add_to_list (Hook **list, Hook *item)
{
	Hook *tmp, *last = NULL;

	for (tmp = *list; tmp; last = tmp, tmp = tmp->next)
	{
		if (tmp->sernum < item->sernum)
			continue;
		else if ((tmp->sernum == item->sernum) && (my_stricmp(tmp->nick, item->nick) < 0))
			continue;
		else
			break;
	}

	if (last)
	{
		item->next = last->next;
		last->next = item;
	}
	else
	{
		item->next = *list;
		*list = item;
	}
}


static Hook *remove_from_list (Hook **list, char *item, int sernum)
{
	Hook *tmp, *last = NULL;

	for (tmp = *list; tmp; last = tmp, tmp = tmp->next)
	{
		if (tmp->sernum == sernum && !my_stricmp(tmp->nick, item))
		{
			if (last)
				last->next = tmp->next;
			else
				*list = tmp->next;
			return tmp;
		}
	}
	return NULL;
}


/* this function traverses all the hooks for all the events in the system
 * trying to find a free serial number (one that is unused by any hook on
 * any event) in the direction given (either -1 or +1 for - and +) starting
 * at the given point. */
int hook_find_free_serial(int dir, int from, int which) {
    int 	ser;
    Hook	*hp;
    int		wc;

    if (from == 0)
	from = dir;

    /* We iterate through the specified (or all) lists looking for a serial
     * number that isn't in use.  If we make it through all of our loops
     * without breaking out of them, we have found an unused number */
    for (ser = from; (dir > 0 ? ser <= 32767 : ser >= -32767); ser += dir) {
	if (which != INVALID_HOOKNUM) {
	    /* a list was specified */
	    hp = hook_functions[which].list;
	    
	    while (hp != NULL) {
		if (hp->sernum == ser)
		    break;
		hp = hp->next;
	    }
	    if (hp == NULL)
		break;
	} else {
	    /* no list was specified.  start digging */
	    hp = NULL;
	    for (wc = 0; wc < NUMBER_OF_LISTS; wc++) {
		for (hp = hook_functions[wc].list; hp != NULL; hp = hp->next)
		    if (hp->sernum == ser)
			break;
		if (hp != NULL)
		    break;
	    }
	    if (hp == NULL)
		break; /* found an unused one */
	}
    }

    return ser;
}

/* get_noise_id() returns identifer for noise chr */
static int	get_noise_id (char *chr)
{
	int n;
	n = atol(chr);
	if (n == 0 && chr[0] != '0') 
		for (n = 0; n < noise_level_num; n++)
			if (!my_stricmp(chr, noise_info[n]->name))
				break;
	return n;
}

enum
{
	HOOKCTL_GET_HOOK_NOARG,
	
	HOOKCTL_GET_HOOK,
	
	HOOKCTL_GET_LIST,
	HOOKCTL_GET_NOISE,
	HOOKCTL_GET_NOISY,
};
enum
{
	HOOKCTL_ADD = 1,
	HOOKCTL_ARGS,
	HOOKCTL_COUNT,
	HOOKCTL_CURRENT_IMPLIED_HOOK,
	HOOKCTL_DEFAULT_NOISE_LEVEL,
	HOOKCTL_DENY_ALL_HOOKS,
	HOOKCTL_EMPTY_SLOTS,
	HOOKCTL_EXECUTING_HOOKS,
	HOOKCTL_FIRST_NAMED_HOOK,
	HOOKCTL_GET,
	HOOKCTL_HALTCHAIN,
	HOOKCTL_HOOKLIST_SIZE,
	HOOKCTL_LAST_CREATED_HOOK,
	HOOKCTL_LIST,
	HOOKCTL_LOOKUP,
	HOOKCTL_MATCH,
	HOOKCTL_MATCHES,
	HOOKCTL_NOISE_LEVELS,
	HOOKCTL_NOISE_LEVEL_NUM,
	HOOKCTL_NUMBER_OF_LISTS,
	HOOKCTL_PACKAGE,
	HOOKCTL_POPULATED_LISTS,
	HOOKCTL_REMOVE,
	HOOKCTL_RETVAL,
	HOOKCTL_SERIAL,
	HOOKCTL_SET,
	HOOKCTL_USERINFO
};

enum
{
	HOOKCTL_GET_HOOK_ARGUMENT_LIST = 1,
	HOOKCTL_GET_HOOK_FLEXIBLE,
	HOOKCTL_GET_HOOK_NICK,
	HOOKCTL_GET_HOOK_NOT,
	HOOKCTL_GET_HOOK_NOISE,
	HOOKCTL_GET_HOOK_NOISY,
	HOOKCTL_GET_HOOK_PACKAGE,
	HOOKCTL_GET_HOOK_SERIAL,
	HOOKCTL_GET_HOOK_SKIP,
	HOOKCTL_GET_HOOK_STUFF,
	HOOKCTL_GET_HOOK_TYPE,
	HOOKCTL_GET_HOOK_STRING
};

/*
 * $hookctl() arguments:
 *   ADD <#!'[NOISETYPE]><list> [[#]<serial>] <nick> [(<argument list>)] <stuff>
 *       Argument list not yet implemented for $hookctl()
 *   ADD <#!'[NOISETYPE]><list> [[#]<serial>] <nick> <stuff>
 *       - Creates a new hook. Returns hook id.
 *   COUNT
 *       - See COUNT/LIST
 *   HALTCHAIN <recursive number>
 *       - Will set the haltflag for eventchain. May halt the current chain,
 *         or any chain currently being executed.
 *         Returns 1 on success, 0 otherwise.
 *   DEFAULT_NOISE_LEVEL
 *       - returns the 'default noise level'. It is not currently possible
 *         to change the current noise level, and probably never will be.
 *   DENY_ALL_HOOKS <arguments>
 *       - this sets the deny_all_hooks flag, or gets it's value. If set,
 *         to anything non negative, all hooks will be "ignored", and the
 *         default action of any event will be taken. Similar to a /DUMP ON
 *         but doens't actually remove any hooks.
 *   EMPTY_SLOTS
 *       - will return a list of empty slots in the hook-list.
 *   EXECUTING_HOOKS
 *       - will return a list of the current executing hooks. This is a
 *         'recursive' list, listing the current hook first.
 *   FIRST_NAMED_HOOK
 *       - returns FIRST_NAMED_HOOK
 *   HOOKLIST_SIZE
 *       - will returns HOOKLIST_SIZE
 *   LAST_CREATED_HOOK
 *       - returns the value of LAST_CREATED_HOOK
 *   LIST
 *   	 - See COUNT/LIST
 *   NOISE_LEVELS <pattern>
 *       - Returns a list of 'noise-types'. If <pattern> is specified only
 *         noise levels matching pattern will be returns.
 *   NOISE_LEVEL_NUM
 *       - Returns NOISE_LEVEL_NUM
 *   NUMBER_OF_LISTS
 *       - Returns NUBER_OF_LISTS
 *   PACKAGE <package> [<list>]
 *       - Returns a list of hooks of the given package. If <list> is
 *         specified, it will return only hooks in list <list>
 *   RETVAL <recursive number> [<new value>]
 *       - If recursve number isn't specified, 0 (the current) is specified.
 *         Will either return the value of retval for the given hook, or
 *         set it.
 *   SERIAL <serial> [<list>]
 *       - Works exactly like PACKAGE.
 *
 *   GET <type> <arg>
 *       - See GET/SET
 *   LOOKUP <list> <nick> [<serial>]
 *       - Returns hook matching given parametres.
 *   MATCH <list> <pattern>
 *       - Returns a list of matching hooks.
 *   REMOVE <hook id>
 *       - Removes the hook with the given hook ID. Returns 1 on success,
 *         0 otherwise.
 *   SET <type> <arg>
 *       - See GET/SET
 *
 *   * GET/SET usage
 *   GET gettype <arguments>
 *       - will return 'gettype'
 *   SET gettype <arguments>
 *       - will set 'gettype' or similar, and return 1 on success. Not all
 *         'gettypes' may be set, and not all gettypes will silently ignore
 *         being set.
 *
 *   It is very important to remember that GET won't ever SET anything(!!!)
 *   Won't, and shouldn't.
 *
 *   * GET/SET types:
 *   HOOK <argument>
 *       - More info on this under GET/SET HOOK
 *   LIST <arguments>
 *       - More info on this under GET/SET LIST
 *   NOISE <argument>
 *   NOISY <argument>
 *       - More info on this under GET/SET NOISE/NOISY
 *   MATCHES <argument>
 *       - More info on this under GET/SET MATCHES
 *
 *   * GET/SET HOOK usage:
 *       GET HOOK <hook id> <prop> <arg>
 *       SET HOOK <hook id> <prop> <arg>
 *
 *       <prop> may be one of the following:
 *		 ARGUMENT_LIST
 *		 	 - Returns or sets the argument list, if SET and <arg> is empty,
 * 			   it will be set to NULL, and therefore not used.
 *       FLEXIBLE
 *           - Returns or sets the value of flexible
 *       NICK
 *           - Sets or gets the hook's nick. The position of the hook will
 *             be changed if needed, and it is not possible to change this
 *             to a "crashing nick"
 *       NOT
 *           - Sets or gets the value of NOT.
 *       NOISE
 *       NOISY
 *           - Sets or returns the value of noisy.
 *       PACKAGE
 *           - Returns or sets the hook's packagename
 *       SERIAL
 *           - Returns or sets the serial for the hook. The hook's position
 *             in the list will be changed if necesarry, and it is not
 *             possible to set the serial to a crashing serial.
 *       SKIP
 *           - Returns or sets the value of skip.
 *       STUFF
 *           - Returns or sets the value of stuff.
 *       TYPE
 *           - Returns or sets the type.
 * 
 *   * GET/SET LIST usage:
 *       GET LIST <listname> <prop>
 *       SET LIST <listname> <prop> - not functional
 *
 *       <prop> may be one of the following:
 *       COUNT
 *           - Returns count of hooks
 *       FLAGS
 *           - Returns flags
 *       MARK
 *           - Returns mark
 *       NAME
 *           - Returns name
 *       PARAMETERS
 *       PARAMS
 *           - Returns value of params
 *
 *
 * 
 *   * GET/SET NOISE/NOISY usage:
 *       GET NOISE <noisename> <prop>
 *       SET NOISE <noisename> <prop> - not functional
 *
 *       <prop> may be one of the following:
 *           ALERT
 *               - returns value of alert.
 *           CUSTOM
 *               - returns value of custom.
 *           DISPLAY
 *               - returns value of display.
 *           IDENTIFIER
 *               - returns value of identifier.
 *           NAME
 *               - returns name.
 *           SUPPRESS
 *               - returns value of suppress.
 *           VALUE
 *               - returns value of value. d'oh!
 *
 *   * GET/SET MATCHES:
 *       - This function is not ready yet, and will currently RETURN_NULL.
 *
 *   * COUNT/LIST usage:
 *   	COUNT / LIST work doing the same, the only difference is that
 *   	COUNT will return the count of lists/hooks, while list will return
 *   	a list
 *
 * 	    The following options are permitted:
 * 	    
 *	 		LISTS <pattern>
 *          - Will either return all lists available, or only the 
 *            matching ones.
 *	 		POPULATED_LISTS <pattern>
 *	 	    - Works _just_ like LISTS, but will only return "populated"
 *	 	      lists
 *   		HOOKS <pattern>
 *   	 	- Will either return all the hooks on the system, or all
 *   	 	  the hooks in the matching lists
 
 */

char *hookctl (char *input)
{
	/* ALL the variables are due for a clean up */
	int go = 0;
	int hooknum = INVALID_HOOKNUM;
	int action;
	int prop;
	int userial;
	int tmp_int, tmp_int2;
	int set;
	int ser;
	int is_serial = 0;
	int set_noisy = default_noise;
	int set_serial = 0;
	int set_not = 0;
	int serial = 0;
	int set_flex = 0;
	int bestmatch = 0;
	int currmatch;
	int sernum;
	int halt = 0;
	int id;
	size_t retlen;
	char *nam;
	char *str;
	char *hookname;
	char *name;
	char *ret = NULL;
	char *tmp;
	char *nick;
	char *buffer;

	struct Current_hook *curhook;
	
	Hookables *hooks = NULL;
	Hook *hook = NULL;
	Hook *tmp_hook;

	ArgList *tmp_arglist = NULL;

	if (!hook_functions_initialized)
		initialize_hook_functions();

	if (!input || !*input)
		RETURN_EMPTY;
	
	GET_FUNC_ARG(str, input);
	go = vmy_strnicmp (strlen(str), str, 
		"ADD",
		"ARGS",
		"COUNT",
		"CURRENT_IMPLIED_HOOK",
		"DEFAULT_NOISE_LEVEL", 
		"DENY_ALL_HOOKS",
		"EMPTY_SLOTS",
		"EXECUTING_HOOKS", 
		"FIRST_NAMED_HOOK",
		"GET",
		"HALTCHAIN",
		"HOOKLIST_SIZE",
		"LAST_CREATED_HOOK",
		"LIST",
		"LOOKUP",
		"MATCH",
		"MATCHES",
		"NOISE_LEVELS",
		"NOISE_LEVEL_NUM",
		"NUMBER_OF_LISTS",
		"POPULATED_LISTS",
		"PACKAGE",
		"REMOVE",
		"RETVAL",
		"SERIAL",
		"SET",
		"USERINFO",
		NULL);

	switch (go)
	{
	
	/* go-switch */
	case 0:
		RETURN_EMPTY;
		break;

	/* go-switch */
	case HOOKCTL_LOOKUP:
		ser = 0;
		if (!input || !*input)
			RETURN_EMPTY;
		GET_FUNC_ARG(hookname, input);
		if ((hooknum = find_hook(hookname, NULL, 1)) == INVALID_HOOKNUM)
			RETURN_EMPTY;
		if (!input || !*input)
			RETURN_EMPTY;
		GET_FUNC_ARG(nick, input);
		if (input && *input)
			GET_INT_ARG(ser, input);
		for (
			hook = hook_functions[hooknum].list;
			hook != NULL;
			hook = hook->next)
		{
			if (hook->sernum != ser || my_stricmp(nick, hook->nick))
				continue;
			RETURN_INT(hook->userial);
		}
		RETURN_INT(-1);
		break;
	
	/* go-switch */
	case HOOKCTL_DEFAULT_NOISE_LEVEL:
		RETURN_STR(noise_info[default_noise]->name);
		break;
		
	/* go-switch */	
	case HOOKCTL_FIRST_NAMED_HOOK:
		RETURN_INT(FIRST_NAMED_HOOK);
		break;
		
	/* go-switch */
	case HOOKCTL_HOOKLIST_SIZE:
		RETURN_INT(hooklist_size);
		break;
	
	/* go-switch */
	case HOOKCTL_LAST_CREATED_HOOK:
		RETURN_INT(last_created_hook);
		break;
	
	/* go-switch */
	case HOOKCTL_NOISE_LEVEL_NUM:
		RETURN_INT(noise_level_num);
		break;
		
	/* go-switch */
	case HOOKCTL_NUMBER_OF_LISTS:
		RETURN_INT(NUMBER_OF_LISTS);
		break;
		
	case HOOKCTL_CURRENT_IMPLIED_HOOK:
		RETURN_STR(current_implied_on_hook);
		break;

	/* go-switch */
	case HOOKCTL_RETVAL:
		if (!current_hook)
			RETURN_EMPTY;
		if (!input || !*input)
			RETURN_INT(current_hook->retval);
		else
		{
			GET_FUNC_ARG(str, input);
			if (!isdigit(str[0]) && str[0] != '-')
				tmp_int = -2 + vmy_strnicmp(strlen(str), str, 
					"NO_ACTION_TAKEN",	"SUPPRESS_DEFAULT",
					"DONT_SUPPRESS_DEFAULT", "RESULT_PENDING",
					NULL
				);
			else
				GET_INT_ARG(tmp_int, str);
			
			if (tmp_int < -1 || tmp_int > 2)
				RETURN_INT(0);
			current_hook->retval = tmp_int;
			RETURN_INT(1);
		}
		break;
	
	/* go-switch */
	case HOOKCTL_DENY_ALL_HOOKS:
		if (!input || !*input)
			RETURN_INT(deny_all_hooks);
		else
		{
			GET_INT_ARG(tmp_int, input);
			deny_all_hooks = tmp_int ? 1 : 0;
			RETURN_INT(deny_all_hooks);
		}
		break;	

	/* go-switch */
	case HOOKCTL_EMPTY_SLOTS:
		for (tmp_int = 0; tmp_int < hooklist_size; tmp_int++)
			if (hooklist[tmp_int] == NULL)
				malloc_strcat_wordlist_c(&ret, space, ltoa(tmp_int), &retlen);

		RETURN_MSTR(ret);
		break;
	
	/* go-switch */
	case HOOKCTL_COUNT:
	case HOOKCTL_LIST:
		/* Shamelessly using prop here as well. Save the variables! */
		if (!input || !*input)
			prop = 1;
		else
		{
			GET_FUNC_ARG(str,input);
			prop = vmy_strnicmp(strlen(str), str, "LISTS", "POPULATED_LISTS", "HOOKS", NULL);
		}
		if (input && *input)
		{
			GET_FUNC_ARG(str, input);
		}
		else
		{
			str = NULL;
		}
		if (prop == 0)
			RETURN_EMPTY;
		tmp_int2 = 0;	
		/* Walk through the entire list, starting with the named hooks */
		for (tmp_int = 0; tmp_int < NUMBER_OF_LISTS; tmp_int++)
		{
			if (((prop != 1 && hook_functions[tmp_int].list == NULL)
				|| (str && !wild_match(str, hook_functions[tmp_int].name))
			)
			)
				continue;
			if (prop != 3)
			{
				if (go == HOOKCTL_COUNT)
					tmp_int2++;
				else
					malloc_strcat_wordlist_c(&ret, space, hook_functions[tmp_int].name,
						&retlen);
				continue;
			}
			for (tmp_hook = hook_functions[tmp_int].list;
				tmp_hook != NULL;
				tmp_hook = tmp_hook->next)
			{
				if (go == HOOKCTL_COUNT)
					tmp_int2++;
				else
					malloc_strcat_wordlist_c(&ret, space, ltoa(tmp_hook->userial),
						&retlen);
			}
		}
		if (go == HOOKCTL_COUNT)
			RETURN_INT(tmp_int2);
		else
			RETURN_MSTR(ret);
		break;

	/* go-switch */
	case HOOKCTL_SERIAL:
	case HOOKCTL_PACKAGE:
		if (go == HOOKCTL_SERIAL)
			is_serial = 1;
		if (!input || !*input)
			RETURN_EMPTY;
		if (is_serial)
		{
			GET_INT_ARG(serial, input);
		}
		else
		{
			GET_FUNC_ARG(str, input);
		}
		if (input && *input)
		{
			GET_FUNC_ARG(hookname, input);
			if ((hooknum = find_hook (hookname, NULL, 1)) == INVALID_HOOKNUM)
				RETURN_EMPTY;
		}
		if (hooknum == INVALID_HOOKNUM)
		{
			for (tmp_int = 0; tmp_int < hooklist_size; tmp_int++)
			{
				if (hooklist[tmp_int] == NULL)
					continue;
				if ((is_serial && hooklist[tmp_int]->sernum == serial) ||
					(!is_serial && hooklist[tmp_int]->filename && !my_stricmp(hooklist[tmp_int]->filename, str)))
				{
					malloc_strcat_wordlist_c(&ret, space, ltoa(tmp_int), &retlen);	
				}
			}
		}
		else
		{
			if (hook_functions[hooknum].list == NULL)
				RETURN_EMPTY;
			for (
				hook = hook_functions[hooknum].list; 
				hook != NULL; 
				hook = hook->next)
				if ((is_serial && hook->sernum == serial) || 
					(!is_serial && hook->filename && !my_stricmp(hook->filename, str))
				)
				{
					malloc_strcat_wordlist_c(&ret, space, ltoa(hook->userial), &retlen);
				}
		}
		RETURN_MSTR(ret);
		break;

	/* go-switch */
	case HOOKCTL_NOISE_LEVELS:
		if (!input || !*input)
			nam = NULL;
		else
		{
			GET_FUNC_ARG(nam, input);
		}
		for (tmp_int = 0; tmp_int < noise_level_num; tmp_int++)
		{
			if (!nam || wild_match(nam, noise_info[tmp_int]->name))
				malloc_strcat_wordlist_c(&ret, space, noise_info[tmp_int]->name, &retlen);
		}
		RETURN_MSTR(ret);
		break;
	
	/* go-switch */
	case HOOKCTL_EXECUTING_HOOKS:
		if (current_hook == NULL)
			RETURN_EMPTY;
		for (
			curhook = current_hook; 
			curhook != NULL; curhook = curhook->under)
			malloc_strcat_wordlist_c(&ret, space, ltoa(curhook->userial), &retlen);
		RETURN_MSTR(ret);
		break;

	/* go-switch */
	case HOOKCTL_MATCHES:
		/*
		 * serial, package, stuff, nick, list
		 */
		RETURN_EMPTY;

	/* go-switch */
	case HOOKCTL_GET:
	case HOOKCTL_SET:
		set = (go == HOOKCTL_SET);
	
		if (!input || !*input)
			RETURN_EMPTY;

		if (atoi(input) == 0 && input[0] != '0')
		{
			GET_FUNC_ARG(str, input);
			action = vmy_strnicmp(strlen(str), str,
				"HOOK",
				"LIST",
				"NOISE",
				"NOISY",
				NULL
			);
				
			if (action == 0)
				RETURN_EMPTY;
		}
		else	
			action = HOOKCTL_GET_HOOK_NOARG;

		switch (action)
		{
		/* action-switch */
		case HOOKCTL_GET_HOOK:
		case HOOKCTL_GET_HOOK_NOARG:
			
			GET_INT_ARG(userial, input);
			if (userial == -1 && current_hook)
				userial = current_hook->userial;
			if (userial < 0 
				|| hooklist_size <= userial 
				|| hooklist[userial] == NULL)
				RETURN_EMPTY;
			hook = hooklist[userial];
			if (!set && (!input || !*input))
				RETURN_STR(hook->stuff);
			GET_FUNC_ARG(str, input);

			prop = vmy_strnicmp(strlen(str), str,
				"ARGUMENT_LIST",
				"FLEXIBLE", 	"NICK", 	"NOT",		"NOISE", 
				"NOISY", 		"PACKAGE",	"SERIAL", 	"SKIP",
				"STUFF",		"TYPE", 	"STRING",	NULL);
			if (prop == 0)
				RETURN_EMPTY;

			if (!input || !*input)
				str = LOCAL_COPY(empty_string);
			else
				str = input;
		
			switch (prop)
			{
			/* prop-switch */
			case HOOKCTL_GET_HOOK_ARGUMENT_LIST:
				if (!set)
				{
					if (!hook->arglist)
						RETURN_EMPTY;
					str = print_arglist(hook->arglist);
					if (!str)
						RETURN_EMPTY;
					return str;
				}
				
				if (hook->arglist)
					destroy_arglist(&(hook->arglist));
				
				if (!str || !*str)
				{
					hook->arglist = NULL;
					RETURN_EMPTY;
				}
				
				hook->arglist = parse_arglist(str);
				str = print_arglist(hook->arglist);
				if (!str)
				{
					destroy_arglist(&(hook->arglist));
					RETURN_EMPTY;
				}
				return str;
				
				break;
				
			case HOOKCTL_GET_HOOK_NICK:
				if (!set)
					RETURN_STR(hook->nick);
				str = malloc_strdup(upper(str));
				for (
					tmp_hook = hook_functions[hook->type].list;
					tmp_hook != NULL;
					tmp_hook = tmp_hook->next
				)
				{
					if (!strcmp(tmp_hook->nick, str)
						&& tmp_hook->sernum == hook->sernum)
					{
						new_free (&str);
						RETURN_INT(0);
					}
				}
				remove_from_list(
					&hook_functions[hook->type].list,
					hook->nick,
					hook->sernum
				);
				new_free(&hook->nick);
				hook->nick = str;
				add_to_list(
					&hook_functions[hook->type].list,
					hook
				);
				RETURN_INT(1);
				break;
				
			/* prop-switch */
			case HOOKCTL_GET_HOOK_STUFF:
				if (!set)
					RETURN_STR(hook->stuff);
				new_free (&(hook->stuff));
				hook->stuff = malloc_strdup(str);
				RETURN_INT(1);
				break;
			
			/* prop-switch */
			case HOOKCTL_GET_HOOK_NOT:
				if (!set)
					RETURN_INT(hook->not);
				hook->not = atol(str) ? 1 : 0;
				RETURN_INT(1);
				break;
			
			case HOOKCTL_GET_HOOK_SKIP:
				if (!set)
					RETURN_INT(hook->skip);
				hook->skip = atol(str) ? 1 : 0;
				RETURN_INT(1);
				break;
				
			/* prop-switch */
			case HOOKCTL_GET_HOOK_NOISE:
			case HOOKCTL_GET_HOOK_NOISY:
				if (!set)
					RETURN_STR(noise_info[hook->noisy]->name);
				if ((tmp_int = get_noise_id(input)) >= noise_level_num
					|| tmp_int < 0
				)
					RETURN_INT(0);
				hook->noisy = tmp_int;
				RETURN_INT(1);
				break;
			
			/* prop-switch */
			case HOOKCTL_GET_HOOK_SERIAL:
				if (!set)
					RETURN_INT(hook->sernum);
				tmp_int = atol(str);
				for (
					tmp_hook = hook_functions[hook->type].list;
					tmp_hook != NULL;
					tmp_hook = tmp_hook->next
				)
				{
					if (!strcmp(tmp_hook->nick, hook->nick)
						&& tmp_hook->sernum == tmp_int
					)
						RETURN_INT(0);
				}
				remove_from_list(
					&hook_functions[hook->type].list,
					hook->nick,
					hook->sernum
				);
				
				hook->sernum = tmp_int;
				add_to_list(
					&hook_functions[hook->type].list,
					hook
				);
				RETURN_INT(1);
				break;	
	
			/* prop-switch */
			case HOOKCTL_GET_HOOK_FLEXIBLE:
				if (!set)
					RETURN_INT(hook->flexible);
				hook->flexible = atol(str) ? 1 : 0;
				RETURN_INT(1);
				break;

			/* prop-switch */
			case HOOKCTL_GET_HOOK_PACKAGE:
				if (!set)
					RETURN_STR(hook->filename);
				new_free (&(hook->filename));
				hook->filename = malloc_strdup(str);
				RETURN_INT(1);
				break;

			/* prop-switch */
			case HOOKCTL_GET_HOOK_TYPE:
				if (!set)
					RETURN_STR(hook_functions[hook->type].name);
				break;

			case HOOKCTL_GET_HOOK_STRING:
			{
				char *retval = NULL;
				size_t	clue = 0;
				char	blah[10];

				/* Just to start off */
				retval = new_malloc(128);
				*retval = 0;

				/* ON <SERIAL-INDICATOR><NOISE><TYPE> <SERIAL-NUMBER>
						<QUOTE><PATTERN><QUOTE> {<STUFF>} */
				malloc_strcat_c(&retval, "ON ", &clue);

				if (hook->sernum)
					malloc_strcat_c(&retval, "#", &clue);
				if (noise_info[hook->noisy]->identifier)
				{
					blah[0] = noise_info[hook->noisy]->identifier;
					blah[1] = 0;
					malloc_strcat_c(&retval, blah, &clue);
				}
				malloc_strcat_c(&retval, hook_functions[hook->type].name, &clue);

				if (hook->sernum)
				{
					snprintf(blah, sizeof blah, " %d", hook->sernum);
					malloc_strcat_c(&retval, blah, &clue);
				}

				malloc_strcat_c(&retval, space, &clue);
				if (hook->not)
				{
					blah[0] = '^';
					blah[1] = 0;
					malloc_strcat_c(&retval, blah, &clue);
				}

				if (hook->flexible)
				{
					blah[0] = '\'';
					blah[1] = 0;
				}
				else
				{
					blah[0] = '"';
					blah[1] = 0;
				}
				malloc_strcat_c(&retval, blah, &clue);
				malloc_strcat_c(&retval, hook->nick, &clue);
				malloc_strcat_c(&retval, blah, &clue);
				malloc_strcat_c(&retval, space, &clue);

				if (hook->arglist)
				{
					char *arglist = print_arglist(hook->arglist);

					blah[0] = '(';
					blah[1] = 0;
					malloc_strcat_c(&retval, blah, &clue);
					malloc_strcat_c(&retval, arglist, &clue);
					blah[0] = ')';
					malloc_strcat_c(&retval, blah, &clue);
				    malloc_strcat_c(&retval, space, &clue);
					new_free(&arglist);
				}

				if (!hook->not)
				{
					blah[0] = '{';
					blah[1] = 0;
					malloc_strcat_c(&retval, blah, &clue);
					malloc_strcat_c(&retval, hook->stuff, &clue);
					blah[0] = '}';
					malloc_strcat_c(&retval, blah, &clue);
				}
				RETURN_MSTR(retval);
			}
		/* end prop-switch */
		}
		RETURN_EMPTY;
		break;
	
		/* action switch */
		case HOOKCTL_GET_NOISE:
		case HOOKCTL_GET_NOISY:
			if (!input || !*input)
				RETURN_EMPTY;
			if (isdigit(input[0]))
			{
				GET_INT_ARG(id, input);
				if (noise_level_num <= id)
					RETURN_EMPTY;
			}
			else
			{
				GET_FUNC_ARG(name, input);
				for (tmp_int = 0; tmp_int < noise_level_num; tmp_int++)
				{
					if (!my_stricmp(name, noise_info[tmp_int]->name))
						break;
				}
				if (noise_level_num == tmp_int)
					RETURN_EMPTY;
				id = tmp_int;
			}
		
			if (!input || !*input)
				RETURN_EMPTY;
			
			GET_FUNC_ARG(str, input);
			switch (vmy_strnicmp(strlen(str), str,
				"ALERT", 	"CUSTOM", 	"DISPLAY", 	"IDENTIFIER",
				"NAME", 	"SUPPRESS", "VALUE", 	NULL)
			)
			{
				case 1: 	RETURN_INT(noise_info[id]->alert ? 1 : 0);
				case 2: 	RETURN_INT(noise_info[id]->custom ? 1 : 0);
				case 3: 	RETURN_INT(noise_info[id]->display ? 1 : 0);
				case 4: 	RETURN_INT(noise_info[id]->identifier);
				case 5: 	RETURN_STR(noise_info[id]->name);
				case 6:		RETURN_INT(noise_info[id]->suppress ? 1 : 0);
				case 7:		RETURN_INT(noise_info[id]->value);
				default: 	RETURN_EMPTY;
			}
			break;

		/* action-switch */
		case HOOKCTL_GET_LIST:
		
			GET_FUNC_ARG(hookname, input);
			if (atoi(hookname) == -1)
			{
				if (current_hook == NULL)
					RETURN_EMPTY;
				hooknum = hooklist[current_hook->userial]->type;
			}
			else
			{
				if ((hooknum = find_hook (hookname, NULL, 1)) == INVALID_HOOKNUM)
				{
					RETURN_EMPTY;
				}
			}
			hooks = &hook_functions[hooknum];
			if (!input || !*input)
				prop = 2;
			else
			{
				GET_FUNC_ARG(str, input);
				prop = vmy_strnicmp(strlen(str), str,
					"FLAGS",		"MARK", 		"NAME",
					"PARAMETRES", 	"PARAMETERS",	"PARAMS",
					"IMPLIED",		NULL);
			}

			switch (prop)
			{
				case 1:		RETURN_INT(hooks->flags);
				case 2:		RETURN_INT(hooks->mark);
				case 3:		RETURN_STR(hooks->name);
				case 4:
				case 5:
				case 6:		RETURN_INT(hooks->params);
#ifdef IMPLIED_ON_HOOKS
				case 7:		if (set) {
								if (*input == '{')
								{
									ssize_t span;
									span = MatchingBracket(input+1, '{', '}');
									if (span >= 0) {
										input[span + 1] = 0;
										input++;
									}
									hooks->implied_protect = 1;
								}
								if (empty(input))
									new_free(&hooks->implied);
								else
									malloc_strcpy(&hooks->implied, input);
								RETURN_INT(1);
							} else
								RETURN_STR(hooks->implied);
#endif
			}
			RETURN_EMPTY;
			break;
		
		/* end action-switch */
		}
		RETURN_EMPTY;
		break;

	/* go-switch */
	case HOOKCTL_ADD:
		if (!input || !*input)
			RETURN_INT(-1);
		GET_FUNC_ARG(hookname, input);
		while (*hookname != '\0')
		{
			switch (*hookname)
			{
			case '#':
				set_serial = 1;
				hookname++;
				continue;
			
			case '!':
				set_not = 1;
				hookname++;
				continue;
			
			case '\'':
				set_flex = 1;
				hookname++;
				continue;
		
			default:
				for (tmp_int = 0; tmp_int < noise_level_num; tmp_int++)
				{
					if (noise_info[tmp_int]->identifier != 0 &&
						noise_info[tmp_int]->identifier == *hookname)
						break;
				}
				if (tmp_int == noise_level_num)
					break;
				set_noisy = noise_info[tmp_int]->value;
				hookname++;
				continue;
			}
			break;
		}
		if ((hooknum = find_hook (hookname, NULL, 1)) == INVALID_HOOKNUM)
			RETURN_EMPTY;
		if (set_serial)
		{
			if (!input || !*input)
				RETURN_INT(-1);
			if (input[0] == '#')
				input++;
			GET_FUNC_ARG(tmp, input);
			if (!strcmp(tmp, "-"))
			{
				serial = hook_find_free_serial(-1, 0, hooknum);
			}
			else if (!strcmp(tmp, "+"))
			{
				serial = hook_find_free_serial(1, 0, hooknum);
			}
			else
			{
				serial = atoi (tmp);
			}
		}
		if (!input || !*input)
			RETURN_INT(-1);
		GET_FUNC_ARG(nick, input);
		nick = malloc_strdup(nick);
		tmp_int = add_hook (hooknum, nick, tmp_arglist, input, set_noisy, set_not, serial, set_flex);
		new_free (&nick);
		RETURN_INT(tmp_int);
		break;

	/* go-switch */
	case HOOKCTL_REMOVE:
		if (!input || !*input)
			RETURN_INT(-1);
		GET_INT_ARG(id, input);
		if (id == -1 && current_hook != NULL)
			id = current_hook -> userial;
		if (hooklist_size <= id || id < 0 || hooklist[id] == NULL)
			RETURN_INT(-1);
		hook = hooklist[id];
		remove_hook(hook->type, hook->nick, hook->sernum, 1);
		RETURN_INT(1);
		break;

	/* go-switch */
	case HOOKCTL_HALTCHAIN:
		if (input && *input)
		{
			GET_INT_ARG(halt, input);
		}
		curhook = current_hook;
		for (tmp_int = 0; curhook && tmp_int < halt; tmp_int++)
			curhook = curhook->under;
		if (!curhook)
			RETURN_INT(0);
		curhook->halt = 1;
		RETURN_INT(1);
		break;

	/* go-switch */
	case HOOKCTL_ARGS:
		if (input && *input)
		{
			GET_INT_ARG(halt, input);
		}
		curhook = current_hook;
		for (tmp_int = 0; curhook && tmp_int < halt; tmp_int++)
			curhook = curhook->under;
		if (!curhook)
			RETURN_INT(0);
		malloc_strcpy(&curhook->buffer, input);
		curhook->buffer_changed = 1;
		RETURN_INT(1);
		break;

	/* go-switch */
	case HOOKCTL_USERINFO:
		if (input && *input)
		{
			GET_INT_ARG(halt, input);
		};
		curhook = current_hook;
		for (tmp_int = 0; curhook && tmp_int < halt; tmp_int++)
			curhook = curhook->under;
		if (!curhook)
			RETURN_INT(0);
		if (!input || !*input)
		{
			if (curhook->user_supplied_info == NULL)
				RETURN_EMPTY;
			else
				RETURN_STR(curhook->user_supplied_info);
		}
		malloc_strcpy(&curhook->user_supplied_info, input);
		RETURN_INT(1);
		break;
	/* go-switch */
	case HOOKCTL_MATCH:
		if (!input || !*input)
			RETURN_EMPTY;
		GET_FUNC_ARG(hookname, input);
		if ((hooknum = find_hook (hookname, NULL, 1)) == INVALID_HOOKNUM)
			RETURN_EMPTY;
		if (!hook_functions[hooknum].list
			|| (hook_functions[hooknum].mark
				&& hook_functions[hooknum].flags & HF_NORECURSE))
			RETURN_EMPTY;
		buffer = malloc_strdup(!input || !*input ? "" : input);
		for (sernum = INT_MIN;sernum < INT_MAX;sernum++)
		{
			for (hook = hook_functions[hooknum].list; hook; hook = hook->next)
			{
				Hook *besthook = NULL;
				if (hook->sernum < sernum)
					continue;
				
				if (hook->sernum > sernum)
					sernum = hook->sernum;
				
				for (; hook && hook->sernum == sernum; hook = hook->next)
				{
					if (hook->flexible)
					{
						char *tmpnick;
						tmpnick = expand_alias(hook->nick, empty_string);
						currmatch = wild_match(tmpnick, buffer);
						new_free(&tmpnick);
					}
					else
						currmatch = wild_match(hook->nick, buffer);
		
					if (currmatch > bestmatch)
					{
						besthook = hook;
						bestmatch = currmatch;
					}
				}
				
				if (!besthook || besthook->not)
					break;
				
				malloc_strcat_wordlist_c(&ret, space, ltoa(besthook->userial), &retlen);
				break;
			}
			if (!hook)
				break;
		}
		new_free (&buffer);
		RETURN_MSTR(ret);
		break;

	/* go-switch */
	default:
		yell ("In function hookctl: unknown go: %d", go);
		RETURN_EMPTY;
	/* The function should never come to this point */
	
	/* end go-switch */
	}
	RETURN_EMPTY;
}

#if 0
void    help_topics_on (FILE *f)
{
        int     x;

        for (x = 0; x < NUMBER_OF_LISTS; x++)
		if (!isdigit(*hook_functions[x].name))
			fprintf(f, "on %s\n", hook_functions[x].name);
}
#endif

