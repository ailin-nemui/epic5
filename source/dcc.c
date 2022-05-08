/*
 * dcc.c: Things dealing client to client connections. 
 *
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1995, 2015 EPIC Software Labs
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
#include "sedcrypt.h"
#include "ctcp.h"
#include "dcc.h"
#include "functions.h"
#include "hook.h"
#include "ircaux.h"
#include "lastlog.h"
#include "newio.h"
#include "output.h"
#include "parse.h"
#include "server.h"
#include "status.h"
#include "vars.h"
#include "window.h"
#include "termx.h"
#include "reg.h"
#include "alias.h"
#include "timer.h"

#define DCC_BLOCK_SIZE (1<<11)

/* This should probably be configurable. */
#define DCC_RCV_BLOCK_SIZE (1<<16)

/* These are the settings for ``flags'' */
#define DCC_CHAT	0x0001U
#define DCC_FILEOFFER	0x0002U
#define DCC_FILEREAD	0x0003U
#define DCC_RAW		0x0004U
#define DCC_RAW_LISTEN	0x0005U
#define DCC_TYPES	0x000fU
#define DCC_MY_OFFER	0x0010U
#define DCC_ACTIVE	0x0020U
#define DCC_THEIR_OFFER	0x0040U
#define DCC_DELETE	0x0080U
#define DCC_TWOCLIENTS	0x0100U
#define DCC_REJECTED	0x0200U
#define DCC_QUOTED	0x0400U
#define DCC_CONNECTING	0x0800U
#define DCC_RESUME_REQ	0x1000U
#define DCC_STATES	0xfff0U

static char *dcc_target (const char *name) 
{
	size_t	len;
	char *	ret;

	len = strlen(name);
	ret = new_malloc(len + 2);
	snprintf(ret, len + 2, "=%s", name);

	return ret;
}

typedef	struct	DCC_struct
{
	unsigned	flags;
	int		family;
	int		locked;			/* XXX - Sigh */
	int		socket;
	int		file;
	int		held;
	int		packet_size;
	int		full_line_buffer;
	long		refnum;
	intmax_t	filesize;
	char *		local_filename;
	char *		description;
	char *		othername;
	char *		user;
	char *		userhost;
struct	DCC_struct *	next;

	SSu		offer;			/* Their offer */
	SSu		peer_sockaddr;		/* Their saddr */
	SSu		local_sockaddr;		/* Our saddr */
	unsigned short	want_port;		/* HOST ORDER */

	intmax_t	bytes_read;		/* INTMAX */
	intmax_t	bytes_sent;		/* IM */
	intmax_t	bytes_acked;		/* IM */
	intmax_t	resume_size;		/* IM */

	Timeval		lasttime;
	Timeval		starttime;
	Timeval		holdtime;
	double		heldtime;

	int		(*open_callback) (struct DCC_struct *);
	int		server;
	int		updates_status;
}	DCC_list;

static	DCC_list *	ClientList = NULL;
static	char		DCC_current_transfer_buffer[256];
static	int		dcc_global_lock = 0;
static	int		dccs_rejected = 0;
static	int		dcc_refnum = 0;
static	int		dcc_updates_status = 1;
static	char *		default_dcc_port = NULL;

#define DCC_SUBCOMMAND(x)  static void x (int argc, char **argv, const char *subargs)
static	void		dcc_chat 		(char *);
DCC_SUBCOMMAND(dcc_chat_subcmd);
static	void		dcc_get 		(char *);
#ifdef MIRC_BROKEN_DCC_RESUME
static	void		dcc_resume 		(char *);
#endif
DCC_SUBCOMMAND(dcc_get_subcmd);
static	void 		dcc_close 		(char *);
DCC_SUBCOMMAND(dcc_close_subcmd);
static	void		dcc_closeall		(char *);
DCC_SUBCOMMAND(dcc_closeall_subcmd);

static	void		dcc_filesend 		(char *);
static	void		dcc_send_raw 		(char *);
static	void		dcc_list 		(char *args);
static	void		dcc_rename 		(char *);

static	DCC_list *	dcc_searchlist 		(unsigned, const char *, const char *, const char *, int);
static	void		dcc_erase 		(DCC_list *);
static	void 		dcc_garbage_collect 	(void);
static	int		dcc_connected		(int);
static	int		dcc_connect 		(DCC_list *);
static	int		dcc_listen		(DCC_list *);
static 	void		dcc_send_booster_ctcp 	(DCC_list *dcc);

static	void		do_dcc 			(int fd);
static	void		process_dcc_chat	(DCC_list *);
static	void		process_incoming_listen (DCC_list *);
static	void		process_incoming_raw 	(DCC_list *);
static	void		process_dcc_send 	(DCC_list *);
static	void		process_incoming_file 	(DCC_list *);

static	void 		output_reject_ctcp 	(int, char *, char *);
static	void		DCC_close_filesend 	(DCC_list *, const char *, const char *);
static	void		update_transfer_buffer 	(DCC_list *, const char *format, ...);
static	char *		dcc_urlencode		(const char *);
static	char *		dcc_urldecode		(const char *);
static	void		fill_in_default_port	(DCC_list *dcc);

#ifdef MIRC_BROKEN_DCC_RESUME
static 	void 		dcc_getfile_resume_demanded (const char *, char *, char *, char *);
static	void		dcc_getfile_resume_start    (const char *, char *, char *, char *);
#endif


/*
 * These are the possible <type> arguments to /DCC
 */
typedef void (*dcc_function) (char *);
struct
{
	const char *	name;
	dcc_function 	function;
}	dcc_commands[] =
{
	{ "CHAT",	dcc_chat 		},	/* DCC_CHAT */
	{ "SEND",	dcc_filesend 		},	/* DCC_FILEOFFER */
	{ "GET",	dcc_get 		},	/* DCC_FILEREAD */
	{ "RAW",	dcc_send_raw 		},	/* DCC_RAW */

	{ "CLOSE",	dcc_close 		},
	{ "CLOSEALL",	dcc_closeall		},
	{ "LIST",	dcc_list 		},
	{ "RENAME",	dcc_rename 		},
#ifdef MIRC_BROKEN_DCC_RESUME
	{ "RESUME",	dcc_resume 		},
#endif
	{ NULL,		(dcc_function) NULL 	}
};

/*
 * These are the possible types of DCCs that can be open.
 */
static const char	*dcc_types[] =
{
	"<none>",
	"CHAT",		/* DCC_CHAT */
	"SEND",		/* DCC_FILEOFFER */
	"GET",		/* DCC_FILEREAD */
	"RAW",		/* DCC_RAW */
	"RAW_LISTEN",
	NULL
};

static DCC_list *	get_dcc_by_filedesc (int fd)
{
	DCC_list *	dcc = NULL;

	for (dcc = ClientList; dcc ; dcc = dcc->next)
	{
		/* Never return deleted entries */
		if (dcc->flags & DCC_DELETE)
			continue;

		if (dcc->socket == fd)
			break;
	}

	return dcc;
}

static DCC_list *	get_dcc_by_refnum (int refnum)
{
	DCC_list *	dcc = NULL;

	for (dcc = ClientList; dcc; dcc = dcc->next)
	{
		if (dcc->flags & DCC_DELETE)
			continue;

		if (dcc->refnum == refnum)
			break;
	}

	return dcc;
}


/************************************************************************/
/*
 * remove_from_dcc_list: What do you think it does?
 */
static void	dcc_remove_from_list (DCC_list *erased)
{
	if (x_debug & DEBUG_DCC_XMIT)
		yell("Removing %p from dcc list", erased);

	if (erased == ClientList)
		ClientList = erased->next;
	else
	{
		DCC_list *prev = NULL;

		for (prev = ClientList; prev; prev = prev->next)
			if (prev->next == erased)
				break;
		if (prev)
			prev->next = erased->next;
	}
}

/*
 * We use a primitive form of cooperative reference counting to figure out
 * when it's ok to delete an item.  Whenever anyone iterates over ClientList,
 * they "lock" with NULL to claim a reference on all DCC items.  When they
 * are done, they "unlock" with NULL to release the references to all DCC
 * items.  It is also possible to claim a reference on a particular DCC item.
 * All references must be released later, naturally.  A DCC item is garbage
 * collected when it is marked as "DELETE"d, it has no reference locks, and
 * there are no global reference locks.
 */
static void 	dcc_garbage_collect (void)
{
	DCC_list *dcc;
	int	need_update = 0;

	if (dcc_global_lock)		/* XXX Yea, yea, yea */
	{
		if (x_debug & DEBUG_DCC_XMIT)
			yell("Garbage collection waiting for global lock");
		return;
	}

	dcc = ClientList;
	while (dcc)
	{
		if ((dcc->flags & DCC_DELETE) && dcc->locked == 0)
		{
			if ((dcc->flags & DCC_TYPES) == DCC_FILEOFFER ||
			    (dcc->flags & DCC_TYPES) == DCC_FILEREAD)
				need_update = 1;

			if (x_debug & DEBUG_DCC_XMIT)
				yell("DCC %p being GC'd", dcc);
			dcc_erase(dcc);
			dcc = ClientList;	/* Start over */
		}
		else
			dcc = dcc->next;
	}

	if (need_update)
		update_transfer_buffer(NULL, NULL);	/* Whatever */
}

/*
 * Note that 'erased' does not neccesarily have to be on ClientList.
 * In fact, it may very well NOT be on ClientList.  The handling at the
 * beginning of the function is only to sanity check that it isnt on 
 * ClientList when we blow it away.
 */
static 	void		dcc_erase (DCC_list *erased)
{
	if (x_debug & DEBUG_DCC_XMIT)
		yell("DCC %p being erased", erased);

	dcc_remove_from_list(erased);

	/*
	 * Automatically grok for REJECTs
	 */
	if (!(erased->flags & DCC_REJECTED))
	{
	    if (erased->flags & (DCC_MY_OFFER|DCC_THEIR_OFFER))
	     if (get_int_var(DCC_AUTO_SEND_REJECTS_VAR))
	      do
	      {
		unsigned my_type = erased->flags & DCC_TYPES;
		char	*dummy_ptr = NULL;
		char 	*nopath;
		static time_t	last_reject = 0;
		time_t	right_now;

		time(&right_now);
		if (right_now - last_reject < 2)
			break;		/* Throttle flood attempts */

		last_reject = right_now;

		if (erased->description && 
				(nopath = strrchr(erased->description, '/')))
			nopath++;
		else
			nopath = erased->description;

		malloc_sprintf(&dummy_ptr, "%s %s %s", 
			erased->user,
			(my_type == DCC_FILEOFFER ? "GET" :
			 (my_type == DCC_FILEREAD  ? "SEND" : 
			   dcc_types[my_type])),
			nopath ? nopath : "<any>");

		erased->flags |= DCC_REJECTED;

		/*
		 * And output it to the user
		 */
		if (is_server_registered(erased->server))
		{
		    if (!dead && *(erased->user) != '=')
			isonbase(erased->server, dummy_ptr, output_reject_ctcp);
		    else
			output_reject_ctcp(erased->server, dummy_ptr, erased->user);
		}
	      }
	      while (0);
	}

	/*
	 * In any event, blow it away.
	 */
	erased->socket = new_close(erased->socket);
	close(erased->file);
	erased->file = -1;
	new_free(&erased->description);	/* Magic check failure here */
	new_free(&erased->local_filename);
	new_free(&erased->user);
	new_free(&erased->othername);
	new_free(&erased->userhost);
	new_free((char **)&erased);

	if (x_debug & DEBUG_DCC_XMIT)
		yell("DCC erased");
}

/*
 * close_all_dcc:  We call this when we create a new process so that
 * we don't leave any fd's lying around, that won't close when we
 * want them to..
 */
void 	close_all_dcc (void)
{
	DCC_list *dcc;
	int	l;

	dccs_rejected = 0;

	while ((dcc = ClientList))
		dcc_erase(dcc);

	if (dccs_rejected)
	{
		l = message_from(NULL, LEVEL_DCC);
		say("Waiting for DCC REJECTs to be sent");
		sleep(1);
		pop_message_from(l);
	}
}

/*
 * Place the dcc on hold.  Return 1
 * (fail) if it was already on hold.
 */
static int	dcc_hold (DCC_list *dcc)
{
	new_hold_fd(dcc->socket);
	if (dcc->held)
		return 1;
	else {
		get_time(&dcc->holdtime);
		dcc->held = 1;
		return 0;
	}
}

/*
 * Remove the hold.  Return 1
 * (fail) if it was already unheld.
 */
static int	dcc_unhold (DCC_list *dcc)
{
	Timeval right_now;

	new_unhold_fd(dcc->socket);
	if (!dcc->held)
		return 1;
	else {
		get_time(&right_now);
		dcc->heldtime += time_diff(dcc->holdtime, right_now);
		get_time(&dcc->holdtime);
		dcc->held = 0;
		return 0;
	}
}


static int	lock_dcc (DCC_list *dcc)
{
	if (x_debug & DEBUG_DCC_XMIT)
		yell("DCC %p being locked", dcc);

	if (dcc)
		dcc->locked++;
	dcc_global_lock++;
	return 0;
}

static int	unlock_dcc (DCC_list *dcc)
{
	if (x_debug & DEBUG_DCC_XMIT)
		yell("DCC %p being unlocked", dcc);

	if (dcc)
		dcc->locked--;
	dcc_global_lock--;

	if (dcc_global_lock == 0)
		dcc_garbage_collect();		/* XXX Maybe unnecessary */
	return 0;
}



/************************************************************************/
/*
 * These functions handle important DCC jobs.
 */
static	DCC_list *dcc_create (
	unsigned	type,		/* Type of connection we want */
	const char *	user, 		/* Nick of the remote peer */
	const char *	description,	/* 
					 * DCC Type specific information,
					 * Usually a full pathname for 
					 * SEND/GET, "listen" or "connect"
					 * for RAW, or NULL for others.
					 */
	const char *	othername, 	/* Alias filename for SEND/GET */
	int		family,
	intmax_t	filesize)
{
	DCC_list *new_client;

	new_client 			= new_malloc(sizeof(DCC_list));
	new_client->flags 		= type;
	new_client->family		= family;
	new_client->locked		= 0;
	new_client->socket 		= -1;
	new_client->file 		= -1;
	new_client->filesize 		= filesize;
	new_client->held		= 0;
	new_client->local_filename 	= NULL;
	new_client->next 		= ClientList;
	new_client->user 		= malloc_strdup(user);
	new_client->userhost 		= (FromUserHost && *FromUserHost)
					? malloc_strdup(FromUserHost)
					: malloc_strdup(unknown_userhost);
	new_client->description 	= malloc_strdup(description);
	new_client->othername 		= malloc_strdup(othername);
	new_client->bytes_read 		= 0;
	new_client->bytes_sent 		= 0;
	new_client->bytes_acked		= 0;
	new_client->starttime.tv_sec 	= 0;
	new_client->starttime.tv_usec 	= 0;
	new_client->holdtime.tv_sec 	= 0;
	new_client->holdtime.tv_usec 	= 0;
	new_client->heldtime		= 0.0;
	new_client->want_port 		= 0;
	new_client->resume_size		= 0;
	new_client->open_callback	= NULL;
	new_client->refnum		= dcc_refnum++;
	new_client->server		= from_server;
	new_client->updates_status	= 1;
	new_client->packet_size		= 0;
	new_client->full_line_buffer	= 0;
	memset(&new_client->offer.ss, 0, sizeof(new_client->offer.ss));
	memset(&new_client->peer_sockaddr.ss, 0, sizeof(new_client->peer_sockaddr.ss));
	memset(&new_client->local_sockaddr.ss, 0, sizeof(new_client->local_sockaddr.ss));
	get_time(&new_client->lasttime);

	if (x_debug & DEBUG_DCC_XMIT)
		yell("DCC %p created", new_client);
	ClientList = new_client;
	return new_client;
}

/*
 * dcc_searchlist searches through the dcc_list and finds the client
 * with the the flag described in type set.  This function should never
 * return a delete'd entry.
 */
