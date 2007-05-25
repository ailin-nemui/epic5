/* $EPIC: who.c,v 1.59 2007/05/25 16:47:48 jnelson Exp $ */
/*
 * who.c -- The WHO queue.  The ISON queue.  The USERHOST queue.
 *
 * Copyright © 1996, 2003 EPIC Software Labs.
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

#define NEED_SERVER_LIST
#include "irc.h"
#include "commands.h"
#include "ircaux.h"
#include "who.h"
#include "ssl.h"
#include "server.h"
#include "window.h"
#include "vars.h"
#include "hook.h"
#include "output.h"
#include "numbers.h"
#include "parse.h"
#include "if.h"
#include "names.h"
#include "words.h"
#include "reg.h"
#include "log.h"
#include "timer.h"
#include "alias.h"

/* XXXX - only debugging stuff for adm.  Remove later */
static	FILE *	who_log = NULL;
static	int	who_global_refnum = 0;
static	char	who_timeref[] = "WHOTIM";


static int	who_queue_debug (void *unused);

static void	WHO_DEBUG (const char *format, ...)
{
	va_list	args;

	if (who_log == NULL && (x_debug & DEBUG_WHO_QUEUE))
	{
		add_timer(1, who_timeref, 5, -1, who_queue_debug, 
				NULL, NULL, GENERAL_TIMER, -1, 0);
		do_log(1, "who.log", &who_log);
	}

	if (who_log)
	{
		time_t	t;
		char	my_buffer[256];
		struct tm *ugh;

		time(&t);
		ugh = localtime(&t);
		strftime(my_buffer, 255, "%H:%M:%S ", ugh);
		fprintf(who_log, "%s", my_buffer);

		va_start(args, format);
		vfprintf(who_log, format, args);
		fputc('\n', who_log);
		fflush(who_log);
		va_end(args);
	}

	if (who_log && !(x_debug & DEBUG_WHO_QUEUE))
	{
		remove_timer(who_timeref);
		do_log(0, "who.log", &who_log);
	}
}


/*
 *
 *
 *
 * 				WHO QUEUE
 *
 *
 *
 */

/* flags used by who queue */
#define WHO_OPS		0x0001
#define WHO_NAME	0x0002
#define WHO_ZERO	0x0004
#define WHO_CHOPS	0x0008
#define WHO_FILE	0x0010
#define WHO_HOST	0x0020
#define WHO_SERVER	0x0040
#define	WHO_HERE	0x0080
#define	WHO_AWAY	0x0100
#define	WHO_NICK	0x0200
#define	WHO_LUSERS	0x0400
#define	WHO_REAL	0x0800
#define WHO_NOCHOPS	0x1000

/*
 * This one requires an explanation.  As of u2.10.04, operators have to
 * specify a specific special flag to the WHO protocol command in order to
 * see invisible users.  This is so they can be all paranoid and pedantic 
 * about just who is looking at invisible users and when.  Sheesh.  This
 * flag has *no effect* in any other context than this.
 */
#define WHO_INVISIBLE	0x2000
#define WHO_OPERSPY	0x4000

#define S(x) (((x) != NULL) ? (x) : empty_string)

static char *who_item_full_desc (WhoEntry *item)
{
	static char retval[10240];

	if (item)
	    snprintf(retval, sizeof retval, 
		"refnum [%d] "
		"dirty [%d], piggyback [%d], unet [%d], unet_args [%s], "
		"dalnet [%d], dalnet_args [%s], who_mask [%d], "
		"who_target [%s], who_name [%s], who_host [%s], "
		"who_server [%s], who_nick [%s], who_real [%s], "
		"who_stuff [%s], who_end [%s], next [%p], line [%p], "
		"end [%p], requested [%ld], dirty [%ld]",
			item->refnum,
			item->dirty, item->piggyback, item->undernet_extended, 
				S(item->undernet_extended_args),
			item->dalnet_extended, S(item->dalnet_extended_args), 
				item->who_mask,
			S(item->who_target), S(item->who_name), 
				S(item->who_host),
			S(item->who_server), S(item->who_nick), 
				S(item->who_real),
			S(item->who_stuff), S(item->who_end), 
				item->next, item->line,
			item->end, item->request_time.tv_sec,
				item->dirty_time.tv_sec);
	else
	    snprintf(retval, sizeof retval, "<none>");

	return retval;
}

static char *who_item_desc (WhoEntry *item)
{
	static char retval[10240];

	if (item)
	    snprintf(retval, sizeof retval, 
		"refnum [%d] dirty [%d], piggyback [%d], next [%p]",
			item->refnum,
			item->dirty, item->piggyback, item->next);
	else
	    snprintf(retval, sizeof retval, "<none>");

	return retval;
}

static WhoEntry *who_queue_top (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return NULL;

	WHO_DEBUG("Returning top of who queue for server %d [%s]", 
			refnum, who_item_desc(s->who_queue));
	return s->who_queue;
}

/*
 * This is tricky -- this doesnt get the LAST one, it gets the
 * next to the last one.  Why?  Because the LAST one is the one
 * asking, and they want to know who is LAST (before them)
 * So it sucks.  Sue me.
 */
static WhoEntry *who_previous_query (int refnum, WhoEntry *me)
{
	WhoEntry *what;
	Server *s;

	if (!(s = get_server(refnum)))
		return NULL;

	what = who_queue_top(refnum);
	while (what && what->next != me)
		what = what->next;

	WHO_DEBUG("Returning item previous to [%d(%d)] - [%s]", 
			me->refnum, refnum, who_item_desc(what));
	return what;
}

static void who_queue_add (int refnum, WhoEntry *item)
{
	WhoEntry *bottom;
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	bottom = who_queue_top(refnum);
	while (bottom && bottom->next)
		bottom = bottom->next;

	if (!bottom)
		s->who_queue = item;
	else
		bottom->next = item;

	get_time(&item->request_time);

	WHO_DEBUG("Adding item to who queue for server %d [%s]", 
			refnum, who_item_full_desc(item));
	return;
}

static void delete_who_item (WhoEntry *save)
{
	Timeval t;
	double	d;

	get_time(&t);
	d = time_diff(save->request_time, t);
	WHO_DEBUG("Final deletion of item [%s], alive for [%f] seconds",
			who_item_full_desc(save), d);

	new_free(&save->who_target);
	new_free(&save->who_name);
	new_free(&save->who_host);
	new_free(&save->who_server);
	new_free(&save->who_nick);
	new_free(&save->who_real);
	new_free(&save->who_stuff);
	new_free(&save->who_end);
	new_free((char **)&save);
}

static void who_queue_pop (int refnum)
{
	WhoEntry *save;
	int	piggyback;
	Server *s;

	WHO_DEBUG("Popping first item off of server [%d]", refnum);

	if (!(s = get_server(refnum)))
		return;

	do
	{
		if (!(save = s->who_queue))
			break;

		piggyback = save->piggyback;
		s->who_queue = save->next;
		delete_who_item(save);
	}
	while (piggyback);

	if (s->who_queue == NULL)
		WHO_DEBUG("WHO QUEUE for server [%d] is now empty", refnum);
	return;
}