static	DCC_list *dcc_searchlist (
	unsigned	type,		/* What kind of connection we want */
	const char *	user, 		/* Nick of the remote peer */
	const char *	description,	/* 
					 * DCC Type specific information,
					 * Usually a full pathname for 
					 * SEND/GET, "listen" or "connect"
					 * for RAW, or NULL for others.
					 */
	const char *	othername, 	/* Alias filename for SEND/GET */
	int 		active)		/* Only get active/non-active? */
{
	DCC_list 	*client;
	const char 	*last = NULL;
	char		*decoded_description;

	decoded_description = description ? dcc_urldecode(description) : NULL;

	if (x_debug & DEBUG_DCC_SEARCH)
		yell("entering dcc_sl.  desc (%s) decdesc (%s) user (%s) "
		     "type(%d) other (%s) active (%d)", 
			description, decoded_description, user, type,
			othername, active);

	/*
	 * Walk all of the DCC connections that we know about.
	 */
	lock_dcc(NULL);

	for (client = ClientList; client ; client = client->next)
	{
		/* Never return deleted entries */
		if (client->flags & DCC_DELETE)
			continue;

		/*
		 * Tell the user if they care
		 */
		if (x_debug & DEBUG_DCC_SEARCH)
		{
			yell("checking against  name (%s) user (%s) type (%d) "
					"flag (%d) other (%s) active (%x)", 
				client->description, 
				client->user, 
				client->flags & DCC_TYPES,
				client->flags, 
				client->othername, 
				client->flags & DCC_ACTIVE);
		}

		/*
		 * Ok. first of all, it has to be the right type.
		 * XXX - Doing (unsigned) -1 is a hack.
		 */
		if (type != (unsigned)-1 && 
				((client->flags & DCC_TYPES) != type))
			continue;

		/*
		 * Its OK if the user matches the client's user
		 */
		if (user && my_stricmp(user, client->user))
			continue;


		/*
		 * If "name" is NULL, then that acts as a wildcard.
		 * If "description" is NULL, then that also acts as a wildcard.
		 * If "name" is not the same as "description", then it could
		 * 	be that "description" is a filename.  Check to see if
		 *	"name" is the last component in "description" and
		 *	accept that.
		 * Otherwise, "othername" must exist and be the same.
		 * In all other cases, reject this entry.
		 */
		
		if (description && client->description && 
			my_stricmp(description, client->description) &&
			my_stricmp(decoded_description, client->description))
		{
			/*
			 * OK.  well, 'name' isnt 'description', try looking
			 * for a last segment.  If there isnt one, choke.
			 */
			if ((last = strrchr(client->description, '/')) == NULL)
				continue;

			/*
			 * If 'name' isnt 'last' then we'll have to look at
			 * 'othername' to see if that matches.
			 */
			if (my_stricmp(description, last + 1) && my_stricmp(decoded_description, last + 1))
			{
				if (!othername || !client->othername)
					continue;

				if (my_stricmp(othername, client->othername))
					continue;
			}
		}

		/*
		 * Active == 0  -> Only PENDING unopened connections, please
		 * No deleted entries, either.
		 */
		if (active == 0)
		{
			if (client->flags & DCC_ACTIVE)
				continue;
		}
		/*
		 * Active == 1 -> Only ACTIVE and OPEN connections, please
		 */
		else if (active == 1)
		{
			if ((client->flags & DCC_ACTIVE) == 0)
				continue;
		}
		/*
		 * Active == -1 -> Only NON DELETED entries, please.
		 */
		else if (active == -1)
			(void) 0;

		if (x_debug & DEBUG_DCC_SEARCH)
			yell("We have a winner!");

		/* Looks like we have a winner! */
		unlock_dcc(NULL);
		return client;
	}
	new_free(&decoded_description);
	unlock_dcc(NULL);

	return NULL;
}


/*
 * dcc_get_bucket searches through the dcc_list and collects the clients
 * we can do dcc get on.  This function should never return a delete'd entry.
 */
static	int	dcc_get_bucket (Bucket *b, const char *user, const char *fname)
{
	DCC_list 	*client;
	const char 	*last = NULL;
	char		*decoded_description;
	int		count = 0;

	decoded_description = fname ? dcc_urldecode(fname) : NULL;

	if (x_debug & DEBUG_DCC_SEARCH) 
		yell("entering dcc_g_b.  desc (%s) decdesc (%s) user (%s) ",
			fname, decoded_description, user);

	/*
	 * Walk all of the DCC connections that we know about.
	 */
	lock_dcc(NULL);

	for (client = ClientList; client ; client = client->next)
	{
		/* Skip deleted entries */
		if (client->flags & DCC_DELETE)
			continue;

		/* Skip already ACTIVE connections */
		if (client->flags & DCC_ACTIVE)
			continue;

		/* Skip non-DCC GETs */
		if ((client->flags & DCC_TYPES) != DCC_FILEREAD)
			continue;

		/* Skip DCC GETs with no filename (!!!) */
		if (!client->description)
			continue;

		/* Skip DCCs from other people */
		if (user && my_stricmp(user, client->user))
			continue;

		last = strrchr(client->description, '/');

		/*
		 * If "fname" is NULL, then that also acts as a wildcard.
		 * If "fname" is not the same as "description", then it could
		 * 	be that "description" is a filename.  Check to see if
		 *	"fname" is the last component in "description" and
		 *	accept that.
		 * In all other cases, reject this entry.
		 */
#define ACCEPT { if (b) 					\
			add_to_bucket(b, empty_string, client); \
		 count++; 					\
		 continue; }
#define CHECKVAL(x) 						\
	if (! x ) 						\
		ACCEPT						\
	else if (!my_stricmp( x , client->description))		\
		ACCEPT						\
	else if (last && !my_stricmp( x , last + 1))		\
		ACCEPT

		CHECKVAL(fname)
		CHECKVAL(decoded_description)
	}

	new_free(&decoded_description);
	unlock_dcc(NULL);
	return count;
}

/*
 * Added by Chaos: Is used in edit.c for checking redirect.
 */
int	dcc_chat_active (const char *user)
{
	int	retval;
	int	l;

	l = message_from(user, LEVEL_DCC);
	retval = dcc_searchlist(DCC_CHAT, user, NULL, NULL, 1) ? 1 : 0;
	pop_message_from(l);
	return retval;
}


/************************************************************************/
static	int	dcc_get_connect_addrs (DCC_list *dcc)
{
	ssize_t	c;
	const char *	type;
	int	retval;

	type = dcc_types[dcc->flags & DCC_TYPES];

#define DGETS(x, y) dgets( x , (char *) & y , sizeof y , -1);

	/* * */
	/* This is the errno value from getsockopt() */
	c = DGETS(dcc->socket, retval)
	if (c < (ssize_t)sizeof(retval) || retval)
		goto something_broke;

	/* This is the socket error returned by getsockopt() */
	c = DGETS(dcc->socket, retval)
	if (c < (ssize_t)sizeof(retval) || retval)
		goto something_broke;

	/* * */
	/* This is the errno value from getsockname() */
	c = DGETS(dcc->socket, retval)
	if (c < (ssize_t)sizeof(retval) || retval)
		goto something_broke;

	/* This is the address returned by getsockname() */
	c = DGETS(dcc->socket, dcc->local_sockaddr.ss)
	if (c < (ssize_t)sizeof(dcc->local_sockaddr.ss))
		goto something_broke;

	/* * */
	/* This is the errno value from getpeername() */
	c = DGETS(dcc->socket, retval)
	if (c < (ssize_t)sizeof(retval) || retval)
		goto something_broke;

	/* This is the address returned by getpeername() */
	c = DGETS(dcc->socket, dcc->peer_sockaddr.ss)
	if (c < (ssize_t)sizeof(dcc->peer_sockaddr.ss))
		goto something_broke;

	return 0;

something_broke:
	say("DCC %s connection with %s could not be established: %s",
		type, dcc->user, 
		retval ? strerror(retval) : "(internal error)");
	dcc->flags |= DCC_DELETE;
	return -1;
}

/*
 * This is called when a client connection completes (successfully).
 * If the connection failed, we will forcibly eject the dcc.
 */
static int	dcc_connected (int fd)
{
	DCC_list *	dcc;
	const char *	type;
	int		jvs_blah;

	if (!(dcc = get_dcc_by_filedesc(fd)))
		return -1;	/* Don't want it */

	type = dcc_types[dcc->flags & DCC_TYPES];

	/*
	 * Set up the connection to be useful
	 */
	new_open(dcc->socket, do_dcc, NEWIO_RECV, 1, dcc->server);
	dcc->flags &= ~DCC_THEIR_OFFER;
	dcc->flags |= DCC_ACTIVE;

	/*
	 * If this was a two-peer connection, then tell the user
	 * that the connection was successfull.
	 */
	lock_dcc(dcc);
	if ((dcc->flags & DCC_TYPES) != DCC_RAW && 
	    (dcc->flags & DCC_TYPES) != DCC_RAW_LISTEN)
	{
		char p_addr[256];
		char p_port[24];
		char *encoded_description;

		if (inet_ntostr(&dcc->peer_sockaddr, p_addr, 256, p_port, 24, NI_NUMERICHOST))
			yell("Couldn't get host/port for this connection.");

		encoded_description = dcc_urlencode(dcc->description);

		/* 
		 * It would be nice if SEND also showed the filename
		 * and size (since it's possible to have multiple
		 * SEND requests queued), so we check for a file size
		 * first. 
		 * Actually, that was wrong.  We just check the type.
		 * so that it doesnt choke on zero-length files.
		 */
		if (!strcmp(type, "SEND"))
		{
		    if ((jvs_blah = do_hook(DCC_CONNECT_LIST,
					"%s %s %s %s %s " INTMAX_FORMAT,
					dcc->user, type, p_addr, p_port,
					encoded_description,
					dcc->filesize)))
			    /*
			     * Compatability with bitchx
			     */
			jvs_blah = do_hook(DCC_CONNECT_LIST,
					"%s GET %s %s %s " INTMAX_FORMAT, 
					dcc->user, p_addr, p_port,
					encoded_description,
					dcc->filesize);
		}
		else
		{
		    jvs_blah = do_hook(DCC_CONNECT_LIST,
				"%s %s %s %s", 
				dcc->user, type, p_addr, p_port);
		}

		if (jvs_blah)
		{
		    say("DCC %s connection with %s[%s:%s] established",
				type, dcc->user, p_addr, p_port);
		}
		new_free(&encoded_description);
	}
	unlock_dcc(dcc);

	/*
	 * Record the time the connection was started, 
	 * Clean up and then return.
	 */
	get_time(&dcc->starttime);
	get_time(&dcc->lasttime);
	if (dcc->open_callback)
		dcc->open_callback(dcc);
	return 0;		/* Going to keep it. */
}

static int	do_expire_dcc_connects (void *stuff)
{
	int	old_server = from_server;
	int	seconds;
	DCC_list *dcc;
	Timeval	right_now;
	int	l;

	/*
	 * Initialize our idea of what is going on.
	 */
	if (from_server == NOSERV)
		from_server = get_window_server(0);

	get_time(&right_now);
	if ((seconds = get_int_var(DCC_CONNECT_TIMEOUT_VAR)) == 0)
		return 0;		/* Do not time out if == 0 */

	lock_dcc(NULL);
	for (dcc = ClientList ; dcc != NULL ; dcc = dcc->next)
	{
	    if (!(dcc->flags & DCC_CONNECTING))
		continue;

	    if (time_diff(dcc->lasttime, right_now) >= seconds)
	    {
                unsigned my_type = dcc->flags & DCC_TYPES;
                char *encoded_description;

                encoded_description = dcc_urlencode(dcc->description);
		l = message_from(dcc->user, LEVEL_DCC);
		if (do_hook(DCC_LOST_LIST,"%s %s %s CONNECT TIMED OUT",
		        dcc->user,
		        dcc_types[my_type],
			encoded_description ? encoded_description : "<any>"))
                    say("DCC %s:%s to %s -- connection timed out",
                        dcc_types[my_type],
                        dcc->description ? dcc->description : "<any>",
                        dcc->user);
		dcc->flags |= DCC_DELETE;
		pop_message_from(l);
	    }
	}
	unlock_dcc(NULL);

	dcc_garbage_collect();
	from_server = old_server;
	return 0;
}

/*
 * Whenever a DCC changes state from WAITING->ACTIVE, it calls this function
 * to initiate the internet connection for the transaction.
 */
static	int	dcc_connect (DCC_list *dcc)
{
	int	old_server = from_server;
	int	retval = 0;
	SSu	local;
	socklen_t	locallen;
	int	seconds;

	/*
	 * Initialize our idea of what is going on.
	 */
	if (from_server == NOSERV)
		from_server = get_window_server(0);

	lock_dcc(dcc);
    do
    {
	if (!(dcc->flags & DCC_THEIR_OFFER))
	{
		say("Can't connect on a dcc that was not offered [%s]", dcc->user);
		dcc->flags |= DCC_DELETE;
		retval = -1;
		break;
	}

	/* inet_vhostsockaddr doesn't usually return an error */
	if (inet_vhostsockaddr(family(&dcc->offer), -1, NULL, &local, &locallen) < 0)
	{
		say("Can't figure out your virtual host.  "
		    "Use /HOSTNAME to reset it and try again.");
		retval = -1;
		break;
	}

	dcc->socket = client_connect(&local, locallen, &dcc->offer, sizeof(dcc->offer.ss));

	if (dcc->socket < 0)
	{
		char *encoded_description = dcc_urlencode(dcc->description);

		/* XXX Error message may need to be tuned here. */
		if (do_hook(DCC_LOST_LIST,"%s %s %s ERROR",
				dcc->user,
				dcc_types[dcc->flags&DCC_TYPES],
				encoded_description ? 
					encoded_description : 
					"<any>"))
			say("Unable to create connection: (%d)", dcc->socket);

		if (encoded_description)
			new_free(&encoded_description);

		dcc->flags |= DCC_DELETE;
		retval = -1;
		break;
	}

	dcc->flags |= DCC_CONNECTING;
	new_open(dcc->socket, do_dcc, NEWIO_CONNECT, 0, dcc->server);

	if ((seconds = get_int_var(DCC_CONNECT_TIMEOUT_VAR)) > 0)
	{
/*
		say("A non-blocking connect() for your DCC has been initiated."
		    "  It could take a while to complete."
		    "  I'll check on it in %d seconds", seconds);
*/
		add_timer(0, empty_string, seconds, 1,
			  do_expire_dcc_connects, NULL, NULL,
			  GENERAL_TIMER, -1, 0, 0);
	}
	from_server = old_server;
	get_time(&dcc->lasttime);
	break;
    }
    while (0);

	unlock_dcc(dcc);
	from_server = old_server;
	return retval;
}


/*
 * Make an offer to another peer that they can't refuse.
 */
static	int	dcc_listen (DCC_list *dcc)
{
	int	old_server = from_server;
	char	p_port[12];
	int	retval = 0;

	/*
	 * Initialize our idea of what is going on.
	 */
	if (from_server == NOSERV)
		from_server = get_window_server(0);

    do
    {
	if (dcc->flags & DCC_THEIR_OFFER)
	{
		dcc->flags |= DCC_DELETE;
		say("Mixup: dcc_offer on a remote offer [%d]", dcc->socket);
		retval = -1;
		break;
	}

	/*
	 * Mark that we're waiting for the remote peer to answer,
	 * and then open up a listen()ing socket for them.  If our
	 * first choice of port fails, try another one.  If both
	 * fail, then we give up.  If the user insists on doing
	 * random ports, then we will fallback to asking the system
	 * for a port if our random port isnt available.
	 */
	dcc->flags |= DCC_MY_OFFER;

	while ((dcc->socket = ip_bindery(dcc->family, dcc->want_port, 
				      &dcc->local_sockaddr)) < 0)
	{
	    /* XXX Maybe this shouldn't be done for $listen()s. */
	    char *encoded_description = NULL;
	    int	  original_port = dcc->want_port;

	    fill_in_default_port(dcc);

	    encoded_description = dcc_urlencode(dcc->description);
            do_hook(DCC_LOST_LIST,"%s %s %s %ld %d PORT IN USE",
			dcc->user, dcc_types[dcc->flags & DCC_TYPES],
			encoded_description, dcc->refnum, original_port);
	    new_free(&encoded_description);

	    if (dcc->want_port == original_port)
	    {
		dcc->flags |= DCC_DELETE;
		say("Unable to create connection on fd [%d] "
		    "binding local port [%d] for inbound connection.", 
			dcc->socket, dcc->want_port);
		retval = -1;
		goto dcc_listen_bail;
	    }
	}

#ifdef MIRC_BROKEN_DCC_RESUME
	/*
	 * For stupid MIRC dcc resumes, we need to stash the
	 * local port number, because the remote client will send
	 * back that port number as its ID of what file it wants
	 * to resume (rather than the filename. ick.)
	 */
	inet_ntostr(&dcc->local_sockaddr, NULL, 0, p_port, 12, 0);
	malloc_strcpy(&dcc->othername, p_port);
#endif
	new_open(dcc->socket, do_dcc, NEWIO_ACCEPT, 1, dcc->server);

	/*
	 * If this is to be a 2-peer connection, then we need to
	 * send the remote peer a CTCP request.  I suppose we should
	 * do an ISON request first, but thats another project.
	 */
	if (dcc->flags & DCC_TWOCLIENTS)
		dcc_send_booster_ctcp(dcc);
	get_time(&dcc->lasttime);
    }
    while (0);

dcc_listen_bail:
	from_server = old_server;
	return retval;
}


/*
 * send_booster_ctcp: This is called by dcc_open and also by dcc_filesend
 * to send a CTCP handshake message to a remote peer.  The reason its a 
 * function is because its a rather large chunk of code, and it needs to be
 * done basically identically by both places.  Whatever.
 *
 * XXX This function is not really protocol independant.
 */