static WhoEntry *get_new_who_entry (void)
{
	WhoEntry *new_w = (WhoEntry *)new_malloc(sizeof(WhoEntry));
	new_w->refnum = ++who_global_refnum;
	new_w->dirty = 0;
	new_w->piggyback = 0;
	new_w->who_mask = 0;
	new_w->who_target = NULL;
	new_w->who_host = NULL;
	new_w->who_name = NULL;
	new_w->who_server = NULL;
	new_w->who_nick = NULL;
	new_w->who_real = NULL;
	new_w->who_stuff = NULL;
	new_w->who_end = NULL;
	new_w->next = NULL;
	new_w->undernet_extended = 0;
	new_w->undernet_extended_args = NULL;
	new_w->dalnet_extended = 0;
	new_w->dalnet_extended_args = NULL;
	new_w->request_time.tv_sec = 0;
	new_w->request_time.tv_usec = 0;
	new_w->dirty_time.tv_sec = 0;
	new_w->dirty_time.tv_usec = 0;

	WHO_DEBUG("Creating new who item with refnum [%d]", new_w->refnum);
	return new_w;
}

static void who_queue_list (int refnum)
{
	WhoEntry *item;
	int count = 0;
	Server *s;

	WHO_DEBUG("Listing queue for server [%d]", refnum);

	if (!(s = get_server(refnum)))
		return;

	for (item = s->who_queue; item; item = item->next)
	{
		yell("[%d] %s", count, who_item_full_desc(item));
		WHO_DEBUG("[%d] %s", count, who_item_full_desc(item));
		count++;
	}
}

static void who_queue_flush (int refnum)
{
	Server *s;

	WHO_DEBUG("Flushing who queue for server [%d]",  refnum);

	if (!(s = get_server(refnum)))
		return;

	while (s->who_queue)
		who_queue_pop(refnum);

	yell("Who queue for server [%d] purged", refnum);
	WHO_DEBUG("done");
}


/*
 * who: the /WHO command. Parses the who switches and sets the who_mask and
 * whoo_stuff accordingly.  Who_mask and who_stuff are used in whoreply() in
 * parse.c 
 */
BUILT_IN_COMMAND(whocmd)
{
	whobase(from_server, args, NULL, NULL);
}


/*
 * whobase: What does all the work.
 */