static void	dcc_send_booster_ctcp (DCC_list *dcc)
{
	SSu	my_sockaddr;
	char	p_host[128];
	char	p_port[24];
	char *	nopath;
	const char *	type = dcc_types[dcc->flags & DCC_TYPES];
	int	family_;
	int	server = from_server < 0 ? primary_server : from_server;

	if (!is_server_registered(server))
	{
		yell("You cannot use DCC while not connected to a server.");
		return;
	}

	family_ = family(&dcc->local_sockaddr);

	/*
	 * Use the external gateway address (visible to the server) if the
	 * user wants us to use that.  If the user is NOT using a vhost,
	 * then use the address we have for ourselves connecting to the
	 * server.  If the user IS using a vhost, then obviously use that.
	 */
	if (get_int_var(DCC_USE_GATEWAY_ADDR_VAR))
		my_sockaddr = get_server_uh_addr(server);
	else if (family_ == AF_INET && dcc->local_sockaddr.si.sin_addr.s_addr == htonl(INADDR_ANY))
		my_sockaddr = get_server_local_addr(server);
	else if (family_ == AF_INET6 && memcmp(&dcc->local_sockaddr.si6.sin6_addr, 
						&in6addr_any, 
						sizeof(in6addr_any)) == 0)
		my_sockaddr = get_server_local_addr(server);
	else
		my_sockaddr = dcc->local_sockaddr;

	/*
	 * If the family the user asked for is not the family of the address
	 * we want to put in the handshake, something is very wrong.  Tell
	 * the user about it and give up.
	 */
	if (family_ != family(&my_sockaddr))
	{
	    if (get_int_var(DCC_USE_GATEWAY_ADDR_VAR))
		yell("When using /SET DCC_USE_GATEWAY_ADDR ON, I can only "
		     "support DCC in the same address family (IPv4 or IPv6) "
		     "as you are using to connect to the server.");
	    else if (family_ == AF_INET)
		yell("I do not know what your IPv4 address is.  You can tell "
		     "me your IPv4 hostname with /HOSTNAME and retry the /DCC");
	    else if (family_ == AF_INET6)
		yell("I do not know what your IPv6 address is.  You can tell "
		     "me your IPv6 hostname with /HOSTNAME and retry the /DCC");
	    else
		yell("I do not know what your address is because you asked "
		     "me for an address family that I don't support.");

	    dcc->flags |= DCC_DELETE;
	    return;
	}

	if (family_ == AF_INET)
		my_sockaddr.si.sin_port = dcc->local_sockaddr.si.sin_port;
	else if (family_ == AF_INET6)
		my_sockaddr.si6.sin6_port = dcc->local_sockaddr.si6.sin6_port;
	else
	{
		yell("Could not send a CTCP handshake becuase the address "
		     "family is not supported.");
		dcc->flags |= DCC_DELETE;
		return;
	}

	if (inet_ntostr(&my_sockaddr, p_host, 128, p_port, 24, 
					GNI_INTEGER | NI_NUMERICHOST)) 
	{
		yell("dcc_send_booster_ctcp: I couldn't figure out your hostname (or the port you tried to use was invalid).  I have to delete this DCC");
		if (get_int_var(DCC_USE_GATEWAY_ADDR_VAR))
			yell("dcc_send_booster_ctcp: You have /SET DCC_USE_GATEWAY_ADDR ON, which uses the hostname the irc server shows to the network.  If you're using a fake hostname, that would cause this problem.  Use /SET DCC_USE_GATEWAY_ADDR OFF if you have a fake hostname on irc (but that still won't work behind a NAT router");
		dcc->flags |= DCC_DELETE;
		return;
	}

	/*
	 * If this is to be a 2-peer connection, then we need to
	 * send the remote peer a CTCP request.  I suppose we should
	 * do an ISON request first, but thats another project.
	 */
	if (!(dcc->flags & DCC_TWOCLIENTS))
		return;

	get_time(&dcc->starttime);
	get_time(&dcc->lasttime);

	if (((dcc->flags & DCC_TYPES) == DCC_FILEOFFER) && 
		  (nopath = strrchr(dcc->description, '/')))
		nopath++;
	else
		nopath = dcc->description;

	/*
	 * If this is a DCC SEND...
	 */
	lock_dcc(dcc);
	if ((dcc->flags & DCC_TYPES) == DCC_FILEOFFER)
	{
		char *	url_name = dcc_urlencode(nopath);

		/*
		 * Dont bother with the checksum.
		 */
		send_ctcp(1, dcc->user, "DCC", "%s %s %s %s "INTMAX_FORMAT,
			 type, url_name, p_host, p_port,
			 dcc->filesize);

		/*
		 * Tell the user we sent out the request
		 */
		if (do_hook(DCC_OFFER_LIST, "%s %s %s "INTMAX_FORMAT, 
			dcc->user, type, url_name, dcc->filesize))
		    say("Sent DCC %s request (%s "INTMAX_FORMAT") to %s", 
			type, nopath, dcc->filesize, dcc->user);

		new_free(&url_name);
	}

	/*
	 * Otherwise its a DCC CHAT request
	 */
	else
	{
		/*
		 * Send out the handshake
		 */
		send_ctcp(1, dcc->user, "DCC", "%s %s %s %s", 
			 type, nopath, p_host, p_port);

		/*
		 * And tell the user
		 */
		if (do_hook(DCC_OFFER_LIST, "%s %s", dcc->user, type))
		    say("Sent DCC %s request to %s", type, dcc->user);
	}
	unlock_dcc(dcc);
}


/************************************************************************/
/*
 * This allows you to send (via /msg or /query) a message to a remote 
 * dcc target.  The two types of targets you may send to are a DCC CHAT
 * connection (you specify a nickname) or a DCC RAW connection (you specify
 * the file descriptor).  Of course, since DCC RAW connections use digits
 * and DCC CHATs dont, sending a message to =<digits> then is presumed to
 * be a message to a DCC RAW.
 *
 * If ``noisy'' is 1, then we tell the user that we send the message, and
 * allow them to hook it.  If ``noisy'' is 0, then we're doing this silently
 * (as in /redirect, or /ctcp) and we dont want to tell the user about it.
 *
 * XXXX This needs to be broken up into CHAT and RAW cases.
 */
static void	dcc_message_transmit (
	int 		type, 		/* What type of DCC to send over */
	char 		*user, 		/* Who to send it to */
	char		*desc,		/* THe 'desc' field in DCC_List */
	char 		*text, 		/* What to send them */
const	char 		*text_display, 	/* What to tell the user we sent */
	int 		noisy, 		/* Do we tell the user? */
	const char 	*cmd)		/* 
					 * For /CTCP's, is this a PRIVMSG
					 * or a NOTICE?
					 */
{
	DCC_list	*dcc;
	char		tmp[DCC_BLOCK_SIZE+1];
	char		thing = '\0';
	int		list = 0;
	int		old_from_server = from_server;
	int		writeval;
	char 		*target = NULL;
	const char 	*utf8_text = NULL;
	char 		*extra = NULL;

	tmp[0] = 0;

	switch (type)
	{
		case DCC_CHAT:
		{
			thing = '=';
			list = SEND_DCC_CHAT_LIST;
			break;
		}
		case DCC_RAW:
		{
			noisy = 0;
			break;
		}
	}

	if (!(dcc = dcc_searchlist(type, user, desc, NULL, -1)))
	{
		say("No active DCC %s:%s connection for %s",
			dcc_types[type], desc, user);
		return;
	}


	/*
	 * Check for CTCPs... whee.
	 * Dont tag outbound ACTIONs, which break mirc.  Blah.
	 */
	if (type == DCC_CHAT && *text == CTCP_DELIM_CHAR
			&& strncmp(text + 1, "ACTION", 6))
	{
		if (!cmd || !strcmp(cmd, "PRIVMSG"))
			strlcpy(tmp, "CTCP_MESSAGE ", sizeof tmp);
		else
			strlcpy(tmp, "CTCP_REPLY ", sizeof tmp);
	}

	if (x_debug & DEBUG_OUTBOUND) 
		yell("-> [%s] [%s]", desc, text);

	if (dcc->flags & DCC_QUOTED)
	{
		char *	dest;
		size_t	destlen;

		if (!(dest = transform_string_dyn("-CTCP", text, 0, &destlen)))
		{
			yell("DMSG: Could not CTCP-dequote [%s]", text);
			dest = malloc_strdup(text);
		}
		writeval = write(dcc->socket, dest, destlen);
		new_free(&dest);
	}
	else
	{
		strlcat(tmp, text, sizeof tmp);
		strlcat(tmp, "\n", sizeof tmp);

		writeval = write(dcc->socket, tmp, strlen(tmp));
	}


	if (writeval == -1)
	{
		dcc->flags |= DCC_DELETE;
		say("Outbound write() failed: %s", strerror(errno));

		from_server = old_from_server;
		return;
	}

	dcc->bytes_sent += writeval;

	if (noisy)
	{
		lock_dcc(dcc);
		if (do_hook(list, "%s %s", dcc->user, text_display))
			put_it("=> %c%s%c %s", 
				thing, dcc->user, thing, text_display);
		unlock_dcc(dcc);
	}

	get_time(&dcc->lasttime);
	return;
}

/*
 * This is used to send a message to a remote DCC peer.  This is called
 * by send_text.
 */
void	dcc_chat_transmit (char *user, char *text, const char *orig, const char *type, int noisy)
{
	int	fd;
	int	l = -1;
	char *	target = NULL;

    do
    {
	/*
	 * This converts a message being sent to a number into whatever
	 * its local port is (which is what we think the nickname is).
	 * Its just a 15 minute hack. dont read too much into it.
	 */
	if (is_number(user) && (fd = atol(user)))
	{
		DCC_list *	dcc;
		if (!(dcc = get_dcc_by_filedesc(fd)))
		{
			l = message_from(NULL, LEVEL_DCC);
			put_it("Descriptor %d is not an open DCC RAW", fd);
			break;
		}

		target = dcc_target(dcc->user);
		l = message_from(target, LEVEL_DCC);
		dcc_message_transmit(DCC_RAW, dcc->user, dcc->description, 
					text, orig, noisy, type);
		get_time(&dcc->lasttime);
	}
	else
	{
		target = dcc_target(user);
		l = message_from(target, LEVEL_DCC);
		dcc_message_transmit(DCC_CHAT, user, NULL,
					text, orig, noisy, type);
	}
    }
    while (0);

	dcc_garbage_collect();
	pop_message_from(l);
	new_free(&target);
}




/*
 *
 * All these functions are user-generated -- that is, they are all called
 * when the user does /DCC <command> or one of the DCC-oriented built in
 * functions.
 *
 */


/*
 * The /DCC command.  Delegates the work to other functions.
 */
BUILT_IN_COMMAND(dcc_cmd)
{
	const char	*cmd;
	int	i, l;

	if (!(cmd = next_arg(args, &args)))
		cmd = "LIST";

	for (i = 0; dcc_commands[i].name != NULL; i++)
	{
		if (!my_stricmp(dcc_commands[i].name, cmd))
		{
			l = message_from(NULL, LEVEL_DCC);
			lock_dcc(NULL);
			dcc_commands[i].function(args);
			unlock_dcc(NULL);
			pop_message_from(l);

			dcc_garbage_collect();
			return;
		}
	}

	l = message_from(NULL, LEVEL_DCC);
	say("Unknown DCC command: %s", cmd);
	pop_message_from(l);
}


/*
 * Usage: /DCC CHAT <nick> [-p port]|[-6]|[-4]
 */
static void dcc_chat (char *args)
{
	int	argc;
	char *	argv[10];

	argc = split_args(args, argv, 10);
	dcc_chat_subcmd(argc, argv, NULL);
}

DCC_SUBCOMMAND(dcc_chat_subcmd)
{
	char		*user = NULL;
	DCC_list	*dcc;
	unsigned short	portnum = 0;		/* Any port by default */
	int		family = AF_INET;	/* IPv4 by default */
	int		i;

	if (argc == 0)
	{
		say("Usage: /DCC CHAT <nick> [-p port]|[-6]|[-4]");
		return;
	}

	for (i = 0; i < argc; i++)
	{
	    if (!strcmp(argv[i], "-4"))
		family = AF_INET;
	    else if (!strcmp(argv[i], "-6"))
		family = AF_INET6;
	    else if (!strcmp(argv[i], "-p"))
	    {
		if (i + 1 == argc)
		    say("DCC CHAT: Argument to -p missing -- ignored");
		else if (!is_number(argv[i + 1]))
		    say("DCC CHAT: Argument to -p non-numeric -- ignored");
		else
		{
		    portnum = my_atol(argv[i + 1]);
		    i++;
		}
	    }
	    else if (*argv[i] == '-')
		say("DCC CHAT: Option %s not supported", argv[i]);
	    else if (user)
	    {
		say("DCC CHAT: Opening multiple chats per command not "
			"supported yet -- ignoring extra nick %s", argv[i]);
	    }
	    else
		malloc_strcpy(&user, argv[i]);
	}

	if ((dcc = dcc_searchlist(DCC_CHAT, user, NULL, NULL, -1)))
	{
		if ((dcc->flags & DCC_ACTIVE) || (dcc->flags & DCC_MY_OFFER))
		{
			say("Sending a booster CTCP handshake for "
				"an existing DCC CHAT to %s", user);
			dcc_send_booster_ctcp(dcc);
			return;
		}
	}
	else
		dcc = dcc_create(DCC_CHAT, user, "chat", NULL, family, 0);

	dcc->flags |= DCC_TWOCLIENTS;
	dcc->want_port = portnum;
	fill_in_default_port(dcc);

	if (dcc->flags & DCC_THEIR_OFFER)
		dcc_connect(dcc);
	else
		dcc_listen(dcc);	
}

/*
 * Usage: /DCC CLOSE <type> <nick> [<file>]
 */
static void dcc_close (char *args)
{
	int	argc;
	char *	argv[10];

	argc = split_args(args, argv, 10);
	dcc_close_subcmd(argc, argv, NULL);
}

DCC_SUBCOMMAND(dcc_close_subcmd)
{
	DCC_list	*dcc;
	int	type;
	char	*user = NULL, 
		*file = NULL;
	int	count = 0;

	if (argc < 2)
	{
		say("Usage: /DCC CLOSE <type> <nick> [<file>]");
		return;
	}

	if ((!my_stricmp(argv[0], "-all") || !my_stricmp(argv[0], "*")))
		type = -1;
	else
	{
		int	i;

		for (i = 0; dcc_types[i]; i++)
			if (!my_stricmp(argv[0], dcc_types[i]))
				break;

		if (!dcc_types[i])
		{
			say("DCC CLOSE: Unknown DCC type: %s", argv[0]);
			return;
		}

		type = i;
	}

	if ((!my_stricmp(argv[1], "-all") || !my_stricmp(argv[1], "*")))
		user = NULL;
	else
		user = argv[1];

	if (argc >= 3)
	    file = argv[2];

	while ((dcc = dcc_searchlist(type, user, file, file, -1)))
	{
		char *		encoded_description = NULL;
		unsigned	my_type = dcc->flags & DCC_TYPES;

		count++;
		lock_dcc(dcc);
		
		if (dcc->description) {
		    if (my_type == DCC_FILEOFFER)
			encoded_description = dcc_urlencode(dcc->description);
		    else
			/* assume the other end encoded the filename */
			encoded_description = malloc_strdup(dcc->description);
		}
		
                if (do_hook(DCC_LOST_LIST,"%s %s %s USER ABORTED CONNECTION",
			dcc->user, 
			dcc_types[my_type],
                        encoded_description ? encoded_description : "<any>"))
		    say("DCC %s:%s to %s closed", 
			dcc_types[my_type],
			file ? file : "<any>", 
			dcc->user);
		
		if (encoded_description)
			new_free(&encoded_description);
			
		dcc->flags |= DCC_DELETE;
		unlock_dcc(dcc);
	}

	if (!count)
		say("No DCC %s:%s to %s found", 
			(type == -1 ? "<any>" : dcc_types[type]), 
			(file ? file : "<any>"), 
			user);
}

/*
 * Usage: /DCC CLOSEALL
 * It leaves your DCC list very empty
 */
static void dcc_closeall (char *args)
{
	/* Just a dummy placeholder */
	dcc_closeall_subcmd(0, NULL, NULL);
}

DCC_SUBCOMMAND(dcc_closeall_subcmd)
{
	char *	my_argv[3];

	my_argv[0] = LOCAL_COPY("-all");
	my_argv[1] = LOCAL_COPY("-all");
	dcc_close_subcmd(2, my_argv, NULL);
}

/*
 * Usage: /DCC GET <nick> [file|*]
 * The '*' file gets all offered files.
 */
static void dcc_get (char *args)
{
	int	argc;
	char *	argv[10];

	argv[0] = LOCAL_COPY("GET");
	argc = split_args(args, &argv[1], 9);
	dcc_get_subcmd(argc + 1, argv, NULL);
}

#ifdef MIRC_BROKEN_DCC_RESUME
/*
 * Usage: /DCC RESUME <nick> [file|*]
 * The '*' file gets all offered files.
 */
static void dcc_resume (char *args)
{
	int	argc;
	char *	argv[10];

	argv[0] = LOCAL_COPY("RESUME");
	argc = split_args(args, &argv[1], 9);
	dcc_get_subcmd(argc + 1, argv, NULL);
}
#endif

static void	handle_invalid_savedir (const char *pathname)
{
	say("DCC GET: Can't save file because %s is not a valid directory.",
					pathname);
	say("DCC GET: Check /SET DCC_STORE_PATH and try again.");
}

DCC_SUBCOMMAND(dcc_get_subcmd)
{
	char		*user;
	char		*filename = NULL;
	DCC_list	*dcc;
	Filename	default_savedir = "";
	Filename	fullname = "";
	Filename	pathname = "";
	int		savedir_is_invalid = 0;
	int		file;
	char 		*realfilename = NULL;
	int		count = 0, i, j;
	Stat		sb;
	int		proto;
	const char *	x = NULL;
	int		resume;
	Bucket *	b;

	if (argc < 2)
	{
		say("You must supply a nickname for DCC GET");
		return;
	}

	/* Figure out if this is DCC GET or DCC RESUME */
	if (!strcmp(argv[0], "RESUME"))
		resume = 1;
	else
		resume = 0;

	/* Figure out whose offer we will accept */
	user = argv[1];

	lock_dcc(NULL);

	/* Handle directory as the last argument */
	if (!normalize_filename(argv[argc-1], pathname) && isdir(pathname))
	{
		/* Pretend the user did /set dcc_store_path argv[argc-1] */
		x = pathname;
		argv[argc-1] = NULL;
		argc--;
	}

	/* Handle a straight rename */
	else if (argc == 4 && dcc_get_bucket(NULL, user, argv[2]) == 1 &&
			 dcc_get_bucket(NULL, user, argv[3]) == 0)
	{
		/* Pretend the user did /dcc rename get <user> argv[2] argv[3] */
		if (!(b = new_bucket()))
		{
			yell("DCC GET: new_bucket() failed. [1] help!");
			return;
		}

		dcc_get_bucket(b, user, argv[2]);
		dcc = b->list[0].stuff;
		malloc_strcpy(&dcc->description, argv[3]);
		free_bucket(&b);

		/* Pretend the user did /dcc get <user> argv[3] */
		argv[2] = argv[3];
		argc = 3;
	}

	/* Calculate "default_savedir" */
	if (!x)
		x = get_string_var(DCC_STORE_PATH_VAR);
	if (!x || !*x)
		x = "./";

	if (normalize_filename(x, default_savedir))
		savedir_is_invalid = 1;

	if (argc == 2)
	{
		filename = NULL;
		j = 4;
		goto jumpstart_get;
	}

	for (j = 2; j < argc; j++)
	{
		filename = argv[j];
jumpstart_get:
		if (!(b = new_bucket()))
		{
			yell("DCC GET: new_bucket() failed.  [2] Help!");
			return;
		}
		count = dcc_get_bucket(b, user, filename);
		for (i = 0; i < count; i++)
		{
			/* Skip any null pointers. sigh. */
			if (!(dcc = b->list[i].stuff))
				continue;

			lock_dcc(dcc);

			/* 
			 * Figure out where we will save this file.  If the user has
			 * /DCC RENAMEd this file to an absolute path, we honor that.
			 * Otherwise, we use the default directory from above 
			 * (09/24/2003)
			 */
			realfilename = dcc_urldecode(dcc->description);
			if (*realfilename == '/')
				strlcpy(fullname, realfilename, sizeof(fullname));
			else
			{
				if (savedir_is_invalid == 1)
				{
					handle_invalid_savedir(x);
					savedir_is_invalid++;
					new_free(&realfilename);
					unlock_dcc(dcc);
					continue;
				}

				strlcpy(fullname, default_savedir, sizeof(fullname));
				strlcat(fullname, "/", sizeof(fullname));
				strlcat(fullname, realfilename, sizeof(fullname));
			}

			new_free(&realfilename);
			dcc->local_filename = malloc_strdup(fullname);
			dcc->open_callback = NULL;

#ifdef MIRC_BROKEN_DCC_RESUME
			if (resume && get_int_var(MIRC_BROKEN_DCC_RESUME_VAR) && 
				stat(fullname, &sb) != -1) 
			{
				dcc->bytes_sent = 0;
				dcc->bytes_read = dcc->resume_size = sb.st_size;

				if ((file = open(fullname, O_WRONLY|O_APPEND, 0644)) == -1)
				{
					say("Unable to open %s: %s", fullname, strerror(errno));
					unlock_dcc(dcc);
					continue;
				}
				dcc->file = file;
				dcc->flags |= DCC_RESUME_REQ;
	
				if (family(&dcc->offer) == AF_INET)
					malloc_strcpy(&dcc->othername, 
						ltoa(ntohs(dcc->offer.si.sin_port)));
		
				if (x_debug & DEBUG_DCC_XMIT)
					yell("SENDING DCC RESUME to [%s] [%s|%s|%ld]", 
						user, filename, dcc->othername, 
						(long)sb.st_size);
		
				/* Just in case we have to fool the protocol enforcement. */
				proto = get_server_protocol_state(from_server);
				set_server_protocol_state(from_server, 0);
				send_ctcp(1, user, "DCC",
#if 1
					strchr(dcc->description, ' ')
						? "RESUME \"%s\" %s %ld"
						: "RESUME %s %s %ld",
					dcc->description,
#else
					/* This is for testing mirc compatability */
					"RESUME file.ext %s %ld",
#endif
					dcc->othername, (long)sb.st_size);
				set_server_protocol_state(from_server, proto);
			}
			else
#endif
			{
				if ((file = open(fullname, O_WRONLY|O_TRUNC|O_CREAT, 0644))==-1)
				{
					say("Unable to open %s: %s", fullname, strerror(errno));
					unlock_dcc(dcc);
					continue;
				}

				dcc->file = file;
				dcc->flags |= DCC_TWOCLIENTS;
				dcc_connect(dcc);	/* Nonblocking should be ok here */
				unlock_dcc(dcc);
			}
		}
	}
	unlock_dcc(NULL);

	if (!count)
	{
		if (filename)
			say("No file (%s) offered in SEND mode by %s", filename, user);
		else
			say("No file offered in SEND mode by %s", user);
	}
}

/*
 * Calculates transfer speed based on size, start time, and current time.
 */
static char *	calc_speed (intmax_t sofar, Timeval sta, Timeval cur)
{
	static char	buf[7];
	double		tdiff = time_diff(sta, cur);

	if (sofar == 0 || tdiff <= 0.0)
		snprintf(buf, sizeof(buf), "N/A");
	else
		snprintf(buf, sizeof(buf), "%4.1f", 
				((sofar / 1024.0) / tdiff));
	return buf;
}

/*
 * Packs a file size into a smaller representation of Kb, Mb, or Gb.
 * I'm sure it can be done less kludgy.
 */
static char *	calc_size (intmax_t fsize, char *retval, size_t retsize)
{
	if (fsize < 0)
		snprintf(retval, retsize, "!ERR!");
	else if (fsize < 1 << 10)
		snprintf(retval, retsize, INTMAX_FORMAT, fsize);
	else if (fsize < 1 << 20)
		snprintf(retval, retsize, "%3.1fKb", fsize / (double)(1 << 10));
	else if (fsize < 1 << 30)
		snprintf(retval, retsize, "%3.1fMb", fsize / (double)(1 << 20));
	else
		snprintf(retval, retsize, "%3.1fGb", fsize / (double)(1 << 30));

	return retval;
}

/*
 * Usage: /DCC LIST
 */
static void	dcc_list (char *args)
{
	DCC_list	*dcc;
	DCC_list	*next;
static	const char	*format =
		"%-7.7s%-3.3s %-9.9s %-9.9s %-20.20s %-6.6s %-5.5s %-6.6s %s";
	char		*encoded_description;
	int		l;

	l = message_from(NULL, LEVEL_OTHER);	/* Gulp */

	if (do_hook(DCC_LIST_LIST, "Start * * * * * * *"))
	{
		put_it(format, "Type", " ", "Nick", "Status", "Start time", 
				"Size", "Compl", "Kb/s", "Args");
	}

	lock_dcc(NULL);
	for (dcc = ClientList ; dcc != NULL ; dcc = next)
	{
	    unsigned	flags = dcc->flags;

	    next = dcc->next;
	    lock_dcc(dcc);

	    if ((flags & DCC_TYPES) == DCC_FILEOFFER)
		encoded_description = dcc_urlencode(dcc->description);
	    else
		/* assume the other end encoded the filename */
		encoded_description = malloc_strdup(dcc->description);

	    if (do_hook(DCC_LIST_LIST, "%s %s %s %s " INTMAX_FORMAT " "INTMAX_FORMAT
							" "INTMAX_FORMAT" %s",
				dcc_types[flags & DCC_TYPES],
				zero,			/* No encryption */
				dcc->user,
					flags & DCC_DELETE       ? "Closed"  :
					flags & DCC_ACTIVE       ? "Active"  :
					flags & DCC_MY_OFFER     ? "Waiting" :
					flags & DCC_THEIR_OFFER  ? "Offered" :
							           "Unknown",
				(intmax_t)dcc->starttime.tv_sec,
			        dcc->filesize,
				dcc->bytes_sent ? dcc->bytes_sent
						   : dcc->bytes_read,
				encoded_description))
	    {
		char *	filename = strrchr(dcc->description, '/');
		char	completed[9];
		char	size[9];
		char	speed[9];
		char	buf[23];
		const char *	time_f;
		intmax_t	tot_size;
		intmax_t	act_sent;

		/*
		 * Figure out something sane for the filename
		 */
		if (!filename || get_int_var(DCC_LONG_PATHNAMES_VAR))
			filename = dcc->description;
		else if (filename && *filename)
			filename++;

		if (!filename)
			filename = LOCAL_COPY(ltoa(get_pending_bytes(dcc->socket)));

		/*
		 * Figure out how many bytes we have sent for *this*
		 * session, and how many bytes of the file have been
		 * sent *in total*.
		 */
		if (dcc->bytes_sent)
		{
			tot_size = dcc->bytes_sent;
			act_sent = tot_size - dcc->resume_size;
		}
		else
		{
			tot_size = dcc->bytes_read;
			act_sent = tot_size - dcc->resume_size;
		}

		/*
		 * Figure out something sane for the "completed" and
		 * "size" fields.
		 */
		if (dcc->filesize)
		{
			double	prop;
			long	perc;

			prop = (double)tot_size / dcc->filesize;
			perc = prop * 100;

			snprintf(completed, sizeof completed, "%ld%%", perc);
			calc_size(dcc->filesize, size, sizeof(size));
		}
		else
		{
			calc_size(tot_size, completed, sizeof(completed));
			*size = 0;
		}


		/*
		 * Figure out something sane for starting time
		 */
		if (dcc->starttime.tv_sec)
		{
			time_t 	blech = dcc->starttime.tv_sec;
			struct tm *btime = localtime(&blech);

			strftime(buf, 22, "%T %b %d %Y", btime);
			time_f = buf;
		}
		else
			time_f = empty_string;

		/*
		 * Figure out something sane for the xfer speed.
		 */
		if (act_sent)
		{
			strlcpy(speed, calc_speed(act_sent, 
				dcc->starttime, get_time(NULL)), sizeof speed);
		}
		else
			*speed = 0;

		/*
		 * And do the dirty work.
		 */
		put_it(format,
			dcc_types[flags&DCC_TYPES],
			empty_string,			/* No encryption */
			dcc->user,
			flags & DCC_DELETE      ? "Closed" :
			flags & DCC_ACTIVE      ? "Active" : 
			flags & DCC_MY_OFFER    ? "Waiting" :
			flags & DCC_THEIR_OFFER ? "Offered" : 
						  "Unknown",
			time_f, size, completed, speed, filename);
	    }
	    if (encoded_description)
		    new_free(&encoded_description);
	    unlock_dcc(dcc);
	}
	unlock_dcc(NULL);
	do_hook(DCC_LIST_LIST, "End * * * * * * *");
	pop_message_from(l);
}


/*
 * Usage: /DCC RAW <filedesc> <host> [text]
 */
static	void	dcc_send_raw (char *args)
{
	char	*name;
	char	*host;
	int	l;

	if (!(name = next_arg(args, &args)))
	{
		say("No name specified for DCC RAW");
		return;
	}
	if (!(host = next_arg(args, &args)))
	{
		say("No hostname specified for DCC RAW");
		return;
	}

	l = message_from(name, LEVEL_DCC);
	dcc_message_transmit(DCC_RAW, name, host, args, args, 1, NULL);
	pop_message_from(l);
}

/*
 * Usage: /DCC RENAME [-CHAT] <nick> [<oldname>] <newname>
 */
static	void	dcc_rename (char *args)
{
	DCC_list	*dcc;
	const char	*user;
	const char	*oldf;
	const char	*newf;
	char	*temp;
	int	type = DCC_FILEREAD;
	
	if (!(user = next_arg(args, &args)) || !(temp = next_arg(args, &args)))
	{
		say("You must specify a nick and new filename for DCC RENAME");
		return;
	}

	if ((newf = next_arg(args, &args)) != NULL)
		oldf = temp;
	else
	{
		newf= temp;
		oldf = NULL;
	}

	if (!my_strnicmp(user, "-CHAT", strlen(user)))
	{
		if (!oldf || !newf)
		{
		    say("You must specify a new nickname for DCC RENAME -CHAT");
		    return;
		}
		user = oldf;
		oldf = "chat";
		type = DCC_CHAT;
	}

	if ((dcc = dcc_searchlist(type, user, oldf, NULL, -1)))
	{
		if (type == DCC_FILEREAD && (dcc->flags & DCC_ACTIVE))
		{
			say("Too late to rename that file");
			return;
		}

		if (type == DCC_FILEREAD)
		{
			say("File %s from %s renamed to %s",
				dcc->description ? dcc->description : "<any>",
				user, newf);
			malloc_strcpy(&dcc->description, newf);
		}
		else
		{
			if (is_channel(newf))
			{
				say("I can't permit you to DCC RENAME to "
					"something that looks like a channel, "
					"sorry.");
				return;
			}

			say("DCC CHAT from %s changed to new nick %s",
				user, newf);
			malloc_strcpy(&dcc->user, newf);
		}
	}
	else
		say("%s has not yet offered you the file %s",
			user, oldf ? oldf : "<any>");
}

/*
 * Usage: /DCC SEND <nick> <filename> [<filename>]
 */
static	void	dcc_filesend (char *args)
{
	char		*user,
			*this_arg;
	Filename	fullname;
	unsigned short	portnum = 0;
	int		filenames_parsed = 0;
	DCC_list	*Client;
	Stat		stat_buf;
	int		family;

	/*
	 * For sure, at least one argument is needed, the target
	 */
	if ((user = next_arg(args, &args)))
	{
	    family = AF_INET;

	    while (args && *args)
	    {
		this_arg = new_next_arg(args, &args);

		/*
		 * Check to see if its a flag
		 */
		if (*this_arg == '-')
		{
			if (this_arg[1] == 'p')
			{
				if (args && *args)
				    portnum = my_atol(next_arg(args, &args));
			}
			else if (this_arg[1] == '6')
				family = AF_INET6;
			else if (this_arg[1] == '4')
				family = AF_INET;

			continue;
		}

		/*
		 * Ok.  We have a filename they want to send.  Check to
		 * see what kind it is.
		 */
                if (normalize_filename(this_arg, fullname))
                {
                        say("%s is not a valid directory", fullname);
                        continue;
                }

		/*
		 * Make a note that we've seen a filename
		 */
		filenames_parsed++;

#ifdef I_DONT_TRUST_MY_USERS
		/*
		 * Dont allow the user to send a file that is in "/etc" or
		 * a file that ends in "/passwd".  Presumably this is for
		 * your safety.  If you can figure out how to get around this,
		 * then i guess you dont need this protection.
		 */
		if (!strncmp(fullname, "/etc/", 5) || 
				!end_strcmp(fullname, "/passwd", 7))
		{
			say("Send Request Rejected");
			continue;
		}
#endif

		if (access(fullname, R_OK))
		{
			say("Cannot send %s because you dont have read permission", fullname);
			continue;
		}

		if (isdir(fullname))
		{
			say("Cannot send %s because it is a directory", fullname);
			continue;
		}

		stat(fullname, &stat_buf);
		if ((Client = dcc_searchlist(DCC_FILEOFFER, user, fullname,
						this_arg, -1)))
		{
			if ((Client->flags & DCC_ACTIVE) ||
			    (Client->flags & DCC_MY_OFFER))
			{
				say("Sending a booster CTCP handshake for "
					"an existing DCC SEND:%s to %s", 
					fullname, user);
				dcc_send_booster_ctcp(Client);
				continue;
			}
		}
		else
			Client = dcc_create(DCC_FILEOFFER, user, fullname,
					this_arg, family, stat_buf.st_size);

		Client->flags |= DCC_TWOCLIENTS;
		Client->want_port = portnum;
		fill_in_default_port(Client);
		dcc_listen(Client);
	    } /* The WHILE */
	} /* The IF */

	if (!filenames_parsed)
		yell("Usage: /DCC SEND <[=]nick> <file> [<file> ...]");
}