void 	whobase (int refnum, char *args, void (*line) (int, const char *, const char *, const char **), void (*end) (int, const char *, const char *, const char **))
{
	char *		arg;
	const char *	channel = NULL;
	int		no_args = 1,
			len;
	WhoEntry *	new_w, *old;

	/* Maybe should output a warning? */
	if (!is_server_registered(refnum))
	{
		WHO_DEBUG("WHOBASE: server [%d] is not connected", refnum);
		return;
	}

	new_w = get_new_who_entry();
	new_w->line = line;
	new_w->end = end;

	while ((arg = next_arg(args, &args)) != NULL)
	{
	    lower(arg);
	    no_args = 0;

	    if (*arg == '-' || *arg == '/')
	    {
		arg++;
		if ((len = strlen(arg)) == 0)
		{
			say("Unknown or missing flag");
			WHO_DEBUG("WHOBASE: empty argument. punting.");
			delete_who_item(new_w);
			return;
		}

		else if (!strncmp(arg, "away", MAX(len, 1)))
		{
			new_w->who_mask |= WHO_AWAY;
			WHO_DEBUG("WHOBASE: Setting WHO_AWAY flag");
		}
		else if (!strncmp(arg, "chops", MAX(len, 2)))
		{
			new_w->who_mask |= WHO_CHOPS;
			WHO_DEBUG("WHOBASE: Setting WHO_CHOPS flag");
		}
		else if (!strncmp(arg, "diagnose", MAX(len, 1)))
		{
			WHO_DEBUG("WHOBASE: Listing the who queue");
			who_queue_list(refnum);
			delete_who_item(new_w);
			return;
		}
		else if (!strncmp(arg, "dx", MAX(len, 2)))
		{
			new_w->dalnet_extended = 1;
			new_w->dalnet_extended_args = new_next_arg(args, &args);
			channel = args;		/* Grab the rest of args */
			WHO_DEBUG("WHOBASE: setting -dx flag [%s]", new_w->dalnet_extended_args, channel);
			args = NULL;
		}
		else if (!strncmp(arg, "end", MAX(len, 3)))
		{
			char *stuff;

			if ((stuff = next_expr(&args, '{')))
				malloc_strcpy(&new_w->who_end, stuff);
			else
				say("Need {...} argument for -END argument.");
			WHO_DEBUG("WHOBASE: setting who_end [%s]", new_w->who_end);
		}
		else if (!strncmp(arg, "flush", MAX(len, 1)))
		{
			WHO_DEBUG("WHOBASE: flushing who queue [%d]", refnum);
			who_queue_flush(refnum);
			delete_who_item(new_w);
			return;
		}
	 	else if (!strncmp(arg, "here", MAX(len, 2)))
		{
			WHO_DEBUG("WHOBASE: setting WHO_HERE flag");
			new_w->who_mask |= WHO_HERE;
		}
		else if (!strncmp(arg, "hosts", MAX(len, 2)))
		{
			if ((arg = next_arg(args, &args)) == NULL)
			{
				WHO_DEBUG("WHOBASE: -HOST missing argument");
				say("WHO -HOST: missing argument");
				delete_who_item(new_w);
				return;
			}

			new_w->who_mask |= WHO_HOST;
			malloc_strcpy(&new_w->who_host, arg);
			channel = new_w->who_host;
			WHO_DEBUG("WHOBASE: Setting -HOST argument [%s]", 
					new_w->who_host);
		}

		else if (!strncmp(arg, "line", MAX(len, 4)))
		{
			char *stuff;

			if ((stuff = next_expr(&args, '{')))
				malloc_strcpy(&new_w->who_stuff, stuff);
			else
				say("Need {...} argument for -LINE argument.");
			WHO_DEBUG("WHOBASE: setting -line [%s]", new_w->who_end);
		}
		else if (!strncmp(arg, "literal", MAX(len, 3)))
		{
			/* Hope for the best */
			new_w->who_mask = 0;	/* For safety reasons */
			new_w->dalnet_extended = 0;
			new_w->undernet_extended = 0;
			new_free(&new_w->who_stuff);
			new_free(&new_w->who_end);

			WHO_DEBUG("WHOBASE: Doing -LITERAL [%s]", args);
			who_queue_add(refnum, new_w);

			send_to_aserver(refnum, "WHO %s", args);
			return;
		}
		else if (!strncmp(arg, "lusers", MAX(len, 2)))
		{
			new_w->who_mask |= WHO_LUSERS;
			WHO_DEBUG("WHOBASE: Setting WHO_LUSERS flag", args);
		}
		else if (!strncmp(arg, "name", MAX(len, 2)))
		{
			if ((arg = next_arg(args, &args)) == NULL)
			{
				WHO_DEBUG("WHOBASE: -NAME missing argument");
				say("WHO -NAME: missing arguement");
				delete_who_item(new_w);
				return;
			}

			new_w->who_mask |= WHO_NAME;
			malloc_strcpy(&new_w->who_name, arg);
			channel = new_w->who_name;
			WHO_DEBUG("WHOBASE: Setting -NAME argument [%s]", 
					new_w->who_name);
		}
		else if (!strncmp(arg, "nick", MAX(len, 2)))
		{
			if ((arg = next_arg(args, &args)) == NULL)
			{
				WHO_DEBUG("WHOBASE: -NICK missing argument");
				say("WHO -NICK: missing arguement");
				delete_who_item(new_w);
				return;
			}

			new_w->who_mask |= WHO_NICK;
			malloc_strcpy(&new_w->who_nick, arg);
			channel = new_w->who_nick;
			WHO_DEBUG("WHOBASE: Setting -NICK argument [%s]", 
					new_w->who_nick);
		}
		else if (!strncmp(arg, "nochops", MAX(len, 2)))
		{
			new_w->who_mask |= WHO_NOCHOPS;
			WHO_DEBUG("WHOBASE: Setting WHO_NOCHOPS flag");
		}
		else if (!strncmp(arg, "oper", MAX(len, 1)))
		{
			new_w->who_mask |= WHO_OPS;
			WHO_DEBUG("WHOBASE: Setting WHO_OPS flag");
		}
		else if (!strncmp(arg, "operspy", MAX(len, 5)))
		{
			new_w->who_mask |= WHO_OPERSPY;
			WHO_DEBUG("WHOBASE: Setting WHO_OPERSPY flag");
		}
		else if (!strncmp(arg, "realname", MAX(len, 1)))
		{
			if ((arg = next_arg(args, &args)) == NULL)
			{
				WHO_DEBUG("WHOBASE: -REALNAME missing argument");
				say("WHO -REALNAME: missing arguement");
				delete_who_item(new_w);
				return;
			}

			new_w->who_mask |= WHO_REAL;
			malloc_strcpy(&new_w->who_real, arg);
			channel = new_w->who_real;
			WHO_DEBUG("WHOBASE: Setting -REALNAME [%s]", new_w->who_real);
		}
		else if (!strncmp(arg, "servers", MAX(len, 1)))
		{
			if ((arg = next_arg(args, &args)) == NULL)
			{
				WHO_DEBUG("WHOBASE: -SERVERS missing argument");
				say("WHO -SERVER: missing arguement");
				delete_who_item(new_w);
				return;
			}

			new_w->who_mask |= WHO_SERVER;
			malloc_strcpy(&new_w->who_server, arg);
			channel = new_w->who_server;
			WHO_DEBUG("WHOBASE: Setting -SERVERS [%s]", new_w->who_server);
		}
		else if (!strncmp(arg, "u-i", MAX(len, 3)))
		{
			new_w->who_mask |= WHO_INVISIBLE;
			WHO_DEBUG("WHOBASE: Setting WHO_INVISIBLE flag");
		}
		else if (!strncmp(arg, "ux", MAX(len, 2)))
		{
			new_w->undernet_extended = 1;
			new_w->undernet_extended_args = args;
			args = NULL;
			WHO_DEBUG("WHOBASE: Setting undernet flag [%s]",
					new_w->undernet_extended_args);
		}
		else
		{
			WHO_DEBUG("WHOBASE: Unknown flag [%s]", arg);
			say("Unknown or missing flag");
			delete_who_item(new_w);
			return;
		}
	    }
	    else if (strcmp(arg, "*") == 0)
	    {
		channel = get_echannel_by_refnum(0);
		if (!channel || !*channel)
		{
			WHO_DEBUG("WHOBASE: WHO *, but not on channel");
			say("You are not on a channel.  "
			    "Use /WHO ** to see everybody.");
			delete_who_item(new_w);
			return;
		}
		else
			WHO_DEBUG("WHOBASE: WHO * -> WHO %s", channel);
	    }
	    else
	    {
		channel = arg;
		WHO_DEBUG("WHOBASE: WHO %s", channel);
	    }
	}

	if (no_args)
	{
		WHO_DEBUG("WHOBASE: No arguments");
		say("No argument specified");
		delete_who_item(new_w);
		return;
	}

	if (!channel && (new_w->who_mask & WHO_OPS))
	{
		channel = "*.*";
		WHO_DEBUG("WHOBASE: Fallback to WHO %s", channel);
	}
	new_w->who_target = malloc_strdup(channel);
	WHO_DEBUG("WHOBASE: Target is [%s]", new_w->who_target);

	who_queue_add(refnum, new_w);

	/*
	 * Check to see if we can piggyback
	 */
	old = who_previous_query(refnum, new_w);
	if (old && !old->dirty && old->who_target && channel && 
		!strcmp(old->who_target, channel))
	{
		old->piggyback = 1;
		WHO_DEBUG("WHOBASE: Piggybacking onto refnum [%d]", 
					old->refnum);
		if (x_debug & DEBUG_OUTBOUND)
			yell("Piggybacking this WHO onto last one.");
	}
	else if (new_w->undernet_extended)
	{
		WHO_DEBUG("UNET QUERY: [%d] WHO %s %s%s%s", 
			refnum, new_w->who_target,
			(new_w->who_mask & WHO_OPS) ?  "o" : "",
			(new_w->who_mask & WHO_INVISIBLE) ? "x" : "",
			new_w->undernet_extended_args ? 
				new_w->undernet_extended_args : "");

		send_to_aserver(refnum, "WHO %s %s%s%s", 
			new_w->who_target,
			(new_w->who_mask & WHO_OPS) ?  "o" : "",
			(new_w->who_mask & WHO_INVISIBLE) ? "x" : "",
			new_w->undernet_extended_args ? 
				new_w->undernet_extended_args : "");
	}
	else if (new_w->dalnet_extended)
	{
		WHO_DEBUG("DALNET QUERY: [%d] WHO %s %s", 
			refnum, new_w->dalnet_extended_args,
			new_w->who_target);

		send_to_aserver(refnum, "WHO %s %s", 
			new_w->dalnet_extended_args,
			new_w->who_target);
	}
	else if (new_w->who_mask & WHO_OPERSPY)
	{
		WHO_DEBUG("OPERSPY QUERY: [%d] OPERSPY WHO %s %s%s", 
			refnum, new_w->who_target,
			(new_w->who_mask & WHO_OPS) ?  "o" : "",
			(new_w->who_mask & WHO_INVISIBLE) ? "x" : "");

		send_to_aserver(refnum, "OPERSPY WHO %s %s%s", 
			new_w->who_target,
			(new_w->who_mask & WHO_OPS) ?  "o" : "",
			(new_w->who_mask & WHO_INVISIBLE) ? "x" : "");
	}
	else
	{
		WHO_DEBUG("STD WHO: [%d] WHO %s %s%s", 
			refnum, new_w->who_target,
			(new_w->who_mask & WHO_OPS) ?  "o" : "",
			(new_w->who_mask & WHO_INVISIBLE) ? "x" : "");

		send_to_aserver(refnum, "WHO %s %s%s", 
			new_w->who_target,
			(new_w->who_mask & WHO_OPS) ?  "o" : "",
			(new_w->who_mask & WHO_INVISIBLE) ? "x" : "");
	}
}

static int who_whine = 0;

void	whoreply (int refnum, const char *from, const char *comm, const char **ArgList)
{
static	char	format[40];
static	int	last_width = -1;
	int	ok = 1;
	const char	*channel,
			*user,
			*host,
			*server,
			*nick,
			*status,
			*ircname;
	char 	*name;
	WhoEntry *new_w = who_queue_top(refnum);
	int	l;

	if (!new_w)
	{
		WHO_DEBUG("WHOREPLY: server [%d] queue empty.", refnum);
                new_w = get_new_who_entry();
                new_w->line = NULL;
                new_w->end = NULL;
                new_w->who_target = malloc_strdup(star);
                who_queue_add(refnum, new_w);
	}

	if (new_w->undernet_extended)
	{
		WHO_DEBUG("WHOREPLY: Server [%d], who refnum [%d], "
			"Unet request, Std reply.", refnum, new_w->refnum);
		yell("### You asked for an extended undernet request but "
			"didn't get one back. ###");
	}

	/* Who replies always go to the current window. */
	l = message_from(new_w->who_target, LEVEL_OTHER);

do
{
	/*
	 * We have recieved a reply to this query -- its too late to
	 * piggyback it now!
	 */
	if (new_w->dirty == 0)
	{
		WHO_DEBUG("WHOREPLY: Server [%d], who_refnum [%d]: "
				"Reply is now dirty", refnum, new_w->refnum);
		new_w->dirty = 1;
	}
	else
		WHO_DEBUG("WHOREPLY: Server [%d], who_refnum [%d]: Processing",
				refnum, new_w->refnum);

	/*
	 * We dont always want to use this function.
	 * If another function is supposed to do the work for us,
	 * we yield to them.
	 */
	if (new_w->line)
	{
		WHO_DEBUG("WHOREPLY: Dispatching to callback", new_w->refnum);
		new_w->line(refnum, from, comm, ArgList);
		continue;
	}

	if (last_width != get_int_var(CHANNEL_NAME_WIDTH_VAR))
	{
		if ((last_width = get_int_var(CHANNEL_NAME_WIDTH_VAR)) != 0)
		    snprintf(format, sizeof format, 
				"%%-%u.%us %%-9s %%-3s %%s@%%s (%%s)",
					(unsigned) last_width,
					(unsigned) last_width);
		else
		    strlcpy(format, "%s\t%-9s %-3s %s@%s (%s)", sizeof format);
	}

	if (!(channel = ArgList[0]))
		{ rfc1459_odd(from, comm, ArgList); break; }
	if (!(user    = ArgList[1]))
		{ rfc1459_odd(from, comm, ArgList); break; }
	if (!(host    = ArgList[2]))
		{ rfc1459_odd(from, comm, ArgList); break; }
	if (!(server  = ArgList[3]))
		{ rfc1459_odd(from, comm, ArgList); break; }
	if (!(nick    = ArgList[4]))
		{ rfc1459_odd(from, comm, ArgList); break; }
	if (!(status  = ArgList[5]))
		{ rfc1459_odd(from, comm, ArgList); break; }
	PasteArgs(ArgList, 6);
	if (!(ircname  = ArgList[6]))
		{ rfc1459_odd(from, comm, ArgList); break; }
	name = LOCAL_COPY(ircname);

	if (*status == 'S')	/* this only true for the header WHOREPLY */
	{
		char buffer[1024];

		channel = "Channel";
		snprintf(buffer, 1024, "%s %s %s %s %s %s %s", channel,
				nick, status, user, host, server, name);

		if (new_w->who_stuff)
			;			/* munch it */
		else if (do_hook(WHO_LIST, "%s", buffer))
			put_it(format, channel, nick, status, user, host, name);

		return;
	}

	if (new_w && new_w->who_mask)
	{
		if (new_w->who_mask & WHO_HERE)
			ok = ok && (*status == 'H');
		if (new_w->who_mask & WHO_AWAY)
			ok = ok && (*status == 'G');
		if (new_w->who_mask & WHO_OPS)
			ok = ok && (*(status + 1) == '*');
		if (new_w->who_mask & WHO_LUSERS)
			ok = ok && (*(status + 1) != '*');
		if (new_w->who_mask & WHO_CHOPS)
			ok = ok && ((*(status + 1) == '@') ||
				    (*(status + 2) == '@') ||
				    (*(status + 3) == '@'));
		if (new_w->who_mask & WHO_NOCHOPS)
			ok = ok && ((*(status + 1) != '@') &&
				    (*(status + 2) != '@') &&
				    (*(status + 3) != '@'));
		if (new_w->who_mask & WHO_NAME)
			ok = ok && wild_match(new_w->who_name, user);
		if (new_w->who_mask & WHO_NICK)
			ok = ok && wild_match(new_w->who_nick, nick);
		if (new_w->who_mask & WHO_HOST)
			ok = ok && wild_match(new_w->who_host, host);
		if (new_w->who_mask & WHO_REAL)
		{
			char *copy;

			/* First match, including the hopcount */
			ok = ok && wild_match(new_w->who_real, name);

			/* Then remove the hopcount and try again. */
			copy = LOCAL_COPY(name);
			new_next_arg(name, &name);
			ok = ok && wild_match(new_w->who_real, name);
		}
		if (new_w->who_mask & WHO_SERVER)
			ok = ok && wild_match(new_w->who_server, server);
	}

	if (ok)
	{
		char buffer[1024];

		snprintf(buffer, 1023, "%s %s %s %s %s %s %s", channel,
				nick, status, user, host, server, name);

		if (new_w->who_stuff)
			call_lambda_command("WHO", new_w->who_stuff, buffer);

		else if (do_hook(WHO_LIST, "%s", buffer))
		    if (do_hook(current_numeric, "%s", buffer))
			put_it(format, channel, nick, status, user, host, name);
	}

}
while (new_w->piggyback && (new_w = new_w->next));

	WHO_DEBUG("WHOREPLY: Done processing who reply server [%d]", refnum);
	pop_message_from(l);
}