/*
 * Usage: $listen(<port> <family>)
 */
char	*dcc_raw_listen (int family, unsigned short port)
{
	DCC_list *Client;
	const char *	  PortName = empty_string;
	int	l;

	lock_dcc(NULL);
	l = message_from(NULL, LEVEL_DCC);

    do
    {
	if (port && port < 1024)
	{
		say("May not bind to a privileged port");
		break;
	}
	PortName = LOCAL_COPY(ltoa(port));

	if ((Client = dcc_searchlist(DCC_RAW_LISTEN, PortName, NULL,
					NULL, -1)))
	{
		if ((Client->flags & DCC_ACTIVE) ||
		    (Client->flags & DCC_MY_OFFER))
		{
			say("A previous DCC RAW_LISTEN on %s exists", PortName);
			break;
		}
	}
	else
		Client = dcc_create(DCC_RAW_LISTEN, PortName, "raw_listen",
				NULL, family, 0);

	lock_dcc(Client);
	Client->want_port = port;
	if (dcc_listen(Client))		/* Not a connect(). */
	{
		unlock_dcc(Client);
		break;
	}

	get_time(&Client->starttime);
	Client->flags |= DCC_ACTIVE;
	Client->user = malloc_strdup(ltoa(Client->want_port));
	unlock_dcc(Client);
    }
    while (0);

	unlock_dcc(NULL);
	dcc_garbage_collect();
	pop_message_from(l);
	return malloc_strdup(PortName);
}

/*
 * Usage: $connect(<hostname> <portnum> <family>)
 */
char	*dcc_raw_connect (const char *host, const char *port, int family)
{
	DCC_list *	Client = NULL;
	SSu		my_sockaddr;
	char 		retval[12];
	int		l;

	*retval = 0;
	lock_dcc(NULL);
	l = message_from(NULL, LEVEL_DCC);

    do
    {
	memset(&my_sockaddr.ss, 0, sizeof(my_sockaddr.ss));
	my_sockaddr.sa.sa_family = family;
	if (inet_strton(host, port, &my_sockaddr, AI_ADDRCONFIG))
	{
		say("Unknown host: %s", host);
		break;
	}

	if ((Client = dcc_searchlist(DCC_RAW, port, host, NULL, -1)))
	{
	    if (Client->flags & DCC_ACTIVE)
	    {
		say("A previous DCC RAW to %s on %s exists", host, port);
		break;
	    }
	}
	else
	    Client = dcc_create(DCC_RAW, port, host, NULL, family, 0);

	lock_dcc(Client);
	Client->offer.ss = my_sockaddr.ss;
	Client->flags = DCC_THEIR_OFFER | DCC_RAW;
	if (dcc_connect(Client))	/* Nonblocking from here */
	{
		unlock_dcc(Client);
		break;
	}
	unlock_dcc(Client);
	snprintf(retval, sizeof retval, "%d", Client->socket);
    }
    while (0);

	unlock_dcc(NULL);
	pop_message_from(l);
	dcc_garbage_collect();
	return malloc_strdup(retval);
}



/*
 *
 * All the rest of this file is dedicated to automatic replies generated
 * by the client in response to external stimuli from DCC connections.
 *
 */



/*
 * When a user does a CTCP DCC, it comes here for preliminary parsing.
 *
 * XXX This function is not really family independant (but it's close)
 */
void	register_dcc_offer (const char *user, char *type, char *description, char *address, char *port, char *size, char *extra, char *rest)
{
	DCC_list *	dcc = NULL;
	int		dtype;
	char *		c;
	unsigned short	realport;	/* Bleh */
	char 		p_addr[256];
	int		err;
	SSu		offer;
	intmax_t	filesize;
	int		l;

	/* 
	 * Ensure that nobody will mess around with us while we're working.
	 */
	lock_dcc(NULL);
	l = message_from(NULL, LEVEL_DCC);

    do
    {
	/* If we're debugging, give the user the raw handshake details. */
	if (x_debug & DEBUG_DCC_SEARCH)
		yell("register_dcc_offer: [%s|%s|%s|%s|%s|%s|%s]", 
			user, type, description, address, port, size, extra);

	/* If they offer us a path with directory, ignore the directory. */
	if ((c = strrchr(description, '/')))
		description = c + 1;

	/* If they offer us a hidden ("dot") file, mangle the dot. */
	if (*description == '.')
		*description = '_';

	/* If they give us a file size, set the global variable */
	if (size && *size)
		filesize = STRNUM(size);
	else
		filesize = 0;

	/*
	 * Figure out if it's a type of offer we support.
	 */
	if (!my_stricmp(type, "CHAT"))
		dtype = DCC_CHAT;
	else if (!my_stricmp(type, "SEND"))
	{
	    if (!size || !*size || !is_number(size))
	    {
		say("DCC SEND (%s) received from %s without a file size",
					description, user);
		break;
	    }
	    dtype = DCC_FILEREAD;
	}
#ifdef MIRC_BROKEN_DCC_RESUME
	else if (!my_stricmp(type, "RESUME"))
	{
		/* 
		 * Dont be deceieved by the arguments we're passing it.
		 * The arguments are "out of order" because MIRC doesnt
		 * send them in the traditional order.  Ugh.
		 */
		if (!port || !*port)
		{
		    say("DCC RESUME received from %s without a resume location",
					user);
		    break;
		}
		dcc_getfile_resume_demanded(user, description, address, port);
		break;
	}
	else if (!my_stricmp(type, "ACCEPT"))
	{
		/*
		 * See the comment above.
		 */
		dcc_getfile_resume_start (user, description, address, port);
		break;
	}
#endif
        else
        {
                say("Unknown DCC %s (%s) received from %s", 
				type, description, user);
		break;
        }

	/* 	CHECK HANDSHAKE ADDRESS FOR VALIDITY 	*/
	/*
	 * Convert the handshake address to a sockaddr.  If it cannot be
	 * figured out, warn the user and punt.
	 */
	memset(&offer.ss, 0, sizeof(offer.ss));
	offer.sa.sa_family = AF_UNSPEC;
	if ((err = inet_strton(address, port, &offer, AI_NUMERICHOST)))
	{
		say("DCC %s (%s) request from %s had mangled return address "
			"[%s] (%d)", type, description, user, address, err);
		break;
	}

	/*
	 * Convert the sockaddr back to a name that we can print.
	 * What we've got now is the original handshake address, a 
	 * sockaddr, and a p-addr.
	 */
	if (inet_ntostr(&offer, p_addr, 256, NULL, 0, NI_NUMERICHOST))
	{
		say("DCC %s (%s) request from %s could not be converted back "
		    "into a p-addr [%s] [%s]", 
			type, description, user, address, port);
		break;
	}

	/*
	 * Check for invalid or illegal IP addresses.
	 */
	if (family(&offer) == AF_INET)
	{
	    if (offer.si.sin_addr.s_addr == 0)
	    {
		yell("### DCC handshake from %s ignored becuase it had "
				"an null address", user);
		break;
	    }
	}
	else if (family(&offer) == AF_INET6)
	{
		/* Reserved for future expansion */
	}

#ifdef HACKED_DCC_WARNING
	/*
	 * Check for hacked (forged) IP addresses.  A handshake is considered
	 * forged if the address in the handshake is not the same address that
	 * the user is using on irc.  This is not turned on by default because
	 * it can make epic block on dns lookups, which rogue users can use
	 * to make your life painful, and also because a lot of networks are
	 * using faked hostnames on irc, which makes this a waste of time.
	 */
	if (family(&offer) == AF_INET)
	{
		char *	fromhost;
		SSu	irc_addr;

		if (!(fromhost = strchr(FromUserHost, '@')))
		{
			yell("### Incoming handshake from a non-user peer!");
			break;
		}

		fromhost++;
		irc_addr.sa.sa_family = family(&offer);
		if (inet_strton(fromhost, port, &irc_addr, AI_ADDRCONFIG))
		{
			yell("### Incoming handshake has an address or port "
				"[%s:%s] that could not be figured out!", 
				fromhost, port);
			yell("### Please use caution in deciding whether to "
				"accept it or not");
		}
		else if (family(&offer) == AF_INET)
		{
		   if (irc_addr.si.sin_addr.s_addr != offer.si.sin_addr..s_addr)
		   {
			say("WARNING: Fake dcc handshake detected! [%x]", 
				V4ADDR(offer.si).s_addr);
			say("Unless you know where this dcc request is "
				"coming from");
			say("It is recommended you ignore it!");
		   }
		}
	}
	else if (family(&offer) == AF_INET6)
	{
		/* Reserved for future expansion */
	}
#endif

	/* 	CHECK HANDSHAKE PORT FOR VALIDITY 	*/
	if ((realport = strtoul(port, NULL, 10)) < 1024)
	{
		say("DCC %s (%s) request from %s rejected because it "
			"specified reserved port number (%hu) [%s]", 
				type, description, user, 
				realport, port);
		break;
	}

	/*******************************************************************
	 * So now we've checked the address, and it's ok, we've checked the
	 * port, and it's ok, and we've checked the file size, and it's ok.
	 * We are now ready to get down to business!
	 *******************************************************************/

	/* Get ourselves a new dcc entry. */
	if ((dcc = dcc_searchlist(dtype, user, description, NULL, -1)))
	{
	    /* 
	     * If DCC_MY_OFFER is set, that means we have already sent this 
	     * person this same DCC offer they sent us.  Now we have a race
	     * condition.  They will try to accept ours and we will try to 
	     * accept theirs.  Let's see who wins!
	     */
	    if (dcc->flags & DCC_MY_OFFER)
	    {
		/* Revoke the offer I made to them. */
		dcc->flags |= DCC_DELETE;

		/* 
		 * If they offered us a DCC CHAT, create a new entry for
		 * this new offer to us.
		 *
		 * DCC CHAT collision -- do it automatically.
		 */
		if (dtype == DCC_CHAT)
		{
		    char *copy;

		    say("DCC CHAT already requested by %s, connecting.", user);
		    dcc_create(dtype, user, "chat", NULL, family(&offer), filesize);
		    copy = LOCAL_COPY(user);
		    dcc_chat(copy);
		    break;
		}

		/*
		 * Otherwise, we're trying to send the same file to each other.
		 * We just punt here because I don't know what else to do.
		 */
		else
		{
		    say("DCC %s collision for %s:%s", type, user, description);
		    send_ctcp(0, user, "DCC", "DCC %s collision occured while connecting to %s (%s)", 
			type, get_server_nickname(from_server), description);
		    break;
		}
	    }

	    /*
	     * If DCC_ACTIVE is set, that means we already have an open 
	     * connection to them, either a file transfer or a dcc chat.  
	     * We definitely can't have another one open, so punt on this 
	     * new one.
	     */
	    if (dcc->flags & DCC_ACTIVE)
	    {
		say("Received DCC %s request from %s while previous "
			"session still active", type, user);
		break;
	    }
	}
	else
		dcc = dcc_create(dtype, user, description, NULL, 
					family(&offer), filesize);

	/*
	 * Otherwise, we're good to go!  Mark this dcc as being something
	 * they offered to us, and copy over the port and address.
	 */
	dcc->flags |= DCC_THEIR_OFFER;
	memcpy(&dcc->offer.ss, &offer.ss, sizeof offer.ss);

	/* 
	 * DCC SEND and CHAT have different arguments, so they can't
	 * very well use the exact same hooked data.  Both now are
	 * identical for $0-4, and SEND adds filename/size in $5-6 
	 */
	lock_dcc(dcc);
	if ((dcc->flags & DCC_TYPES) == DCC_FILEREAD)
	{
		if (do_hook(DCC_REQUEST_LIST, "%s %s %s %s %s %s "INTMAX_FORMAT,
				  user, type, description, p_addr, port,
				  dcc->description, dcc->filesize))
			goto display_it;
	}
	else
	       if (do_hook(DCC_REQUEST_LIST, "%s %s %s %s %s",
				  user, type, description, p_addr, port))
			goto display_it;
	unlock_dcc(dcc);

	/* All done! */
	break;

display_it:
	/*
	 * This is kind of a detour we take because the user didn't grab
	 * the ONs and we need to output something to tell them about it.
	 */
	unlock_dcc(dcc);

	/*
	 * For DCC SEND offers, if we already have the file, we need to warn
	 * the user so they don't accidentally delete one of their files!
	 */
	if ((dcc->flags & DCC_TYPES) == DCC_FILEREAD)
	{
	    Stat 	statit;
	    char *	realfilename;

	    if (get_string_var(DCC_STORE_PATH_VAR))
		realfilename = malloc_sprintf(NULL, "%s/%s", 
					get_string_var(DCC_STORE_PATH_VAR), 
					dcc->description);
	    else
		realfilename = malloc_strdup(dcc->description);


	    if (stat(realfilename, &statit) == 0)
	    {
		int xclose = 0, resume = 0, ren = 0, get = 0;

		if ((intmax_t)statit.st_size < dcc->filesize)
		{
		    say("WARNING: File [%s] exists but is smaller than "
			"the file offered.", realfilename);
		    xclose = resume = ren = get = 1;
		}
		else if ((intmax_t)statit.st_size == dcc->filesize)
		{
		    say("WARNING: File [%s] exists, and its the same size.",
			realfilename);
		    xclose = ren = get = 1;
		}
		else
		{
		    say("WARNING: File [%s] already exists.", realfilename);
		    xclose = ren = get = 1;
		}

		if (xclose)
		    say("Use /DCC CLOSE GET %s %s        to not get the file.",
				user, dcc->description);
#ifdef MIRC_BROKEN_DCC_RESUME
		if (resume)
		    say("Use /DCC RESUME %s %s           to continue the "
			"copy where it left off.",
				user, dcc->description);
#endif
		if (get)
		    say("Use /DCC GET %s %s              to overwrite the "
			"existing file.", 
				user, dcc->description);
		if (ren)
		    say("Use /DCC RENAME %s %s newname   to save it to a "
			"different filename.", 
				user, dcc->description);
	    }
	    new_free(&realfilename);
	}

	/* Thanks, Tychy! (lherron@imageek.york.cuny.edu) */
	if ((dcc->flags & DCC_TYPES) == DCC_FILEREAD)
		say("DCC %s (%s "INTMAX_FORMAT") request received "
				"from %s!%s [%s (%s)]",
			type, description, dcc->filesize, user, 
			FromUserHost, p_addr, port);
	else
		say("DCC %s (%s) request received from %s!%s [%s (%s)]", 
			type, description, user, FromUserHost, p_addr, port);
    }
    while (0);

	if (dcc)
	{
		get_time(&dcc->lasttime);
		get_time(&dcc->starttime);
	}

	unlock_dcc(NULL);
	dcc_garbage_collect();
	pop_message_from(l);
	return;
}

/*
 * Check all DCCs for data, and if they have any, perform whatever
 * actions are required.
 */
void	do_dcc (int fd)
{
	DCC_list	*Client;
	int		previous_server;
	int		l;
	int		found_it = 0;

	/* Sanity */
	if (fd < 0)
		return;

	/* Whats with all this double-pointer chicanery anyhow? */
	lock_dcc(NULL);

	for (Client = ClientList; Client; Client = Client->next)
	{
	    if (Client->socket == fd)
	    {
		found_it = 1;
		previous_server = from_server;
		from_server = FROMSERV;

	        l = message_from(NULL, LEVEL_DCC);

		if (Client->flags & DCC_DELETE)
		{
		    say("DCC fd %d is ready, client is deleted.  Closing.", fd);
		    Client->socket = new_close(Client->socket);
		}
		else switch (Client->flags & DCC_TYPES)
		{
		    case DCC_CHAT:
			process_dcc_chat(Client);
			break;
		    case DCC_RAW_LISTEN:
			process_incoming_listen(Client);
			break;
		    case DCC_RAW:
			process_incoming_raw(Client);
			break;
		    case DCC_FILEOFFER:
			process_dcc_send(Client);
			break;
		    case DCC_FILEREAD:
			process_incoming_file(Client);
			break;
		}
		get_time(&Client->lasttime);
		pop_message_from(l);

		from_server = previous_server;
	    }

	    /*
	     * Don't time out raw_listen sockets.
	     */
	    else if ((Client->flags & DCC_TYPES) == DCC_RAW_LISTEN) 
		/* nothing */(void)0;

	    /*
	     * This shouldnt be neccesary any more, but what the hey,
	     * I'm still paranoid.
	     */
	    if (!Client)
		break;
	}

	if (!found_it)
	{
	   yell("DCC callback for fd %d but it doesn't exist any more! "
			"Closing it.  Wish me luck!", fd);
	   new_close(fd);
	}

	unlock_dcc(NULL);
	dcc_garbage_collect();
}



/*********************************** DCC CHAT *******************************/
/*
 * This is a "unix_accept" callback, which is invoked when a remote user
 * accepts our dcc chat offer.  unix_accept sends us the file descriptor
 * (from accept(2)) and the getpeeraddr() from that socket.
 *
 * At this point, we can close our listen(2) socket (in Client->socket) and
 * use the new accept(2) socket instead, and start the connection.
 */
static	void	process_dcc_chat_connection (DCC_list *Client)
{
	char	p_addr[256];
	char	p_port[24];
	int	fd;
	int	c1, c2;

	c1 = DGETS(Client->socket, fd)
	c2 = DGETS(Client->socket, Client->peer_sockaddr.ss)
	if (c1 != sizeof(fd) || c2 != sizeof(Client->peer_sockaddr.ss))
	{
		Client->flags |= DCC_DELETE;
		yell("### DCC Error: accept() failed.");
		return;
	}

	Client->socket = new_close(Client->socket);
	if ((Client->socket = fd) > 0)
		new_open(Client->socket, do_dcc, NEWIO_RECV, 1, Client->server);
	else
	{
		Client->flags |= DCC_DELETE;
		yell("### DCC Error: accept() failed.  Punting.");
		return;
	}

	Client->flags &= ~DCC_MY_OFFER;
	Client->flags |= DCC_ACTIVE;

	inet_ntostr(&Client->peer_sockaddr, p_addr, 256, p_port, 24, NI_NUMERICHOST);

	lock_dcc(Client);
	if (do_hook(DCC_CONNECT_LIST, "%s CHAT %s %s", 
				Client->user, p_addr, p_port))
	    say("DCC chat connection to %s[%s:%s] established", 
				Client->user, p_addr, p_port);
	unlock_dcc(Client);

	get_time(&Client->starttime);
}

static	void	process_dcc_chat_error (DCC_list *Client)
{
	lock_dcc(Client);
	if (do_hook(DCC_LOST_LIST, "%s CHAT ERROR", Client->user))
		say("DCC CHAT connection to %s lost", Client->user);
	Client->flags |= DCC_DELETE;
	unlock_dcc(Client);
	return;
}

static	char *	process_dcc_chat_ctcps (DCC_list *Client, char *tmp)
{
	char 	equal_nickname[80];
	int	ctcp_request = 0, ctcp_reply = 0;
	int	l;
	char 	*target = NULL, *extra = NULL;

#define CTCP_MESSAGE "CTCP_MESSAGE "
#define CTCP_REPLY "CTCP_REPLY "

	if (*tmp == CTCP_DELIM_CHAR)
		ctcp_request = 1;
	else if (!strncmp(tmp, CTCP_MESSAGE, strlen(CTCP_MESSAGE)))
	{
		ov_strcpy(tmp, tmp + strlen(CTCP_MESSAGE));
		ctcp_request = 1;
	}
	else if (!strncmp(tmp, CTCP_REPLY, strlen(CTCP_REPLY)))
	{
		ov_strcpy(tmp, tmp + strlen(CTCP_REPLY));
		ctcp_reply = 1;
	}

	if (ctcp_request == 1 || ctcp_reply == 1)
	{
		const char *OFUH = FromUserHost;

		if (Client->userhost && *Client->userhost)
			FromUserHost = Client->userhost;
		else
			FromUserHost = unknown_userhost;

		/*
		 * So 'inbound_recode' will decode 'tmp' in place,
		 * UNLESS 'tmp' isn't big enough to hold the new text.
		 * In that case, 'extra' is malloc()ed and it will
		 * hold the new text.  
		 * So if 'extra' is null, 'tmp' already holds it.
		 * If 'extra' is not null, 'tmp' needs to point to it.
		 */
		target = dcc_target(Client->user);
		inbound_recode(target, -1, empty_string, tmp, &extra);
		if (extra)
			tmp = extra;

		l = message_from(target, LEVEL_CTCP);
		if (ctcp_request == 1)
			tmp = do_ctcp(1, target, nickname, tmp);
		else
			tmp = do_ctcp(0, target, nickname, tmp);
		pop_message_from(l);

		FromUserHost = OFUH;

		new_free(&target);
		new_free(&extra);
	}

	if (!tmp || !*tmp)
		return NULL;

	return tmp;
}

static	void	process_dcc_chat_data (DCC_list *Client)
{
	char	tmp[IO_BUFFER_SIZE + 1];
	ssize_t	bytesread;
	int	l;
const	char *	OFUH = FromUserHost;
	char  *	target;
const	char *	utf8_text = NULL;
	char *	extra = NULL;

	/* Get a new line via dgets. */
	bytesread = dgets(Client->socket, tmp, IO_BUFFER_SIZE, 1);

	/* 
	 * bytesread == 0 means there was new data, but it was an incomplete
	 * line.  Since we allow dgets() to buffer for us, we just ignore
	 * this and wait for the rest of the line later.
	 */
	if (bytesread == 0)
		return;

	/*
	 * bytesread == -1 means the connection just totaly died.
	 */
	if (bytesread == -1)
	{
		process_dcc_chat_error(Client);
		return;
	}

	/*
	 * Otherwise, handle a new DCC CHAT message.
	 */
	Client->bytes_read += bytesread;

	chomp(tmp);

	/* Tell the user... */
	if (x_debug & DEBUG_INBOUND) 
		yell("DCC: [%d] <- [%s]", Client->socket, tmp);

	/* Handle any CTCPs... */
	/* If the message is empty, ignore it... */
	if (!process_dcc_chat_ctcps(Client, tmp) || !*tmp)
		return;

	/* Otherwise throw the message to the user. */
	target = dcc_target(Client->user);
	utf8_text = inbound_recode(target, -1, empty_string, tmp, &extra);

	l = message_from(target, LEVEL_DCC);
	lock_dcc(Client);
	if (Client->userhost && *Client->userhost)
		FromUserHost = Client->userhost;
	else
		FromUserHost = unknown_userhost;

	if (do_hook(DCC_CHAT_LIST, "%s %s", Client->user, utf8_text))
	{
		char timestr[256];

		*timestr = 0;
		if (get_server_away(NOSERV))
			snprintf(timestr, sizeof(timestr), " <%s>", my_ctime(time(NULL)));

		put_it("=%s= %s%s", Client->user, utf8_text, timestr);
	}

	FromUserHost = OFUH;
	unlock_dcc(Client);
	pop_message_from(l);

	new_free(&target);
	new_free(&extra);
}

/*
 * This is a unix_connect() callback which is invoked when a connect(2)ing
 * socket is ready to write (which means the connection is complete or failed)
 * This is used when we connect to someone else's offer
 */
static void	process_dcc_chat_connected (DCC_list *dcc)
{
	lock_dcc(dcc);
	if (x_debug & DEBUG_SERVER_CONNECT)
	    yell("process_dcc_chat_connected: dcc [%s] now ready to write", 
			dcc->user);

	if (dcc_get_connect_addrs(dcc))
	{
	    if (do_hook(DCC_LOST_LIST, "%s CHAT ERROR", dcc->user))
		say("DCC CHAT connection to %s lost", dcc->user);
	    dcc->flags |= DCC_DELETE;
	    unlock_dcc(dcc);
	    return;
	}

	dcc_connected(dcc->socket);
	dcc->flags &= ~DCC_CONNECTING;
	unlock_dcc(dcc);
}

static	void	process_dcc_chat (DCC_list *Client)
{
	if (Client->flags & DCC_MY_OFFER)
		process_dcc_chat_connection(Client);
	else if (Client->flags & DCC_CONNECTING)
		process_dcc_chat_connected(Client);
	else
		process_dcc_chat_data(Client);
}


/****************************** DCC RAW *************************************/
/*
 * This is a unix_accept() callback invoked whenever we accept(2) an incoming
 * connection to our listen(2)ing socket.  This creates a new DCC RAW, and
 * the original DCC RAW listen is unchanged.
 */
static	void		process_incoming_listen (DCC_list *Client)
{
	SSu		remaddr;
	int		new_socket;
	char		fdstr[10];
	DCC_list	*NewClient;
	char		host[1025];
	socklen_t	len;
	char		p_port[24];
	char		l_port[24];
	char		trash[1025] = "";
	int		c1, c2;

	memset(&remaddr.ss, 0, sizeof(remaddr.ss));

	c1 = DGETS(Client->socket, new_socket)
	c2 = DGETS(Client->socket, remaddr.ss)
	if (c1 != sizeof(new_socket) || c2 != sizeof(remaddr.ss))
	{
		Client->flags |= DCC_DELETE;
		yell("### DCC Error: accept() failed.");
		return;
	}

	if (new_socket < 0)
	{
		yell("### DCC Error: accept() failed.  Punting.");
		return;
	}

	*host = 0;
	inet_ntostr(&remaddr, host, sizeof(host), p_port, sizeof(p_port), 0);

	strlcpy(fdstr, ltoa(new_socket), sizeof fdstr);
	if (!(NewClient = dcc_searchlist(DCC_RAW, fdstr, host, NULL, 0)))
		NewClient = dcc_create(DCC_RAW, fdstr, host, NULL, 0, 0);

	NewClient->socket = new_socket;
	memcpy(&NewClient->peer_sockaddr.ss, &remaddr.ss, sizeof(remaddr.ss));
	memcpy(&NewClient->offer.ss, &remaddr.ss, sizeof(remaddr.ss));

	len = sizeof(NewClient->local_sockaddr.ss);
	getsockname(NewClient->socket, &NewClient->local_sockaddr.sa, &len);
	inet_ntostr(&Client->local_sockaddr, 
				trash, sizeof(trash), 
				l_port, sizeof(l_port), 0);

	NewClient->flags |= DCC_ACTIVE;
	NewClient->flags |= DCC_QUOTED & Client->flags;
	NewClient->bytes_read = NewClient->bytes_sent = 0;
	get_time(&NewClient->starttime);
	new_open(NewClient->socket, do_dcc, NEWIO_RECV, 1, NewClient->server);

	lock_dcc(Client);
	if (do_hook(DCC_RAW_LIST, "%s %s N %s", 
			NewClient->user, NewClient->description, l_port))
            if (do_hook(DCC_CONNECT_LIST,"%s RAW %s %s", 
			NewClient->user, NewClient->description, l_port))
		say("DCC RAW connection to %s on %s via %s established",
			NewClient->description, NewClient->user, p_port);
	unlock_dcc(Client);
}


/*
 * This handles when someone sends you a line of info over a DCC RAW
 * connection (that was established with a $listen() or $connect()).
 */
static	void		process_dcc_raw_data (DCC_list *Client)
{
	char	tmp[IO_BUFFER_SIZE + 1];
	char 	*bufptr;
	char *	freeme = NULL;
	ssize_t	bytesread;

        bufptr = tmp;
	if (Client->flags & DCC_QUOTED)
		bytesread = dgets(Client->socket, bufptr, IO_BUFFER_SIZE, -1);
	else if (Client->packet_size > 0)
		bytesread = dgets(Client->socket, bufptr, Client->packet_size, 2);
	else if (Client->full_line_buffer)
		bytesread = dgets(Client->socket, bufptr, IO_BUFFER_SIZE, 1);
	else
		bytesread = dgets(Client->socket, bufptr, IO_BUFFER_SIZE, 0);

	switch (bytesread)
	{
	    CLOSE:
	    case -1:
	    {
		lock_dcc(Client);
		if (do_hook(DCC_RAW_LIST, "%s %s C",
				Client->user, Client->description))
       		if (do_hook(DCC_LOST_LIST,"%s RAW %s", 
				Client->user, Client->description))
			say("DCC RAW connection to %s on %s lost",
				Client->user, Client->description);
		Client->flags |= DCC_DELETE;
		unlock_dcc(Client);
		break;
	    }
	    case 0:
	    {
		if (Client->flags & DCC_QUOTED)
			goto CLOSE;
		/* FALLTHROUGH */
		FALLTHROUGH;
	    }
	    default:
	    {
		from_server = primary_server;	/* Colten asked for it */
		if (Client->flags & DCC_QUOTED)
		{
			char *  dest;
			if (!(dest = transform_string_dyn("+CTCP", bufptr, 
							bytesread, NULL)))
				yell("DCC RAW: Could not CTCP enquote [%s]",
					bufptr);
			else
				freeme = bufptr = dest;
		}
		else if (bytesread > 0 && tmp[strlen(tmp) - 1] == '\n')
			tmp[strlen(tmp) - 1] = '\0';
		Client->bytes_read += bytesread;

		lock_dcc(Client);
		if (do_hook(DCC_RAW_LIST, "%s %s D %s",
				Client->user, Client->description, bufptr))
			say("Raw data on %s from %s: %s",
				Client->user, Client->description, bufptr);
		unlock_dcc(Client);
	    }
	}
	new_free(&freeme);
	return;
}

/*
 * This is a unix_connect() callback called when your $connect() is writable
 * either connect()ed or it failed. 
 */
static void	process_dcc_raw_connected (DCC_list *dcc)
{
	lock_dcc(dcc);
	if (x_debug & DEBUG_SERVER_CONNECT)
	    yell("process_dcc_raw_connected: dcc [%s] now ready to write", 
			dcc->user);

	if (dcc_get_connect_addrs(dcc))
	{
	    if (do_hook(DCC_LOST_LIST, "%s RAW ERROR", dcc->user))
		say("DCC RAW connection to %s lost", dcc->user);
	    dcc->flags |= DCC_DELETE;
	    unlock_dcc(dcc);
	    return;
	}

	if (family(&dcc->peer_sockaddr) == AF_INET)
		malloc_strcpy(&dcc->othername, 
				ltoa(ntohs(dcc->peer_sockaddr.si.sin_port)));
	else if (family(&dcc->peer_sockaddr) == AF_INET6)
		malloc_strcpy(&dcc->othername, 
				ltoa(ntohs(dcc->peer_sockaddr.si6.sin6_port)));
	else
		malloc_strcpy(&dcc->othername, "<any>");

	dcc->user = malloc_strdup(ltoa(dcc->socket));
	do_hook(DCC_RAW_LIST, "%s %s E %s", dcc->user, dcc->description, 
						dcc->othername);

	dcc_connected(dcc->socket);
	dcc->flags &= ~DCC_CONNECTING;
	unlock_dcc(dcc);
}


static	void		process_incoming_raw (DCC_list *Client)
{
	if (Client->flags & DCC_CONNECTING)
		process_dcc_raw_connected(Client);
	else
		process_dcc_raw_data(Client);
}

/****************************** DCC SEND ************************************/
/*
 * When youre sending a file, and your peer sends an ACK, this handles
 * whether or not to send the next packet.
 */
static void	process_dcc_send_connection (DCC_list *dcc)
{
	int		new_fd;
#ifdef HAVE_SO_SNDLOWAT
	int		size;
#endif
	char		p_addr[256];
	char		p_port[24];
	char		*encoded_description;
	int		c1, c2;

	c1 = DGETS(dcc->socket, new_fd)
	c2 = DGETS(dcc->socket, dcc->peer_sockaddr.ss)
	if (c1 != sizeof(new_fd) || c2 != sizeof(dcc->peer_sockaddr.ss))
	{
		dcc->flags |= DCC_DELETE;
		yell("### DCC Error: accept() failed.");
		return;
	}

	dcc->socket = new_close(dcc->socket);
	if ((dcc->socket = new_fd) < 0)
	{
		dcc->flags |= DCC_DELETE;
		yell("### DCC Error: accept() failed.  Punting.");
		return;
	}
	new_open(dcc->socket, do_dcc, NEWIO_RECV, 1, dcc->server);
	dcc->flags &= ~DCC_MY_OFFER;
	dcc->flags |= DCC_ACTIVE;
	get_time(&dcc->starttime);

#ifdef HAVE_SO_SNDLOWAT
	/*
	 * Give a hint to the OS how many bytes we need to send
	 * for each write()
	 */
	size = DCC_BLOCK_SIZE;
	if (setsockopt(dcc->socket, SOL_SOCKET, SO_SNDLOWAT, 
				&size, sizeof(size)) < 0)
		say("setsockopt failed: %s", strerror(errno));
#endif

	inet_ntostr(&dcc->peer_sockaddr, p_addr, 256, p_port, 24, NI_NUMERICHOST);

	/*
	 * Tell the user
	 */
	lock_dcc(dcc);
	encoded_description = dcc_urlencode(dcc->description);
	if (do_hook(DCC_CONNECT_LIST, "%s SEND %s %s %s "INTMAX_FORMAT,
			dcc->user, p_addr, p_port,
			dcc->description, dcc->filesize))
	    say("DCC SEND connection to %s[%s:%s] established", 
			dcc->user, p_addr, p_port);
	new_free(&encoded_description);
	unlock_dcc(dcc);


	/*
	 * Open up the file to be sent
	 */
	if ((dcc->file = open(dcc->description, O_RDONLY)) == -1)
	{
		dcc->flags |= DCC_DELETE;
		say("Unable to open %s: %s", dcc->description, strerror(errno));
		return;
	}

	/*
	 * Set up any DCC RESUME as needed
	 */
	if (dcc->bytes_sent)
	{
		if (x_debug & DEBUG_DCC_XMIT)
			yell("Resuming at address ("INTMAX_FORMAT")", 
				dcc->bytes_sent);
		lseek(dcc->file, dcc->bytes_sent, SEEK_SET);
	}
}