/* Undernet's 354 numeric reply. */
void	xwhoreply (int refnum, const char *from, const char *comm, const char **ArgList)
{
	WhoEntry *new_w = who_queue_top(refnum);
	int	l;

	if (!ArgList[0])
		{ rfc1459_odd(from, comm, ArgList); return; }

	if (!new_w)
	{
		new_w = get_new_who_entry();
		new_w->line = NULL;
		new_w->end = NULL;
		new_w->who_target = malloc_strdup(star);
		new_w->undernet_extended = 1;
		who_queue_add(refnum, new_w);
	}

	if (!new_w->undernet_extended)
		yell("### You got an extended undernet request back "
			"even though you didn't ask for one. ###");

	/* Who replies always go to the current window */
	l = message_from(new_w->who_target, LEVEL_OTHER);
	PasteArgs(ArgList, 0);
	if (new_w->who_stuff)
		call_lambda_command("WHO", new_w->who_stuff, ArgList[0]);
	else if (do_hook(current_numeric, "%s", ArgList[0]))
		put_it("%s %s", banner(), ArgList[0]);
	pop_message_from(l);
}


void	who_end (int refnum, const char *from, const char *comm, const char **ArgList)
{
	WhoEntry 	*new_w = who_queue_top(refnum);
	char 		buffer[1025];
	char		*target = malloc_strdup(ArgList[0]);
	int		l;

	if (!ArgList[0])
		{ rfc1459_odd(from, comm, ArgList); return; }

	if (!new_w)
	{
		WHO_DEBUG("WHOEND: Server [%d], queue is empty.", refnum);
		return;
	}

	PasteArgs(ArgList, 0);

	if (who_whine)
		who_whine = 0;

	l = message_from(new_w->who_target, LEVEL_OTHER);
	do
	{
		/* Defer to another function, if neccesary.  */
		if (new_w->end)
			new_w->end(refnum, from, comm, ArgList);
		else
		{
			snprintf(buffer, 1024, "%s %s", from, ArgList[0]);
			if (new_w->who_end)
			    call_lambda_command("WHO_END", new_w->who_end, buffer);
			else
			    if (do_hook(current_numeric, "%s", buffer))
				put_it("%s %s", banner(), buffer);
		}

		if (strchr(new_w->who_target, ',') && !strchr(target, ','))
		{
		    WHO_DEBUG("WHOEND: Removing one response [%s] from comma-set [%s]", target, new_w->who_target);
		    if (!remove_from_comma_list(new_w->who_target, target))
		    {
			WHO_DEBUG("WHOEND: Server [%d], end of who for refnum [%d]/[%s], (target was not included in comma-set: [%s])!", refnum, new_w->refnum, new_w->who_target, target);
			*new_w->who_target = 0;
		    }
		}
		else if (target && strcmp(target, new_w->who_target))
		{
			WHO_DEBUG("WHOEND: Server [%d], end of who for refnum [%d]/[%s], (target was wrong: [%s])!", refnum, new_w->refnum, new_w->who_target, target);
			*new_w->who_target = 0;
		}
		else
		{
			WHO_DEBUG("WHOEND: Server [%d], end of who for refnum [%d]/[%s]", refnum, new_w->refnum, new_w->who_target);
			*new_w->who_target = 0;
		}
	}
	while (new_w->piggyback && (new_w = new_w->next));
	pop_message_from(l);

	if (!*new_w->who_target)
	{
		WHO_DEBUG("Popping off for server [%d]", refnum);
		who_queue_pop(refnum);
	}
	else
	{
		WHO_DEBUG("NOT POPPING OFF FOR SERVER [%d] BECAUSE THE TOP OF THE WHO QUEUE HAS [%s] LEFT IN THE TARGET LIST", refnum, new_w->who_target);
		yell("WHOEND: Caution -- not popping off who queue -- [%s] is left on target list", new_w->who_target);
	}

	new_free(&target);
}

/*
 * Thanks be to the Dalnet coding team who, in their infinite wisdom,
 * decided to start returning error codes, rather than empty who replies,
 * for who requests that refered to channels or server or whatnot that
 * do not exist.  So as to avoid corrupting the WHO queue, this function is
 * called to give us the opportunity to clean up after any who requests that
 * may be canceled because of the above lamage.
 *
 * Thanks be to the Dalnet coding team for returning error codes that do not
 * tell you what the original request was, making it neigh unto impossible
 * to correctly match up error codes to requests.  Gee whiz, you wouldn't
 * think it would be that hard to get this right.
 */
int	fake_who_end (int refnum, const char *from, const char *comm, const char *who_target)
{
	WhoEntry 	*new_w = who_queue_top(refnum);
	int		l;

	if (who_whine)
		who_whine = 0;
	if (!new_w)
		return 0;	

	/* Only honor these for dalnet extended requests. */
	if (new_w->dalnet_extended == 0)
		return 0;

	if (who_target != NULL)
	{
		char *target;

		target = LOCAL_COPY(who_target);
		while (last_char(target) == ' ')
			chop(target, 1);

		/*
		 * So 'who_target' isn't NULL here.  Make sure it's a 
		 * legitimate match to our current top of who request.
		 */
		if (strncmp(new_w->who_target, target, strlen(target)))
			return 0;

		who_target = target;
	}

	l = message_from(new_w->who_target, LEVEL_OTHER);
	do
	{
		/* Defer to another function, if neccesary.  */
		if (new_w->end)
		{
			const char *fake_ArgList[3];

			/* Fabricate a fake argument list */
			fake_ArgList[0] = new_w->who_target;
			fake_ArgList[1] = "fake_who_end";
			fake_ArgList[2] = NULL;
			new_w->end(refnum, from, comm, fake_ArgList);
		}
		else if (new_w->who_end)
		{
		    char	buffer[1025];

		    snprintf(buffer, 1024, "%s %s", 
				from, new_w->who_target);
		    call_lambda_command("WHO_END", new_w->who_end, buffer);
		}
	} 
	while (new_w->piggyback && (new_w = new_w->next));
	pop_message_from(l);

	who_queue_pop(refnum);
	return 1;
}


/*
 *
 *
 *
 * 				ISON QUEUE
 *
 *
 *
 */
static void ison_queue_add (int refnum, IsonEntry *item, int next)
{
	Server *s;
	IsonEntry *bottom;

	if (!(s = get_server(refnum)))
		return;

	if (next)
	{
		item->next = s->ison_wait;
		s->ison_wait = item;
		return;
	}

	bottom = s->ison_wait;
	while (bottom && bottom->next)
		bottom = bottom->next;

	if (!bottom)
		s->ison_wait = item;
	else
		bottom->next = item;

	return;
}

static void ison_queue_send (int refnum)
{
	int count = 1;
	Server *s;
	IsonEntry *save, *bottom;

	if (!(s = get_server(refnum)))
		return;

	if (!(save = s->ison_wait))
		return;

	bottom = s->ison_queue;
	while (bottom)
	{
		if (s->ison_max && ++count > s->ison_max)
			return;
		else if (bottom->next)
			bottom = bottom->next;
		else
			break;
	}

	s->ison_wait = save->next;

	if (bottom)
		bottom->next = save;
	else
		s->ison_queue = save;

	save->next = NULL;

	send_to_aserver(refnum, "ISON %s", save->ison_asked);
}