static void	process_dcc_send_handle_ack (DCC_list *dcc)
{
	char *		encoded_description;
	u_32int_t	bytes;
	intmax_t	provisional_bytes;

	if (x_debug & DEBUG_DCC_XMIT)
		yell("Reading a packet from [%s:%s("INTMAX_FORMAT")]", 
			dcc->user, dcc->othername, dcc->bytes_acked);

	/*
	 * The acknowledgement is /ALWAYS/ a 32 bit integers in network
	 * order, no matter what.  Even if we've sent more than 2^32 bits,
	 * the value wraps around to 0 again. ugh. bleh.
	 */
	if (dgets(dcc->socket, (char *)&bytes, sizeof(bytes), -1) < 
					(ssize_t)sizeof(bytes))
	{
		lock_dcc(dcc);
		encoded_description = dcc_urlencode(dcc->description);
		if (do_hook(DCC_LOST_LIST,"%s SEND %s CONNECTION LOST",
			dcc->user, encoded_description))
		    say("DCC SEND:%s connection to %s lost",
			dcc->description, dcc->user);
		new_free(&encoded_description);
		dcc->flags |= DCC_DELETE;
		unlock_dcc(dcc);
		return;
	}
	bytes = ntohl(bytes);

	/*
	 * XXX Ok.  Rollover logic is atrocious, but I wrote it in a hurry
	 * and if you want to "fix" it, be my guest.
	 *
	 * Acknowledgements are always 32 bits.  This is de facto the right
	 * 32 bits in the number of bytes transferred.  The number of bytes
	 * transferred might exceed 32 bits, so we need to extrapolate what
	 * the extra bits will be.
	 *
	 * Normally, we will substitute the right 32 bits of the last value
	 * of "bytes_acked" with the new bytes value.  This is fine and 
	 * dandy as long as there is no overflow.
	 *
	 * But if there is overflow, then the extended "bytes" will be less
	 * than the previous value of "bytes_acked".  In this case, we will
	 * substitute the right 32 bits of the bytes SENT, since this would
	 * be in theory a value rolled over from the bytes ACKed.  If this
	 * resulting value is reasonable, then we run with it, and this 
	 * effects the rollover.
	 *
	 * Ugh.  Why am I wasting time supporting dcc sends > 2gb anyways?
	 */
	provisional_bytes = dcc->bytes_acked;
	provisional_bytes = provisional_bytes >> 32;
	provisional_bytes = provisional_bytes << 32;
	provisional_bytes += bytes;

	if (provisional_bytes < dcc->bytes_acked)
	{
	    provisional_bytes = dcc->bytes_sent;
	    provisional_bytes = provisional_bytes >> 32;
	    provisional_bytes = provisional_bytes << 32;
	    provisional_bytes += bytes;
	}

	if (provisional_bytes > dcc->bytes_sent)
	{
yell("### WARNING!  The other peer claims to have recieved more bytes than");
yell("### I have actually sent so far.  Please report this to ");
yell("### problems@epicsol.org or #epic on EFNet right away.");
yell("### Ask the person who you sent the file to look for garbage at the");
yell("### end of the file you just sent them.  Please enclose that ");
yell("### information as well as the following:");
yell("###");
yell("###    bytesrecvd [%ld ("INTMAX_FORMAT")]", (long)bytes, provisional_bytes);
yell("###    dcc->bytes_sent ["INTMAX_FORMAT"]", 	dcc->bytes_sent);
yell("###    dcc->filesize ["INTMAX_FORMAT"]", 		dcc->filesize);
yell("###    dcc->bytes_acked ["INTMAX_FORMAT"]", 	dcc->bytes_acked);

		/* And just cope with it to avoid whining */
		dcc->bytes_sent = provisional_bytes;
	}

	if (provisional_bytes >= dcc->bytes_acked)
	{
		if (x_debug & DEBUG_DCC_XMIT)
			yell("Bytes to %ld ACKed", (long)bytes);
		dcc->bytes_acked = provisional_bytes;
	}

	/*
	 * If we've sent the whole file already...
	 */
	if (dcc->bytes_sent >= dcc->filesize)
	{
		/*
		 * If theyve ACKed the last packet, we close 
		 * the connection.
		 */
		if (provisional_bytes >= dcc->filesize)
			DCC_close_filesend(dcc, "SEND", "TRANSFER COMPLETE");

		/*
		 * Either way there's nothing more to do.
		 */
		return;
	}
}


static void	process_dcc_send_data (DCC_list *dcc)
{
	intmax_t	fill_window;
	ssize_t	bytesread;
	char	tmp[DCC_BLOCK_SIZE+1];
	int	old_from_server = from_server;
	char bytes_sent[10];
	char filesize[10];

	/*
	 * We use a nonblocking sliding window algorithm.  We send as many
	 * packets as we can *without blocking*, up to but never more than
	 * the value of /SET DCC_SLIDING_WINDOW.  Whenever we recieve some
	 * stimulus (like from an ACK) we re-fill the window.  We always do
	 * a my_iswritable() before we write() to make sure that it wont block.
	 */
	fill_window = get_int_var(DCC_SLIDING_WINDOW_VAR) * DCC_BLOCK_SIZE;
	if (fill_window < DCC_BLOCK_SIZE)
		fill_window = DCC_BLOCK_SIZE;		/* Sanity */

	while (dcc->bytes_sent - dcc->bytes_acked < fill_window)
	{
		/*
		 * Check to make sure the write won't block.
		 */
		if (my_iswritable(dcc->socket, 0) <= 0)
			break;

		/*
		 * Grab some more file.  If this chokes, dont sweat it.
		 */
		if ((bytesread = read(dcc->file, tmp, DCC_BLOCK_SIZE)) <= 0)
			break;

		/*
		 * Bug the user
		 */
		if (x_debug & DEBUG_DCC_XMIT)
		    yell("Sending packet [%s [%s] (packet XXX) (%ld bytes)]",
			dcc->user, dcc->othername, bytesread);

		/*
		 * Attempt to write the file.  If it chokes, whine.
		 */
		if (write(dcc->socket, tmp, bytesread) < bytesread)
		{
			dcc->flags |= DCC_DELETE;
			say("Outbound write() failed: %s", strerror(errno));
			from_server = old_from_server;
			return;
		}

		dcc->bytes_sent += bytesread;

		/* XXX When should this ever be possible? */
		if (!dcc->filesize)
			continue;

		calc_size(dcc->bytes_sent, bytes_sent, sizeof(bytes_sent));
		calc_size(dcc->filesize, filesize, sizeof(filesize));

		update_transfer_buffer(dcc, "(to %10s: %s of %s: %d%%)",
			dcc->user, 
			bytes_sent, filesize,
			(int)(dcc->bytes_sent * 100.0 / dcc->filesize));
	}
}

static	void	process_dcc_send (DCC_list *dcc)
{
	if (dcc->flags & DCC_MY_OFFER)
		process_dcc_send_connection(dcc);
	else
		process_dcc_send_handle_ack(dcc);

	process_dcc_send_data(dcc);
}

/****************************** DCC GET ************************************/
/*
 * When youre recieving a DCC GET file, this is called when the sender
 * sends you a portion of the file. 
 *
 * There's no need to stick to the packet size here.  If we have more than
 * one in the buffer, there's no benefit in sending out acks for all.
 * It's probably more polite to hold back on clobbering the sender with
 * redundant useless acks too.
 */
static	void		process_dcc_get_data (DCC_list *dcc)
{
	char		tmp[DCC_RCV_BLOCK_SIZE+1];
	intmax_t	provisional_bytesread;
	u_32int_t	bytestemp;
	ssize_t		bytesread;
	char 		bytes_read[10];
	char 		filesize[10];

	/* Sanity check -- file size is not permitted to be omitted! */
	if (!dcc->filesize)
	{
		say("DCC GET from %s lost -- Filesize is 0, no data to xfer",
				dcc->user);
		DCC_close_filesend(dcc, "GET", "TRANSFER COMPLETE");
		return;
	}

	/* Read the next chunk of the file from the remote peer */
	if ((bytesread = dgets(dcc->socket, tmp, sizeof(tmp), -1)) <= 0)
	{
		if (dcc->bytes_read < dcc->filesize)
		{
		    say("DCC GET to %s lost -- Remote peer closed connection", 
				dcc->user);
		    DCC_close_filesend(dcc, "GET",
					"REMOTE PEER CLOSED CONNECTION");
		}
		else 
		    DCC_close_filesend(dcc, "GET", "TRANSFER COMPLETE");

		return;
	}

	/* Save the chunk to the local file */
	if ((write(dcc->file, tmp, bytesread)) == -1)
	{
		dcc->flags |= DCC_DELETE;
		say("Write to local file [%d] failed: %s",
					 dcc->file, strerror(errno));
		return;
	}

	/* Acknowledge receipt of the chunk */
	dcc->bytes_read += bytesread;
	provisional_bytesread = dcc->bytes_read;
	provisional_bytesread = provisional_bytesread >> 32;
	provisional_bytesread = provisional_bytesread << 32;
	bytestemp = (u_32int_t)(dcc->bytes_read - provisional_bytesread);
	bytestemp = htonl(bytestemp);
	if (write(dcc->socket, (char *)&bytestemp, sizeof(u_32int_t)) == -1)
	{
		dcc->flags |= DCC_DELETE;
		yell("### Writing DCC GET checksum back to %s failed.  "
				"Giving up.", dcc->user);
		return;
	}

	/* TAKE THIS OUT IF IT CAUSES PROBLEMS */
	if (dcc->bytes_read > dcc->filesize)
	{
		dcc->flags |= DCC_DELETE;
		yell("### DCC GET WARNING: incoming file is larger then the "
			"handshake said");
		yell("### DCC GET: Closing connection");
		return;
	}

	/* Tell the user about it */
	calc_size(dcc->bytes_read, bytes_read, sizeof(bytes_read));
	calc_size(dcc->filesize, filesize, sizeof(filesize));
	update_transfer_buffer(dcc, "(to %10s: %s of %s: %d%%)",
		dcc->user, 
		bytes_read, filesize,
		(int)(dcc->bytes_read * 100.0 / dcc->filesize));
}

static void	process_dcc_get_connected (DCC_list *dcc)
{
	lock_dcc(dcc);
	if (x_debug & DEBUG_SERVER_CONNECT)
	    yell("process_dcc_get_connected: dcc [%s] now ready to write", 
			dcc->user);

	if (dcc_get_connect_addrs(dcc))
	{
	    char *edesc = dcc_urlencode(dcc->description);

	    if (do_hook(DCC_LOST_LIST, "%s GET %s ERROR", dcc->user, edesc))
		say("DCC GET connection to %s lost", dcc->user);
	    new_free(&edesc);
	    dcc->flags |= DCC_DELETE;
	    unlock_dcc(dcc);
	    return;
	}

	dcc_connected(dcc->socket);
	dcc->flags &= ~DCC_CONNECTING;
	unlock_dcc(dcc);
}

static	void		process_incoming_file (DCC_list *dcc)
{
	if (dcc->flags & DCC_CONNECTING)
		process_dcc_get_connected(dcc);
	else
		process_dcc_get_data(dcc);
}


/***************************************************************************/
/***************************************************************************/


/*
 * This is a callback.  When we want to do a CTCP DCC REJECT, we do
 * a WHOIS to make sure theyre still on irc, no sense sending it to
 * nobody in particular.  When this gets called back, that means the
 * peer is indeed on irc, so we send them the REJECT.
 *
 * -- Changed this to be quasi-reentrant. yuck.
 */
static	void 	output_reject_ctcp (int refnum, char *original, char *received)
{
	/*char	*nickname_requested;*/
	char	*nickname_recieved;
	char	*type;
	char	*description;
	int	old_fs;

	/*
	 * XXX This is, of course, a monsterous hack.
	 */
	/*nickname_requested 	= */next_arg(original, &original);
	type 			= next_arg(original, &original);
	description 		= next_arg(original, &original);
	nickname_recieved 	= next_arg(received, &received);

	if (nickname_recieved && *nickname_recieved)
	{
		/* XXX -- Ok, whatever.  */
		dccs_rejected++;

		old_fs = from_server;
		from_server = refnum;
		send_ctcp(0, nickname_recieved, "DCC", "REJECT %s %s", type, description);
		from_server = old_fs;
	}

}


/*
 * This is called when someone sends you a CTCP DCC REJECT.
 */
void 	dcc_reject (const char *from, char *type, char *args)
{
	DCC_list *	Client;
	char *		description;
	int		CType;
	int		l;

	for (CType = 0; dcc_types[CType] != NULL; CType++)
		if (!my_stricmp(type, dcc_types[CType]))
			break;

	if (!dcc_types[CType])
		return;

	l = message_from(from, LEVEL_DCC);
	description = next_arg(args, &args);

	if ((Client = dcc_searchlist(CType, from, description,
					description, -1)))
	{
		lock_dcc(Client);

                if (do_hook(DCC_LOST_LIST,"%s %s %s REJECTED", from, type,
                        description ? description : "<any>"))
		    say("DCC %s:%s rejected by %s: closing", type,
			description ? description : "<any>", from);
		Client->flags |= DCC_REJECTED;
		Client->flags |= DCC_DELETE; 
		unlock_dcc(Client);
	}

	dcc_garbage_collect();
	pop_message_from(l);
}


static void	DCC_close_filesend (DCC_list *Client, const char *info,
		const char *errormsg)
{
	char	lame_ultrix[13];	/* should be plenty */
	char	lame_ultrix2[13];
	char	lame_ultrix3[13];
	double 	xtime, xfer;
	char	*encoded_description;

	/* XXX - Can't we do this by calling calc_speed? */
	xtime = time_diff(Client->starttime, get_time(NULL));
	xtime -= Client->heldtime;
	if (Client->bytes_sent)
		xfer = Client->bytes_sent - Client->resume_size;
	else
		xfer = Client->bytes_read - Client->resume_size;
	snprintf(lame_ultrix, sizeof(lame_ultrix), 
			"%2.4g", (xfer / 1024.0 / xtime));

	/* Cant pass %g to put_it (lame ultrix/dgux), fix suggested by sheik. */
	if (xfer <= 0)
		xfer = 1;
	snprintf(lame_ultrix2, sizeof(lame_ultrix2), "%2.4g", xfer / 1024.0);

	if (xtime <= 0)
		xtime = 1;
	snprintf(lame_ultrix3, sizeof(lame_ultrix3), "%2.6g", xtime);

	lock_dcc(Client);
	
	if ((Client->flags & DCC_TYPES) == DCC_FILEOFFER)
		encoded_description = dcc_urlencode(Client->description);
	else
		/* assume the other end encoded the filename */
		encoded_description = malloc_strdup(Client->description);

	if (do_hook(DCC_LOST_LIST,"%s %s %s %s %s",
		Client->user, info, encoded_description, lame_ultrix,
		errormsg))
	     say("DCC %s:%s [%skb] with %s completed in %s sec (%s kb/sec)",
		info, Client->description, lame_ultrix2, Client->user, 
		lame_ultrix3, lame_ultrix);

	new_free(&encoded_description);
	Client->flags |= DCC_DELETE;
	unlock_dcc(Client);
}



/* 
 * Looks for the dcc transfer that is "current" (last recieved data)
 * and returns information for it
 */
char *	DCC_get_current_transfer (void)
{
	return DCC_current_transfer_buffer;
}


static void 	update_transfer_buffer (DCC_list *dcc, const char *format, ...)
{
	if (!dcc_updates_status || (dcc && !dcc->updates_status))
		return;

	if (format)
	{
		va_list args;
		va_start(args, format);
		vsnprintf(DCC_current_transfer_buffer, 
				sizeof DCC_current_transfer_buffer, 
				format, args);
		va_end(args);
	}
	else
		*DCC_current_transfer_buffer = 0;

	if (do_hook(DCC_ACTIVITY_LIST, "%ld", dcc ? dcc->refnum : -1))
		update_all_status();
}


static	char *	dcc_urlencode (const char *s)
{
	const char *	p1;
	char *	p2;
	char *	src;
	size_t	srclen;
	char *	dest;
	size_t	destsize;

	src = LOCAL_COPY(s);
	for (p1 = s, p2 = src; *p1; p1++)
	{
		if (*p1 == '\\')
			continue;
		*p2++ = *p1;
	}
	*p2 = 0;

	srclen = strlen(src);
	destsize = srclen * 3 + 2;
	dest = new_malloc(destsize);
        transform_string(URL_xform, XFORM_ENCODE, NULL, 
			src, srclen, dest, destsize);
	return dest;
}

static	char *	dcc_urldecode (const char *s)
{
	char *	p1;
	size_t	srclen;
	char *	dest;
	size_t	destsize;

	srclen = strlen(s);
	destsize = srclen + 2;
	dest = new_malloc(destsize);
        transform_string(URL_xform, XFORM_DECODE, NULL,
				s, srclen,
				dest, destsize);

	for (p1 = dest; *p1; p1++)
	{
		if (*p1 != '.')
			break;
		*p1 = '_';
	}

	return dest;
}

int	wait_for_dcc (const char *descriptor)
{
	DCC_list	*dcc;
	char		reason[1024];
	int		fd;

	if (!is_number(descriptor))
	{
		yell("File descriptor (%s) should be a number", descriptor);
		return -1;
	}

	fd = atol(descriptor);
	if (!(dcc = get_dcc_by_filedesc(fd)))
		return -1;

	if (!(dcc->flags & DCC_CONNECTING))
		return 0;

	snprintf(reason, 1024, "WAIT on DCC %s", descriptor);
	lock_stack_frame();
	while (dcc->flags & DCC_CONNECTING)
		io(reason);
	unlock_stack_frame();
	return 0;
}

static void	fill_in_default_port (DCC_list *dcc)
{
	if (default_dcc_port)
	{
		char dccref[10];
		char *newport = NULL;

		snprintf(dccref, sizeof(dccref), "%ld", dcc->refnum);
		newport = expand_alias(default_dcc_port, dccref);
		dcc->want_port = (unsigned short)my_atol(newport);
		new_free(&newport);
	}
}


/*
 * This stuff doesnt conform to the protocol.
 * Thanks mirc for disregarding the protocol.
 */
#ifdef MIRC_BROKEN_DCC_RESUME

/*
 * When the peer demands DCC RESUME
 * We send out a DCC ACCEPT
 */
static void	dcc_getfile_resume_demanded (const char *user, char *filename, char *port, char *offset)
{
	DCC_list	*Client;
	int		proto;
	char 		*realfilename = filename;

	if (!get_int_var(MIRC_BROKEN_DCC_RESUME_VAR))
		return;

	if (x_debug & DEBUG_DCC_XMIT)
		yell("GOT DCC RESUME REQUEST from [%s] [%s|%s|%s]", user, filename, port, offset);

	if (!strcmp(filename, "file.ext"))
		filename = NULL;

	if (!(Client = dcc_searchlist(DCC_FILEOFFER, user, filename, port, 0)))
	{
		if (x_debug & DEBUG_DCC_XMIT)
			yell("Resume request that doesnt exist.  Hmmm.");
		return;		/* Its a fake. */
	}

	if (!offset)
		return;		/* Its a fake */

	Client->bytes_acked = Client->bytes_sent = Client->resume_size = STR2INT(offset);
	Client->bytes_read = 0;

	/* Just in case we have to fool the protocol enforcement. */
	proto = get_server_protocol_state(from_server);
	set_server_protocol_state(from_server, 0);

	send_ctcp(1, user, "DCC", "ACCEPT %s %s %s",
		realfilename, port, offset);

	set_server_protocol_state(from_server, proto);
	/* Wait for them to open the connection */
}


/*
 * When we get the DCC ACCEPT
 * We start the connection
 */
static	void	dcc_getfile_resume_start (const char *nick, char *filename, char *port, char *offset)
{
	DCC_list	*Client;

	if (!get_int_var(MIRC_BROKEN_DCC_RESUME_VAR))
		return;

	if (x_debug & DEBUG_DCC_XMIT)
		yell("GOT CONFIRMATION FOR DCC RESUME from [%s] [%s]", nick, filename);

	if (!strcmp(filename, "file.ext"))
		filename = NULL;

	if (!(Client = dcc_searchlist(DCC_FILEREAD, nick, filename, port, 0)))
		return;		/* Its fake. */

	if (!(Client->flags & DCC_RESUME_REQ))
	{
		if (x_debug & DEBUG_DCC_XMIT)
			yell("Unsolicited DCC ACCEPT from [%s] [%s]", nick, filename);
		return;
	}

	Client->flags |= DCC_TWOCLIENTS;
	dcc_connect(Client);
}

#endif

char *	dccctl (char *input)
{
	long		ref;
	char *		listc;
	int		len;
	DCC_list *	client;
	char *		retval = NULL;
	size_t		clue = 0;

	GET_FUNC_ARG(listc, input);
	len = strlen(listc);
	if (!my_strnicmp(listc, "REFNUMS", len)) {
		for (client = ClientList; client; client = client->next)
			malloc_strcat_word_c(&retval, space, ltoa(client->refnum), DWORD_DWORDS, &clue);
	} else if (!my_strnicmp(listc, "REFBASE", len)) {
		int oldref = dcc_refnum;
		if (input && *input)
			GET_INT_ARG(dcc_refnum, input);
		RETURN_INT(oldref);
	} else if (!my_strnicmp(listc, "GET", len)) {
		GET_INT_ARG(ref, input);

		if (!(client = get_dcc_by_refnum(ref)))
			RETURN_EMPTY;

		GET_FUNC_ARG(listc, input);
		len = strlen(listc);
		if (!my_strnicmp(listc, "REFNUM", len)) {
			RETURN_INT(client->refnum);
		} else if (!my_strnicmp(listc, "TYPE", len)) {
			RETURN_STR(dcc_types[client->flags & DCC_TYPES]);
		} else if (!my_strnicmp(listc, "DESCRIPTION", len)) {
			RETURN_STR(client->description);
		} else if (!my_strnicmp(listc, "FILENAME", len)) {
			RETURN_STR(client->local_filename);
		} else if (!my_strnicmp(listc, "USER", len)) {
			RETURN_STR(client->user);
		} else if (!my_strnicmp(listc, "USERHOST", len)) {
			RETURN_STR(client->userhost);
		} else if (!my_strnicmp(listc, "OTHERNAME", len)) {
			RETURN_STR(client->othername);
		} else if (!my_strnicmp(listc, "SIZE", len)) {
			return INT2STR(client->filesize);
		} else if (!my_strnicmp(listc, "FILESIZE", len)) {  /* DEPRECATED */
			return INT2STR(client->filesize);
		} else if (!my_strnicmp(listc, "RESUMESIZE", len)) {
			return INT2STR(client->resume_size);
		} else if (!my_strnicmp(listc, "READBYTES", len)) {
			return INT2STR(client->bytes_read);
		} else if (!my_strnicmp(listc, "SENTBYTES", len)) {
			return INT2STR(client->bytes_sent);
		} else if (!my_strnicmp(listc, "SERVER", len)) {
			RETURN_INT(client->server);
		} else if (!my_strnicmp(listc, "LOCKED", len)) {
			RETURN_INT(client->locked);
		} else if (!my_strnicmp(listc, "HELD", len)) {
			RETURN_INT(client->held);
		} else if (!my_strnicmp(listc, "HELDTIME", len)) {
			RETURN_FLOAT(client->heldtime);
		} else if (!my_strnicmp(listc, "QUOTED", len)) {
			RETURN_INT(client->flags & DCC_QUOTED && 1);
		} else if (!my_strnicmp(listc, "PACKET_SIZE", len)) {
			RETURN_INT(client->packet_size);
		} else if (!my_strnicmp(listc, "FULL_LINE_BUFFER", len)) {
			RETURN_INT(client->full_line_buffer);
		} else if (!my_strnicmp(listc, "FLAGS", len)) {
			/* This is pretty much a crock. */
			RETURN_INT(client->flags);
		} else if (!my_strnicmp(listc, "LASTTIME", len)) {
			malloc_strcat_word_c(&retval, space, ltoa(client->lasttime.tv_sec), DWORD_NO, &clue);
			malloc_strcat_word_c(&retval, space, ltoa(client->lasttime.tv_usec), DWORD_NO, &clue);
		} else if (!my_strnicmp(listc, "STARTTIME", len)) {
			malloc_strcat_word_c(&retval, space, ltoa(client->starttime.tv_sec), DWORD_NO, &clue);
			malloc_strcat_word_c(&retval, space, ltoa(client->starttime.tv_usec), DWORD_NO, &clue);
		} else if (!my_strnicmp(listc, "HOLDTIME", len)) {
			malloc_strcat_word_c(&retval, space, ltoa(client->holdtime.tv_sec), DWORD_NO, &clue);
			malloc_strcat_word_c(&retval, space, ltoa(client->holdtime.tv_usec), DWORD_NO, &clue);
		} else if (!my_strnicmp(listc, "OFFERADDR", len)) {
			char	host[1025], port[25];
			if (inet_ntostr(&client->offer,
					host, sizeof(host),
					port, sizeof(port), NI_NUMERICHOST))
				RETURN_EMPTY;
			malloc_strcat_word_c(&retval, space, host, DWORD_NO, &clue);
			malloc_strcat_word_c(&retval, space, port, DWORD_NO, &clue);
		} else if (!my_strnicmp(listc, "REMADDR", len)) {
			char	host[1025], port[25];
			if (inet_ntostr(&client->peer_sockaddr,
					host, sizeof(host),
					port, sizeof(port), NI_NUMERICHOST))
				RETURN_EMPTY;
			malloc_strcat_word_c(&retval, space, host, DWORD_NO, &clue);
			malloc_strcat_word_c(&retval, space, port, DWORD_NO, &clue);
		} else if (!my_strnicmp(listc, "LOCADDR", len)) {
			char	host[1025], port[25];
			if (inet_ntostr(&client->local_sockaddr,
					host, sizeof(host),
					port, sizeof(port), NI_NUMERICHOST))
				RETURN_EMPTY;
			malloc_strcat_word_c(&retval, space, host, DWORD_NO, &clue);
			malloc_strcat_word_c(&retval, space, port, DWORD_NO, &clue);
		} else if (!my_strnicmp(listc, "READABLE", len)) {
			int retint;
			retint = my_isreadable(client->socket, 0) > 0;
			RETURN_INT(retint);
		} else if (!my_strnicmp(listc, "WRITABLE", len)) {
			int retint;
			retint = my_iswritable(client->socket, 0) > 0;
			RETURN_INT(retint);
		} else if (!my_strnicmp(listc, "UPDATES_STATUS", len)) {
			RETURN_INT(client->updates_status);
		} else if (!my_strnicmp(listc, "WANT_PORT", len)) {
			RETURN_INT(client->want_port);
		} else {
			RETURN_EMPTY;
		}
	} else if (!my_strnicmp(listc, "SET", len)) {
		GET_INT_ARG(ref, input);

		if (!(client = get_dcc_by_refnum(ref)))
			RETURN_EMPTY;

		GET_FUNC_ARG(listc, input);
		len = strlen(listc);
		if (!my_strnicmp(listc, "REFNUM", len)) {
			long	newref;

			GET_INT_ARG(newref, input);
			client->refnum = newref;

			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "DESCRIPTION", len)) {
			malloc_strcpy(&client->description, input);
		} else if (!my_strnicmp(listc, "FILENAME", len)) {
			malloc_strcpy(&client->local_filename, input);
		} else if (!my_strnicmp(listc, "USER", len)) {
			malloc_strcpy(&client->user, input);
		} else if (!my_strnicmp(listc, "USERHOST", len)) {
			malloc_strcpy(&client->userhost, input);
		} else if (!my_strnicmp(listc, "OTHERNAME", len)) {
			malloc_strcpy(&client->othername, input);
		} else if (!my_strnicmp(listc, "WANT_PORT", len)) {
			long	newref;
			GET_INT_ARG(newref, input);
			client->want_port = (unsigned short)newref;
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "HELD", len)) {
			long	hold, held;

			GET_INT_ARG(hold, input);
			if (hold)
				held = dcc_hold(client);
			else
				held = dcc_unhold(client);

			RETURN_INT(held);
		} else if (!my_strnicmp(listc, "QUOTED", len)) {
			long	quoted;

			GET_INT_ARG(quoted, input);
			if (quoted)
				client->flags |= DCC_QUOTED;
			else
				client->flags &= ~DCC_QUOTED;
		} else if (!my_strnicmp(listc, "FULL_LINE_BUFFER", len)) {
			long	buffered;

			GET_INT_ARG(buffered, input);
			if (buffered)
				client->full_line_buffer = 1;
			else
				client->full_line_buffer = 0;
		} else if (!my_strnicmp(listc, "PACKET_SIZE", len)) {
			long	packet_size;

			/* Validation? */
			GET_INT_ARG(packet_size, input);
			client->packet_size = packet_size;
		} else if (!my_strnicmp(listc, "OFFERADDR", len)) {
			char *host, *port;
			SSu a;

			GET_FUNC_ARG(host, input);
			GET_FUNC_ARG(port, input);

			a.sa.sa_family = AF_UNSPEC;
			if ((client->flags & DCC_ACTIVE) ||
					inet_strton(host, port, &a, AI_ADDRCONFIG))
				RETURN_EMPTY;

			memcpy(&client->offer.ss, &a.ss, sizeof(a.ss));
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "UPDATES_STATUS", len)) {
			long	updates;

			GET_INT_ARG(updates, input);
			client->updates_status = updates;
			RETURN_INT(1);
		} else {
			RETURN_EMPTY;
		}
		RETURN_INT(1);
	} else if (!my_strnicmp(listc, "TYPEMATCH", len)) {
		for (client = ClientList; client; client = client->next)
			if (wild_match(input, dcc_types[client->flags & DCC_TYPES]))
				malloc_strcat_word_c(&retval, space, ltoa(client->refnum), DWORD_NO, &clue);
	} else if (!my_strnicmp(listc, "DESCMATCH", len)) {
		for (client = ClientList; client; client = client->next)
			if (wild_match(input, client->description ? client->description : EMPTY))
				malloc_strcat_word_c(&retval, space, ltoa(client->refnum), DWORD_NO, &clue);
	} else if (!my_strnicmp(listc, "FILEMATCH", len)) {
		for (client = ClientList; client; client = client->next)
			if (wild_match(input, client->local_filename ? client->local_filename : EMPTY))
				malloc_strcat_word_c(&retval, space, ltoa(client->refnum), DWORD_NO, &clue);
	} else if (!my_strnicmp(listc, "USERMATCH", len)) {
		for (client = ClientList; client; client = client->next)
			if (wild_match(input, client->user ? client->user : EMPTY))
				malloc_strcat_word_c(&retval, space, ltoa(client->refnum), DWORD_NO, &clue);
	} else if (!my_strnicmp(listc, "USERHOSTMATCH", len)) {
		for (client = ClientList; client; client = client->next)
			if (wild_match(input, client->userhost ? client->userhost : EMPTY))
				malloc_strcat_word_c(&retval, space, ltoa(client->refnum), DWORD_NO, &clue);
	} else if (!my_strnicmp(listc, "OTHERMATCH", len)) {
		for (client = ClientList; client; client = client->next)
			if (wild_match(input, client->othername ? client->othername : EMPTY))
				malloc_strcat_word_c(&retval, space, ltoa(client->refnum), DWORD_NO, &clue);
	} else if (!my_strnicmp(listc, "LOCKED", len)) {
		for (client = ClientList; client; client = client->next)
			if (client->locked)
				malloc_strcat_word_c(&retval, space, ltoa(client->refnum), DWORD_NO, &clue);
	} else if (!my_strnicmp(listc, "HELD", len)) {
		for (client = ClientList; client; client = client->next)
			if (client->held)
				malloc_strcat_word_c(&retval, space, ltoa(client->refnum), DWORD_NO, &clue);
	} else if (!my_strnicmp(listc, "UNHELD", len)) {
		for (client = ClientList; client; client = client->next)
			if (!client->held)
				malloc_strcat_word_c(&retval, space, ltoa(client->refnum), DWORD_NO, &clue);
	} else if (!my_strnicmp(listc, "READABLES", len)) {
		for (client = ClientList; client; client = client->next)
		{
			if (my_isreadable(client->socket, 0))
				malloc_strcat_word_c(&retval, space, 
					ltoa(client->refnum), DWORD_NO, &clue);
		}
	} else if (!my_strnicmp(listc, "WRITABLES", len)) {
		for (client = ClientList; client; client = client->next)
		{
			if (my_iswritable(client->socket, 0))
				malloc_strcat_word_c(&retval, space, 
					ltoa(client->refnum), DWORD_NO, &clue);
		}
	} else if (!my_strnicmp(listc, "UPDATES_STATUS", len)) {
		int	oldval;

		oldval = dcc_updates_status;
		if (input && *input) {
			int	newval;
			GET_INT_ARG(newval, input);
			dcc_updates_status = newval;
		}
		RETURN_INT(oldval);
	} else if (!my_strnicmp(listc, "DEFAULT_PORT", len)) {
		if (empty(input))
			RETURN_STR(default_dcc_port);
		else
			malloc_strcpy(&default_dcc_port, input);
		RETURN_INT(1);
	} else if (!my_strnicmp(listc, "FD_TO_REFNUM", len)) {
		int	fd;
		GET_INT_ARG(fd, input);
		for (client = ClientList; client; client = client->next) {
			if (client->socket == fd)
				RETURN_INT(client->refnum);
		}
		RETURN_INT(-1);
	} else
		RETURN_EMPTY;

	RETURN_MSTR(retval);
}

#if 0
void    help_topics_dcc (FILE *f)
{
        int     x;

        for (x = 0; dcc_commands[x].name; x++)
                fprintf(f, "dcc %s\n", dcc_commands[x].name);
}
#endif