static void ison_queue_pop (int refnum)
{
	Server *s;
	IsonEntry *save;

	if (!(s = get_server(refnum)))
		return;

	if ((save = s->ison_queue))
	{
		s->ison_queue = save->next;
		new_free(&save->ison_asked);
		new_free(&save->ison_got);
		new_free(&save->oncmd);
		new_free(&save->offcmd);
		new_free(&save->endcmd);
		new_free((char **)&save);
	}
	return;
}

static IsonEntry *ison_queue_top (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return NULL;

	return s->ison_queue;
}

static IsonEntry *get_new_ison_entry (int refnum, int next)
{
	Server *s;
	IsonEntry *new_w;

	if (!(s = get_server(refnum)))
		return NULL;

	new_w = (IsonEntry *)new_malloc(sizeof(IsonEntry));
	new_w->ison_asked = NULL;
	new_w->ison_got = NULL;
	new_w->next = NULL;
	new_w->line = NULL;
	new_w->oncmd = NULL;
	new_w->offcmd = NULL;
	new_w->endcmd = NULL;
	ison_queue_add(refnum, new_w, next);
	return new_w;
}

static void ison_queue_list (int refnum)
{
	Server *s;
	IsonEntry *item;
	int count = 0;

	if (!(s = get_server(refnum)))
		return;

	for (item = s->ison_queue; item; item = item->next, count++)
	{
		yell("[%d] [%s] [%p]", count, item->ison_asked, 
				item->line);
	}

	for (item = s->ison_wait; item; item = item->next, count++)
	{
		yell("[%d] [%s] [%p] (pending)", count, item->ison_asked, 
				item->line);
	}
}

BUILT_IN_COMMAND(isoncmd)
{
	if (!args || !*args)
		args = LOCAL_COPY(get_server_nickname(from_server));

	isonbase(from_server, args, NULL);
}

void	isonbase (int refnum, char *args, void (*line) (int, char *, char *))
{
	IsonEntry 	*new_i;
	char 		*next = args;
	char		*on_cmd = NULL, *offcmd = NULL, *endcmd = NULL;
	int		sendnext = 0;

	/* Maybe should output a warning? */
	if (!is_server_registered(refnum))
		return;

	while (args && *args == '-')
	{
		char *arg = next_arg(args, &args);
		char *stuff;

		if (!my_stricmp(arg, "-d"))
		{
			ison_queue_list(from_server);
		}
		if (!my_stricmp(arg, "-f"))
		{
			while (ison_queue_top(from_server))
				ison_queue_pop(from_server);
		}
		if (!my_stricmp(arg, "-s"))
		{
			ison_queue_send(refnum);
		}
		if (!my_stricmp(arg, "-n"))
		{
			sendnext++;
		}
		if (!my_stricmp(arg, "-e") && get_server(refnum)->ison_wait)
		{
			return;
		}
		if (!my_stricmp(arg, "-len"))
		{
			if ((stuff = next_arg(args, &args)))
				get_server(refnum)->ison_len = MAX(100, atol(stuff));
			else
				say("Need numeric argument for -LEN argument.");
		}
		if (!my_stricmp(arg, "-max"))
		{
			if ((stuff = next_arg(args, &args)))
				get_server(refnum)->ison_max = atol(stuff);
			else
				say("Need numeric argument for -MAX argument.");
		}
		if (!my_stricmp(arg, "-oncmd"))
		{
			if ((stuff = next_expr(&args, '{')))
				on_cmd = stuff;
			else
				say("Need {...} argument for -ONCMD argument.");
		}
		if (!my_stricmp(arg, "-offcmd"))
		{
			if ((stuff = next_expr(&args, '{')))
				offcmd = stuff;
			else
				say("Need {...} argument for -OFFCMD argument.");
		}
		if (!my_stricmp(arg, "-end"))
		{
			if ((stuff = next_expr(&args, '{')))
				endcmd = stuff;
			else
				say("Need {...} argument for -END argument.");
		}
	}

	ison_queue_send(refnum);
	if (!args || !*args)
		return;

	next = args;
	while ((args = next))
	{
		new_i = get_new_ison_entry(refnum, sendnext);
		new_i->line = line;
		if ((int)strlen(args) > get_server(refnum)->ison_len)
		{
			next = args + get_server(refnum)->ison_len;
			while (!isspace(*next))
				next--;
			*next++ = 0;
		}
		else
			next = NULL;

		malloc_strcpy(&new_i->ison_asked, args);
		malloc_strcpy(&new_i->oncmd, on_cmd);
		malloc_strcpy(&new_i->offcmd, offcmd);
		if (!next)
			malloc_strcpy(&new_i->endcmd, endcmd);
		ison_queue_send(refnum);
	}
}

/* 
 * ison_returned: this is called when numeric 303 is received in
 * numbers.c. ISON must always be the property of the WHOIS queue.
 * Although we will check first that the top element expected is
 * actually an ISON.
 */
void	ison_returned (int refnum, const char *from, const char *comm, const char **ArgList)
{
	IsonEntry *new_i = ison_queue_top(refnum);
	char	*do_off = NULL, *this1, *all1, *this2, *all2;
	size_t	clue = 0;

	if (!ArgList[0])
		{ rfc1459_odd(from, comm, ArgList); return; }

	if (!new_i)
	{
		/* XXX Hack to work around rogue /quote ison's */
		if (do_hook(current_numeric, "%s", ArgList[0]))
			put_it("%s Currently online: %s", banner(), ArgList[0]);
		return;
	}

	all1 = LOCAL_COPY(new_i->ison_asked);
	all2 = LOCAL_COPY(ArgList[0]);
	if (new_i->offcmd)
		while ((this2 = next_arg(all2, &all2)))
			while ((this1 = next_arg(all1, &all1)) && my_stricmp(this1, this2))
				malloc_strcat_wordlist_c(&do_off, space, this1, &clue);
	malloc_strcat_wordlist_c(&do_off, space, all1, &clue);

	PasteArgs(ArgList, 0);
	if (new_i->line) 
	{
		char *ison_ret = LOCAL_COPY(ArgList[0]);
		new_i->line(refnum, new_i->ison_asked, ison_ret);
	}
	else
	{
		if (new_i->oncmd && ArgList[0] && ArgList[0][0])
			call_lambda_command("ISON", new_i->oncmd, ArgList[0]);
		if (new_i->offcmd && do_off && *do_off)
			call_lambda_command("ISON", new_i->offcmd, do_off);
		if (new_i->endcmd)
			call_lambda_command("ISON", new_i->endcmd, NULL);
		if (!new_i->oncmd && !new_i->offcmd &&
				do_hook(current_numeric, "%s", ArgList[0]))
			put_it("%s Currently online: %s", banner(), ArgList[0]);
	}

	new_free(&do_off);
	ison_queue_pop(refnum);
	ison_queue_send(refnum);
	return;
}


/*
 *
 *
 *
 *
 *				USERHOST QUEUE
 *
 *
 *
 *
 */
static UserhostEntry *userhost_queue_top (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return NULL;

	return s->userhost_queue;
}

static void userhost_queue_add (int refnum, UserhostEntry *item)
{
	UserhostEntry *bottom;
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	bottom = s->userhost_wait;
	while (bottom && bottom->next)
		bottom = bottom->next;

	if (!bottom)
		s->userhost_wait = item;
	else
		bottom->next = item;

	return;
}

static void userhost_queue_send (int refnum)
{
	int count = 1;
	Server *s;
	UserhostEntry *save, *bottom;

	if (!(s = get_server(refnum)))
		return;

	if (!(save = s->userhost_wait))
		return;

	bottom = s->userhost_queue;
	while (bottom)
	{
		if (s->userhost_max && ++count > s->userhost_max)
			return;
		else if (bottom->next)
			bottom = bottom->next;
		else
			break;
	}

	s->userhost_wait = save->next;

	if (bottom)
		bottom->next = save;
	else
		s->userhost_queue = save;

	save->next = NULL;

	send_to_aserver(refnum, save->format, save->userhost_asked);
}

static void userhost_queue_pop (int refnum)
{
	UserhostEntry *save;
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	save = s->userhost_queue;
	s->userhost_queue = save->next;
	new_free(&save->userhost_asked);
	new_free(&save->text);
	new_free((char **)&save);
	return;
}

static UserhostEntry *get_new_userhost_entry (int refnum)
{
	UserhostEntry *new_u = (UserhostEntry *)new_malloc(sizeof(UserhostEntry));
	new_u->format = NULL;
	new_u->userhost_asked = NULL;
	new_u->text = NULL;
	new_u->next = NULL;
	new_u->func = NULL;
	userhost_queue_add(refnum, new_u);
	return new_u;
}

/*
 * userhost: Does the USERHOST command.  Need to split up the queries,
 * since the server will only reply to 5 at a time.
 */
BUILT_IN_COMMAND(userhostcmd)
{
	userhostbase(from_server, args, subargs, NULL, 1);
}

BUILT_IN_COMMAND(useripcmd)
{
	userhostbase(from_server, args, subargs, NULL, 0);
}

BUILT_IN_COMMAND(usripcmd)
{
	userhostbase(from_server, args, subargs, NULL, 2);
}

void userhostbase (int refnum, char *args, const char *subargs, void (*line) (int, UserhostItem *, const char *, const char *), int do_userhost)
{
	int	total = 0,
		userhost_cmd = 0;
	int	server_query_reqd = 0;
	char	*nick;
	char	buffer[BIG_BUFFER_SIZE + 1];
	char 	*ptr, 
		*next_ptr,
		*body = NULL;
	int	count = 5;
	char	*extra = NULL;

	/* Maybe should output a warning? */
	if (!is_server_registered(refnum))
		return;

	*buffer = 0;
	while ((nick = next_arg(args, &args)) != NULL)
	{
		if (check_nickname(nick, 1) || is_number(nick))
		{
			total++;
			if (!fetch_userhost(refnum, NULL, nick))
				server_query_reqd++;

			if (*buffer)
				strlcat(buffer, " ", sizeof buffer);
			strlcat(buffer, nick, sizeof buffer);
		}

		else if (!my_strnicmp(nick, "-direct", 2))
			server_query_reqd++;

		else if (!my_strnicmp(nick, "-count", 3))
			count = atol(safe_new_next_arg(args, &args));

		else if (!my_strnicmp(nick, "-cmd", 2))
		{
			if (!total)
			{
				if (do_userhost == 1)
					say("USERHOST -cmd with no nicks specified");
				else if (do_userhost == 0)
					say("USERIP -cmd with no nicks specified");
				else
					say("USRIP -cmd with no nicks specified");
				return;
			}

			while (my_isspace(*args))
				args++;

			if (!(body = next_expr_failok(&args, '{'))) /* } */
				body = args;

			userhost_cmd = 1;
			break;
		}

		else if (!my_strnicmp(nick, "-extra", 2))
		{
			char *extravar = next_arg(args, &args);
			if (empty(extravar))
			{
				say("Need argument to /userhost -extra");
				break;
			}
			if (extra)
				new_free(&extra);
			/* XXX But what if extravar contains []s? */
			extra = get_variable(extravar);
		}
	}

	if (!userhost_cmd && !total)
	{
		server_query_reqd++;
		strlcpy(buffer, get_server_nickname(refnum), sizeof buffer);
	}

	ptr = buffer;

	if (server_query_reqd || (!line && !userhost_cmd))
	{
		ptr = buffer;
		while (ptr && *ptr)
		{
			UserhostEntry *new_u = get_new_userhost_entry(refnum);

			move_to_abs_word(ptr, (const char **)&next_ptr, count);

			if (next_ptr && *next_ptr && next_ptr > ptr)
				next_ptr[-1] = 0;

			new_u->userhost_asked = malloc_strdup(ptr);
			if (do_userhost == 1)
				new_u->format = "USERHOST %s";
			else if (do_userhost == 0)
				new_u->format = "USERIP %s";
			else
				new_u->format = "USRIP %s";

			userhost_queue_send(refnum);

			if (userhost_cmd)
				new_u->text = malloc_strdup(body);

			if (line)
				new_u->func = line;
			else if (userhost_cmd)
				new_u->func = userhost_cmd_returned;
			else
				new_u->func = NULL;

			new_u->extra = extra;
			ptr = next_ptr;
		}
	}
	else
	{
		while (ptr && *ptr)
		{
			char *my_nick = next_arg(ptr, &ptr);
			const char *ouh = fetch_userhost(refnum, NULL, my_nick);
			char *uh, *host;
			UserhostItem item = {NULL, 0, 0, 0, NULL, NULL, NULL};

			uh = LOCAL_COPY(ouh);
			item.nick = my_nick;
			item.oper = 0;
			item.connected = 1;
			item.away = 0;
			item.user = uh;
			item.extra = extra;
			host = strchr(uh, '@');
			if (host) {
				*host++ = 0;
				item.host = host;
			} else
				item.host = "<UNKNOWN>";

			if (line)
				line(refnum, &item, my_nick, body);
			else if (userhost_cmd)
				userhost_cmd_returned(refnum, &item, my_nick, body);
			else
				yell("Yowza!  I dont know what to do here!");
		}
		new_free(&extra);
	}
}

/* 
 * userhost_returned: this is called when numeric 302 is received in
 * numbers.c. USERHOST must always remain the property of the userhost
 * queue.  Sending out USERHOST requests to the server without going
 * through this queue will cause it to be corrupted and the client will
 * go higgledy-piggledy.
 */
void	userhost_returned (int refnum, const char *from, const char *comm, const char **ArgList)
{
	UserhostEntry *top = userhost_queue_top(refnum);
	char *ptr;
	char *results;

	if (!ArgList[0])
		{ rfc1459_odd(from, comm, ArgList); return; }

	if (!top)
	{
		yell("### Please don't /quote a server command that returns the 302 numeric.");
		return;
	}

	PasteArgs(ArgList, 0);
	results = LOCAL_COPY(ArgList[0]);
	ptr = top->userhost_asked;

	/*
	 * Go through the nicknames that were requested...
	 */
	while (ptr && *ptr)
	{
		/*
		 * Grab the next nickname
		 */
		char *	cnick;
		size_t	len;

		cnick = next_arg(ptr, &ptr);
		len = strlen(cnick);

		/*
		 * Now either it is present at the next argument
		 * or its not.  If it is, it will match the first
		 * part of ArgList, and the following char will
		 * either be a * or an = (eg, nick*= or nick=)
		 */
		if (results && (!my_strnicmp(cnick, results, len)
	            && (results[len] == '*' || results[len] == '=')))
		{
			UserhostItem item;
			char *nick, *user, *host;

			/* Extract all the interesting info */
			item.connected = 1;
			nick = next_arg(results, &results);
			user = strchr(nick, '=');
			if (!user)
			{
				yell("Can't parse useless USERHOST reply [%s]", 
						ArgList[0]);
				userhost_queue_pop(refnum);
			}

			if (user[-1] == '*')
			{
				user[-1] = 0;
				item.oper = 1;
			}
			else
				item.oper = 0;

			if (user[1] == '+')
				item.away = 0;
			else
				item.away = 1;

			*user++ = 0;
			user++;

			host = strchr(user, '@');
			if (!host)
			{
				yell("Can't parse useless USERHOST reply [%s]", 
						ArgList[0]);
				userhost_queue_pop(refnum);
				return;
			}
			*host++ = 0;

			item.nick = nick;
			item.user = user;
			item.host = host;
			item.extra = SAFE(top->extra);

			/*
			 * If the user wanted a callback, then
			 * feed the callback with the info.
			 */
			if (top->func)
				top->func(refnum, &item, cnick, top->text);

			/*
			 * Otherwise, the user just did /userhost,
			 * so we offer the numeric, and if the user
			 * doesnt bite, we output to the screen.
			 */
			else if (do_hook(current_numeric, "%s %s %s %s %s %s", 
						item.nick,
						item.oper ? "+" : "-", 
						item.away ? "-" : "+", 
						item.user, item.host, 
						item.extra))
				put_it("%s %s is %s@%s%s%s %s", banner(),
						item.nick, item.user, item.host, 
						item.oper ?  " (Is an IRC operator)" : empty_string,
						item.away ? " (away)" : empty_string,
						item.extra ? item.extra : empty_string);
		}

		/*
		 * If ArgList isnt the current nick, then the current nick
		 * must not be on irc.  So we whip up a dummy UserhostItem
		 * and send it on its way.  We DO NOT HOOK the 302 numeric
		 * with this bogus entry, because thats the historical
		 * behavior.  This can cause a problem if you do a USERHOST
		 * and wait on the 302 numeric.  I think waiting on the 302
		 * numeric is stupid, anyhow.
		 */
		else
		{
			/*
			 * Of course, only if the user asked for a callback
			 * via /userhost -cmd or a direct call to userhostbase.
			 * If the user just did /userhost, and the nicks arent
			 * on, then we just dont display anything.
			 */
			if (top->func)
			{
				UserhostItem item;

				item.nick = cnick;
				item.user = item.host = "<UNKNOWN>";
				item.oper = item.away = 0;
				item.connected = 1;
				item.extra = top->extra;

				top->func(refnum, &item, cnick, top->text);
			}
		}
	}

	userhost_queue_pop(refnum);
	userhost_queue_send(refnum);
}

void	userhost_cmd_returned (int refnum, UserhostItem *stuff, const char *nick, const char *text)
{
	char	*args = NULL;
	size_t	clue = 0;

	/* This should be safe, though its playing it fast and loose */
	malloc_strcat_c(&args, stuff->nick ? stuff->nick : empty_string, &clue);
	malloc_strcat_c(&args, stuff->oper ? " + " : " - ", &clue);
	malloc_strcat_c(&args, stuff->away ? "+ " : "- ", &clue);
	malloc_strcat_c(&args, stuff->user ? stuff->user : empty_string, &clue);
	malloc_strcat_c(&args, space, &clue);
	malloc_strcat_c(&args, stuff->host ? stuff->host : empty_string, &clue);
	malloc_strcat_c(&args, space, &clue);
	malloc_strcat_c(&args, stuff->extra ? stuff->extra : empty_string, &clue);
	call_lambda_command("USERHOST", text, args);

	new_free(&args);
}

void	clean_server_queues (int i)
{
	while (who_queue_top(i))
		who_queue_pop(i);

	while (ison_queue_top(i))
		ison_queue_pop(i);

	while (userhost_queue_top(i))
		userhost_queue_pop(i);
}


/* XXXX */

static int	who_queue_debug (void *unused)
{
	Server *s;
	int	refnum;
	WhoEntry *item;
	double d;
static	int	last_refnum_checked = 0;

	for (refnum = 0; refnum < number_of_servers; refnum++)
	{
	    if (!(s = get_server(refnum)))
		continue;

	    for (item = s->who_queue; item; item = item->next)
	    {
		if (item->refnum >= last_refnum_checked + 100)
		{
			WHO_DEBUG("WATCHER: Who refnum is up to [%d]", 
					item->refnum);
			last_refnum_checked = item->refnum;
		}

		if (item->dirty == 0)
		{
		    d = time_diff(item->request_time, now);
		    if (d >= 15)
		    {
			yell("Warning: who item [%d] (server %d) is not dirty and > 15 seconds old", item->refnum, refnum);
			WHO_DEBUG("WATCHER: Who item [%d] is not dirty, > 15 seconds old", item->refnum);
			WHO_DEBUG("WATCHER: [%s]", who_item_full_desc(item));
			yell("Flushing who queue for server [%d] -- check logs", refnum);
			who_queue_flush(refnum);
		    }
		}
		else
		{
		    d = time_diff(item->request_time, now);
		    if (d >= 30)
		    {
			yell("Warning: who item [%d] (server %d) is dirty and > 30 seconds old", item->refnum, refnum);
			WHO_DEBUG("WATCHER: Who item [%d] is dirty, > 30 seconds old", item->refnum);
			WHO_DEBUG("WATCHER: [%s]", who_item_full_desc(item));
			yell("Flushing who queue for server [%d] -- check logs", refnum);
			who_queue_flush(refnum);
		    }
		    else
		    {
		        d = time_diff(item->dirty_time, now);
		        if (d >= 15)
		        {
			   yell("Warning: who item [%d] (server %d) is dirty for > 15 seconds", item->refnum, refnum);
			   WHO_DEBUG("WATCHER: Who item [%d] is dirty > 15 seconds", item->refnum);
			   WHO_DEBUG("WATCHER: [%s]", who_item_full_desc(item));
			}
			yell("Flushing who queue for server [%d] -- check logs", refnum);
			who_queue_flush(refnum);
		    }
		}
	    }
	}

	return 0;
}


