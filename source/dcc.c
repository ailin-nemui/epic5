/* $EPIC: dcc.c,v 1.75 2003/10/12 03:41:00 jnelson Exp $ */
/*
 * dcc.c: Things dealing client to client connections. 
 *
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1995, 2003 EPIC Software Labs
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
#include "term.h"
#include "notice.h"

#define DCC_BLOCK_SIZE	BIG_BUFFER_SIZE

/* These are the settings for ``flags'' */
#define DCC_CHAT	((unsigned) 0x0001)
#define DCC_FILEOFFER	((unsigned) 0x0002)
#define DCC_FILEREAD	((unsigned) 0x0003)
#define	DCC_RAW		((unsigned) 0x0004)
#define	DCC_RAW_LISTEN	((unsigned) 0x0005)
#define DCC_TYPES	((unsigned) 0x000f)
#define DCC_MY_OFFER	((unsigned) 0x0010)
#define DCC_ACTIVE	((unsigned) 0x0020)
#define DCC_THEIR_OFFER	((unsigned) 0x0040)
#define DCC_DELETE	((unsigned) 0x0080)
#define DCC_TWOCLIENTS	((unsigned) 0x0100)
#define DCC_REJECTED	((unsigned) 0x0200)
#define DCC_STATES	((unsigned) 0xfff0)

typedef	struct	DCC_struct
{
	unsigned	flags;
	int		family;
	int		locked;			/* XXX - Sigh */
	int		socket;
	int		file;
	int		held;
	long		refnum;
	u_32int_t	filesize;
	char *		description;
	char *		filename;
	char *		user;
	char *		userhost;
	char *		othername;
struct	DCC_struct *	next;

	SS		offer;			/* Their offer */
	SS		peer_sockaddr;		/* Their saddr */
	SS		local_sockaddr;		/* Our saddr */
	unsigned short	want_port;		/* HOST ORDER */

	u_32int_t	bytes_read;
	u_32int_t	bytes_sent;
	int		window_sent;
	int		window_max;
	Timeval		lasttime;
	Timeval		starttime;
	Timeval		holdtime;
	double		heldtime;
	u_32int_t	packets_total;
	u_32int_t	packets_transfer;
	u_32int_t	packets_ack;
	u_32int_t	packets_outstanding;	/* Not unsigned! */
	u_32int_t	resume_size;

	int		(*open_callback) (struct DCC_struct *);
	int		server;
}	DCC_list;

static	DCC_list *	ClientList = NULL;
static	char		DCC_current_transfer_buffer[256];
	time_t		dcc_timeout = 600;		/* Backed by a /set */
static	int		dcc_global_lock = 0;
static	int		dccs_rejected = 0;
static	int		dcc_refnum = 1;

static	void		dcc_chat 		(char *);
static	void 		dcc_close 		(char *);
static	void		dcc_closeall		(char *);
static	void		dcc_erase 		(DCC_list *);
static	void		dcc_filesend 		(char *);
static	void		dcc_getfile_get 	(char *);
static	int		dcc_open 		(DCC_list *);
static	int		dcc_connected		(int, int);
static	void 		dcc_garbage_collect 	(void);
static	void		dcc_rename 		(char *);
static	DCC_list *	dcc_searchlist 		(unsigned, const char *, const char *, const char *, int);
static	void		dcc_send_raw 		(char *);
static	void 		output_reject_ctcp 	(int, char *, char *);
static	void		process_dcc_chat	(DCC_list *);
static	void		process_incoming_listen (DCC_list *);
static	void		process_incoming_raw 	(DCC_list *);
static	void		process_dcc_send 	(DCC_list *);
static	void		process_incoming_file 	(DCC_list *);
static	void		DCC_close_filesend 	(DCC_list *, const char *);
static	void		update_transfer_buffer 	(const char *format, ...);
static 	void		dcc_send_booster_ctcp 	(DCC_list *dcc);
static	char *		dcc_urlencode		(const char *);
static	char *		dcc_urldecode		(const char *);
static	void		dcc_list 		(char *args);

#ifdef MIRC_BROKEN_DCC_RESUME
static	void		dcc_getfile_resume 	    (char *);
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
	{ "GET",	dcc_getfile_get 	},	/* DCC_FILEREAD */
	{ "RAW",	dcc_send_raw 		},	/* DCC_RAW */

	{ "CLOSE",	dcc_close 		},
	{ "CLOSEALL",	dcc_closeall		},
	{ "LIST",	dcc_list 		},
	{ "RENAME",	dcc_rename 		},
#ifdef MIRC_BROKEN_DCC_RESUME
	{ "RESUME",	dcc_getfile_resume 	},
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


/*
 * remove_from_dcc_list: What do you think it does?
 */
static void	dcc_remove_from_list (DCC_list *erased)
{
	DCC_list *prev = NULL;

	if (erased != ClientList)
	{
		for (prev = ClientList; prev; prev = prev->next)
			if (prev->next == erased)
				break;
	}

	if (erased == ClientList)
		ClientList = erased->next;
	else if (prev)
		prev->next = erased->next;
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
		return;

	dcc = ClientList;
	while (dcc)
	{
		if ((dcc->flags & DCC_DELETE) && dcc->locked == 0)
		{
			need_update = 1;
			dcc_erase(dcc);
			dcc = ClientList;	/* Start over */
		}
		else
			dcc = dcc->next;
	}

	if (need_update)
	{
		update_transfer_buffer(NULL);	/* Whatever */
		update_all_status();
	}
}

/*
 * Note that 'erased' does not neccesarily have to be on ClientList.
 * In fact, it may very well NOT be on ClientList.  The handling at the
 * beginning of the function is only to sanity check that it isnt on 
 * ClientList when we blow it away.
 */
static 	void		dcc_erase (DCC_list *erased)
{
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

#if 0
		if (*(erased->user) == '=')
			dummy_nick = erased->user + 1;
		else
			dummy_nick = erased->user;
#endif
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
	erased->file = new_close(erased->file);
	new_free(&erased->description);	/* Magic check failure here */
	new_free(&erased->filename);
	new_free(&erased->user);
	new_free(&erased->othername);
	new_free((char **)&erased);
}

/*
 * close_all_dcc:  We call this when we create a new process so that
 * we don't leave any fd's lying around, that won't close when we
 * want them to..
 */
void 	close_all_dcc (void)
{
	DCC_list *dcc;

	dccs_rejected = 0;

	while ((dcc = ClientList))
		dcc_erase(dcc);

	if (dccs_rejected)
	{
		message_from(NULL, LOG_DCC);
		say("Waiting for DCC REJECTs to be sent");
		sleep(1);
		message_from(NULL, LOG_CRAP);
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
	if (dcc)
		dcc->locked++;
	dcc_global_lock++;
	return 0;
}

static int	unlock_dcc (DCC_list *dcc)
{
	if (dcc)
		dcc->locked--;
	dcc_global_lock--;

	if (dcc_global_lock == 0)
		dcc_garbage_collect();		/* XXX Maybe unnecessary */
	return 0;
}



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
	off_t		filesize)
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
	new_client->filename 		= NULL;
	new_client->packets_total 	= filesize ? 
					  (filesize / DCC_BLOCK_SIZE + 1) : 0;
	new_client->packets_transfer 	= 0;
	new_client->packets_outstanding = 0;
	new_client->packets_ack 	= 0;
	new_client->next 		= ClientList;
	new_client->user 		= malloc_strdup(user);
	new_client->userhost 		= (FromUserHost && *FromUserHost)
					? malloc_strdup(FromUserHost)
					: malloc_strdup(unknown_userhost);
	new_client->description 	= malloc_strdup(description);
	new_client->othername 		= malloc_strdup(othername);
	new_client->bytes_read 		= 0;
	new_client->bytes_sent 		= 0;
	new_client->starttime.tv_sec 	= 0;
	new_client->starttime.tv_usec 	= 0;
	new_client->holdtime.tv_sec 	= 0;
	new_client->holdtime.tv_usec 	= 0;
	new_client->heldtime		= 0.0;
	new_client->window_max 		= 0;
	new_client->window_sent 	= 0;
	new_client->want_port 		= 0;
	new_client->resume_size		= 0;
	new_client->open_callback	= NULL;
	new_client->refnum		= dcc_refnum++;
	new_client->server		= from_server;
	get_time(&new_client->lasttime);

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
 * Added by Chaos: Is used in edit.c for checking redirect.
 */
int	dcc_chat_active (const char *user)
{
	int	retval;

	message_from(NULL, LOG_DCC);
	retval = dcc_searchlist(DCC_CHAT, user, NULL, NULL, 1) ? 1 : 0;
	message_from(NULL, LOG_CRAP);
	return retval;
}


/*
 * This is called when a client connection completes (either through
 * success or failure, which is indicated in 'result')
 */
static int	dcc_connected (int fd, int result)
{
	DCC_list *	dcc;
	socklen_t	len;
	const char *	type;
	int		jvs_blah;

	if (!(dcc = get_dcc_by_filedesc(fd)))
		return -1;	/* Don't want it */

	type = dcc_types[dcc->flags & DCC_TYPES];

	len = sizeof(dcc->local_sockaddr);
	getsockname(dcc->socket, (SA *)&dcc->local_sockaddr, &len);

	len = sizeof(dcc->peer_sockaddr);
	getpeername(dcc->socket, (SA *)&dcc->peer_sockaddr, &len);

	/*
	 * Set up the connection to be useful
	 */
	new_open(dcc->socket);
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
		SA *addr = (SA *)&dcc->peer_sockaddr;

		if (inet_ntostr(addr, p_addr, 256, p_port, 24, NI_NUMERICHOST))
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
					"%s %s %s %s %s %s", 
					dcc->user, type, p_addr, p_port,
					encoded_description,
					ltoa(dcc->filesize))))
			    /*
			     * Compatability with bitchx
			     */
			jvs_blah = do_hook(DCC_CONNECT_LIST,
					"%s GET %s %s %s %s", 
					dcc->user, p_addr, p_port,
					encoded_description,
					ltoa(dcc->filesize));
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
	if (dcc->open_callback)
		dcc->open_callback(dcc);
	return 0;		/* Going to keep it. */
}

/*
 * Whenever a DCC changes state from WAITING->ACTIVE, it calls this function
 * to initiate the internet connection for the transaction.
 */
static	int	dcc_open (DCC_list *dcc)
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
	    /*
	     * DCC GET or DCC CHAT -- accept someone else's offer.
	     */
	    if (dcc->flags & DCC_THEIR_OFFER)
	    {
		lock_dcc(dcc);
		if ((dcc->socket = client_connect(NULL, 0, (SA *)&dcc->offer, 
						  sizeof(dcc->offer))) < 0)
		{
			dcc->flags |= DCC_DELETE;
			say("Unable to create connection: (%d) [%d] %s", 
				dcc->socket, errno, 
				my_strerror(dcc->socket, errno));

			unlock_dcc(dcc);
			retval = -1;
			break;
		}
		unlock_dcc(dcc);

		dcc_connected(dcc->socket, 0);
		from_server = old_server;
		break;
	    }

	    /*
	     * DCC SEND or DCC CHAT -- make someone an offer they can't refuse.
	     */
	    else
	    {
		/*
		 * Mark that we're waiting for the remote peer to answer,
		 * and then open up a listen()ing socket for them.  If our
		 * first choice of port fails, try another one.  If both
		 * fail, then we give up.  If the user insists on doing
		 * random ports, then we will fallback to asking the system
		 * for a port if our random port isnt available.
		 */
		dcc->flags |= DCC_MY_OFFER;
		if ((dcc->socket = ip_bindery(dcc->family, dcc->want_port, 
					      &dcc->local_sockaddr)) < 0)
		{
			dcc->flags |= DCC_DELETE;
			say("Unable to create connection [%d]: %s", 
				dcc->socket, 
				my_strerror(dcc->socket, errno));
			retval = -1;
			break;
		}

#ifdef MIRC_BROKEN_DCC_RESUME
		/*
		 * For stupid MIRC dcc resumes, we need to stash the
		 * local port number, because the remote client will send
		 * back that port number as its ID of what file it wants
		 * to resume (rather than the filename. ick.)
		 */
		inet_ntostr((SA *)&dcc->local_sockaddr, NULL, 0, p_port, 12, 0);
		malloc_strcpy(&dcc->othername, p_port);
#endif
		new_open(dcc->socket);

		/*
		 * If this is to be a 2-peer connection, then we need to
		 * send the remote peer a CTCP request.  I suppose we should
		 * do an ISON request first, but thats another project.
		 */
		if (dcc->flags & DCC_TWOCLIENTS)
			dcc_send_booster_ctcp(dcc);
	    }
	}
	while (0);

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
	SS	my_sockaddr;
	char	p_host[128];
	char	p_port[24];
	char *	nopath;
	const char *	type = dcc_types[dcc->flags & DCC_TYPES];
	int	family;
	int	server = from_server < 0 ? primary_server : from_server;

	if (!is_server_registered(server))
	{
		yell("You cannot use DCC while not connected to a server.");
		return;
	}

	family = FAMILY(dcc->local_sockaddr);

	if (get_int_var(DCC_USE_GATEWAY_ADDR_VAR))
		my_sockaddr = get_server_uh_addr(server);
	else if (family == AF_INET && V4ADDR(dcc->local_sockaddr).s_addr == htonl(INADDR_ANY))
		my_sockaddr = get_server_local_addr(server);
#ifdef INET6
	else if (family == AF_INET6 && memcmp(&V6ADDR(dcc->local_sockaddr), &in6addr_any, sizeof(in6addr_any)) == 0)
		my_sockaddr = get_server_local_addr(server);
#endif
	else
		my_sockaddr = dcc->local_sockaddr;

	if (family == AF_INET)
		V4PORT(my_sockaddr) = V4PORT(dcc->local_sockaddr);
#ifdef INET6
	else if (family == AF_INET6)
		V6PORT(my_sockaddr) = V6PORT(dcc->local_sockaddr);
#endif
	else
	{
		yell("Could not send a CTCP handshake becuase the address family is not supported.");
		return;
	}

	if (inet_ntostr((SA *)&my_sockaddr, p_host, 128, p_port, 24, GNI_INTEGER | NI_NUMERICHOST)) {
		yell("Couldn't get host/port for address!");
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
		send_ctcp(CTCP_PRIVMSG, dcc->user, CTCP_DCC,
			 "%s %s %s %s %ld",
			 type, url_name, p_host, p_port,
			 dcc->filesize);

		/*
		 * Tell the user we sent out the request
		 */
		if (do_hook(DCC_OFFER_LIST, "%s %s %s %s", 
			dcc->user, type, url_name, ltoa(dcc->filesize)))
		    say("Sent DCC %s request (%s %s) to %s", 
			type, nopath, ltoa(dcc->filesize), dcc->user);

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
		send_ctcp(CTCP_PRIVMSG, dcc->user, CTCP_DCC,
			 "%s %s %s %s", 
			 type, nopath, p_host, p_port);

		/*
		 * And tell the user
		 */
		if (do_hook(DCC_OFFER_LIST, "%s %s", dcc->user, type))
		    say("Sent DCC %s request to %s", type, dcc->user);
	}
	unlock_dcc(dcc);
}


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
	if (*text == CTCP_DELIM_CHAR && strncmp(text + 1, "ACTION", 6))
	{
		if (!cmd || !strcmp(cmd, "PRIVMSG"))
			strlcpy(tmp, "CTCP_MESSAGE ", sizeof tmp);
		else
			strlcpy(tmp, "CTCP_REPLY ", sizeof tmp);
	}

	strlcat(tmp, text, sizeof tmp);
	strlcat(tmp, "\n", sizeof tmp);

	if (x_debug & DEBUG_OUTBOUND) 
		yell("-> [%s] [%s]", desc, tmp);

	if (write(dcc->socket, tmp, strlen(tmp)) == -1)
	{
		dcc->flags |= DCC_DELETE;
		say("Outbound write() failed: %s", 
			errno ? strerror(errno) : 
			"What the heck is wrong with your disk?");

		from_server = old_from_server;
		return;
	}

	dcc->bytes_sent += strlen(tmp);

	if (noisy)
	{
		lock_dcc(dcc);
		if (do_hook(list, "%s %s", dcc->user, text_display))
			put_it("=> %c%s%c %s", 
				thing, dcc->user, thing, text_display);
		unlock_dcc(dcc);
	}

	return;
}

/*
 * This is used to send a message to a remote DCC peer.  This is called
 * by send_text.
 */
void	dcc_chat_transmit (char *user, char *text, const char *orig, const char *type, int noisy)
{
	int	fd;

    do
    {
	/*
	 * This converts a message being sent to a number into whatever
	 * its local port is (which is what we think the nickname is).
	 * Its just a 15 minute hack. dont read too much into it.
	 */
	if ((fd = atol(user)))
	{
		DCC_list *	dcc;

		if (!(dcc = get_dcc_by_filedesc(fd)))
		{
			message_from(NULL, LOG_DCC);
			put_it("Descriptor %d is not an open DCC RAW", fd);
			break;
		}

		message_from(dcc->user, LOG_DCC);
		dcc_message_transmit(DCC_RAW, dcc->user, dcc->description, 
					text, orig, noisy, type);
	}
	else
	{
		message_from(user, LOG_DCC);
		dcc_message_transmit(DCC_CHAT, user, NULL,
					text, orig, noisy, type);
	}
    }
    while (0);

	dcc_garbage_collect();
	message_from(NULL, LOG_CRAP);
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
	int	i;

	if (!(cmd = next_arg(args, &args)))
		cmd = "LIST";

	for (i = 0; dcc_commands[i].name != NULL; i++)
	{
		if (!my_stricmp(dcc_commands[i].name, cmd))
		{
			message_from(NULL, LOG_DCC);
			lock_dcc(NULL);
			dcc_commands[i].function(args);
			unlock_dcc(NULL);
			message_from(NULL, LOG_CRAP);

			dcc_garbage_collect();
			return;
		}
	}

	message_from(NULL, LOG_DCC);
	say("Unknown DCC command: %s", cmd);
	message_from(NULL, LOG_CRAP);
}


/*
 * Usage: /DCC CHAT <nick> [-p port]|[-6]|[-4]
 */
static void	dcc_chat (char *args)
{
	char		*user;
	DCC_list	*dcc;
	unsigned short	portnum = 0;
	int		family;

	if ((user = next_arg(args, &args)) == NULL)
	{
		say("You must supply a nickname for DCC CHAT");
		return;
	}

	/* The default is AF_INET */
	family = AF_INET;

	/*
	 * Check to see if there is a flag
	 */
	while (*args == '-')
	{
		if (args[1] == 'p')
		{
			next_arg(args, &args);
			if (args && *args)
			    portnum = my_atol(next_arg(args, &args));
		}
#ifdef INET6
		else if (args[1] == '6')
		{
			next_arg(args, &args);
			family = AF_INET6;
		}
#endif
		else if (args[1] == '4')
		{
			next_arg(args, &args);
			family = AF_INET;
		}
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
	dcc_open(dcc);
}

/*
 * Usage: /DCC CLOSE <type> <nick>
 */
static	void 	dcc_close (char *args)
{
	DCC_list	*dcc;
	char		*type;
	char		*user;
	char		*file;
	char		*encoded_description = NULL;
	int		any_type = 0;
	int		any_user = 0;
	int		i;
	int		count = 0;

	type = next_arg(args, &args);
	user = next_arg(args, &args);
	file = new_next_arg(args, &args);

	if (type && (!my_stricmp(type, "-all") || !my_stricmp(type, "*")))
		any_type = 1;
	if (user && (!my_stricmp(user, "-all") || !my_stricmp(type, "*")))
		any_user = 1;

	if (!type || (!user && !any_type))
	{
		say("You must specify a type and a nick for DCC CLOSE");
		return;
	}

	if (any_type == 0)	/* User did not specify "-all" type */
	{
		for (i = 0; dcc_types[i]; i++)
			if (!my_stricmp(type, dcc_types[i]))
				break;

		if (!dcc_types[i])
		{
			say("Unknown DCC type: %s", type);
			return;
		}
	}
	else			/* User did specify "-all" type */
		i = -1;

	if (any_user)		/* User did specify "-all" user */
		user = NULL;

	while ((dcc = dcc_searchlist(i, user, file, file, -1)))
	{
		unsigned	my_type = dcc->flags & DCC_TYPES;

		count++;
		dcc->flags |= DCC_DELETE;

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
			
		unlock_dcc(dcc);
	}

	if (!count)
		say("No DCC %s:%s to %s found", 
			(i == -1 ? "<any>" : type), 
			(file ? file : "<any>"), 
			user);
}

/*
 * Usage: /DCC CLOSEALL
 * It leaves your DCC list very empty
 */
static void	dcc_closeall (char *args)
{
	const char	*my_local_args = "-all -all";
	char	*breakage = LOCAL_COPY(my_local_args);
	dcc_close(breakage);	/* XXX Bleh */
}

/*
 * Usage: /DCC GET <nick> [file|*]
 * The '*' file gets all offered files.
 */
static	void	dcc_getfile (char *args, int resume)
{
	char		*user;
	char		*filename = NULL;
	DCC_list	*dcc;
	Filename	fullname = "";
	Filename	pathname = "";
	int		file;
	char 		*realfilename = NULL;
	int		get_all = 0;
	int		count = 0;
	Stat		sb;
	int		proto;

	if (!(user = next_arg(args, &args)))
	{
		say("You must supply a nickname for DCC GET");
		return;
	}

	filename = next_arg(args, &args);
	if (filename && *filename && !strcmp(filename, "*"))
		get_all = 1, filename = NULL;

	while ((dcc = dcc_searchlist(DCC_FILEREAD, user, filename, NULL, 0)))
	{
		count++;

		if (!get_all && dcc->flags & DCC_ACTIVE)
		{
			say("A previous DCC GET:%s to %s exists", 
				filename ? filename : "<any>", user);
			return;
		}
		if (!get_all && !(dcc->flags & DCC_THEIR_OFFER))
		{
			say("I'm a little teapot");
			dcc->flags |= DCC_DELETE;
			return;
		}

		if (get_string_var(DCC_STORE_PATH_VAR))
		{
			strlcpy(pathname, get_string_var(DCC_STORE_PATH_VAR), 
						sizeof(pathname));
		} else /* SUSv2 doesn't specify realpath() behavior for "" */
			strcpy(pathname, "./");

		if (*pathname && normalize_filename(pathname, fullname))
		{
			say("%s is not a valid directory", fullname);
			return;
		}

		if (fullname && *fullname)
			strlcat(fullname, "/", sizeof(fullname));

		realfilename = dcc_urldecode(dcc->description);

		/* 
		 * Don't prefix the DCC_STORE_PATH if the description
		 * is already an absolute path (hop, 09/24/2003)
		 */
		if (*realfilename == '/')
			strlcpy(fullname, realfilename, sizeof(fullname));
		else
			strlcat(fullname, realfilename, sizeof(fullname));
		new_free(&realfilename);

#ifdef MIRC_BROKEN_DCC_RESUME
		if (resume && get_int_var(MIRC_BROKEN_DCC_RESUME_VAR) && stat(fullname, &sb) != -1) {
			dcc->bytes_sent = 0;
			dcc->bytes_read = dcc->resume_size = sb.st_size;
		
			if (((SA *)&dcc->offer)->sa_family == AF_INET)
				malloc_strcpy(&dcc->othername, 
						ltoa(ntohs(V4PORT(dcc->offer))));
		
			if (x_debug & DEBUG_DCC_XMIT)
				yell("SENDING DCC RESUME to [%s] [%s|%s|%ld]", user, filename, dcc->othername, (long)sb.st_size);
		
			/* Just in case we have to fool the protocol enforcement. */
			proto = get_server_protocol_state(from_server);
			set_server_protocol_state(from_server, 0);
			send_ctcp(CTCP_PRIVMSG, user, CTCP_DCC, "RESUME %s %s %ld", 
#if 1
				dcc->description,
#else
				"file.ext",  /* This is just for testing. */
#endif
				dcc->othername, (long)sb.st_size);
			set_server_protocol_state(from_server, proto);

			/*
			 * Warning:  It seems to be for the best to _not_ loop
			 *           at this point.
			 */
			return;
		}
#endif
		
		if ((file = open(fullname, 
				O_WRONLY|O_TRUNC|O_CREAT, 0644)) == -1)
		{
			say("Unable to open %s: %s", 
				fullname, errno ? 
					strerror(errno) : 
					"<No Error>");
			return;
		}

		dcc->filename = malloc_strdup(fullname);
		dcc->file = file;
		dcc->flags |= DCC_TWOCLIENTS;
		dcc->open_callback = NULL;
		if (dcc_open(dcc))
		{
			if (get_all)
				continue;
			else
				return;
		}

		if (!get_all)
			break;
	}

	if (!count)
	{
		if (filename)
			say("No file (%s) offered in SEND mode by %s", 
					filename, user);
		else
			say("No file offered in SEND mode by %s", user);
		return;
	}
}
static void dcc_getfile_get	(char *args) { dcc_getfile(args, 0); }
#ifdef MIRC_BROKEN_DCC_RESUME
static void dcc_getfile_resume	(char *args) { dcc_getfile(args, 1); }
#endif

/*
 * Calculates transfer speed based on size, start time, and current time.
 */
static char *	calc_speed (u_32int_t sofar, Timeval sta, Timeval cur)
{
	static char	buf[7];
	double		tdiff = time_diff(sta, cur);

	if (sofar == 0 || tdiff <= 0.0)
		snprintf(buf, sizeof(buf), "N/A");
	else
		snprintf(buf, sizeof(buf), "%4.1f", 
				(double)sofar / tdiff / 1024.0);
	return buf;
}

/*
 * Packs a file size into a smaller representation of Kb, Mb, or Gb.
 * I'm sure it can be done less kludgy.
 */
static char *	calc_size (u_32int_t fsize)
{
	static char	buf[8];

	if (fsize < 1 << 10)
		snprintf(buf, sizeof buf, "%ld", (long)fsize);
	else if (fsize < 1 << 20)
		snprintf(buf, sizeof buf, "%3.1fKb", (float)fsize / (1 << 10));
	else if (fsize < 1 << 30)
		snprintf(buf, sizeof buf, "%3.1fMb", (float)fsize / (1 << 20));
	else
		snprintf(buf, sizeof buf, "%3.1fGb", (float)fsize / (1 << 30));

	return buf;
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

	    if (do_hook(DCC_LIST_LIST, "%s %s %s %s %ld %ld %ld %s",
				dcc_types[flags & DCC_TYPES],
				zero,			/* No encryption */
				dcc->user,
					flags & DCC_DELETE       ? "Closed"  :
					flags & DCC_ACTIVE       ? "Active"  :
					flags & DCC_MY_OFFER     ? "Waiting" :
					flags & DCC_THEIR_OFFER  ? "Offered" :
							           "Unknown",
				dcc->starttime.tv_sec,
			  (long)dcc->filesize,
				dcc->bytes_sent ? (unsigned long)dcc->bytes_sent
						   : (unsigned long)dcc->bytes_read,
				encoded_description))
	    {
		char *	filename = strrchr(dcc->description, '/');
		char	completed[9];
		char	size[9];
		char	speed[9];
		char	buf[23];
		char *	time_f;
		u_32int_t	tot_size;
		u_32int_t	act_sent;

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
			strlcpy(size, calc_size(dcc->filesize), sizeof size);
		}
		else
		{
			strlcpy(completed, calc_size(tot_size), sizeof completed);
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
}


/*
 * Usage: /DCC RAW <filedesc> <host> [text]
 */
static	void	dcc_send_raw (char *args)
{
	char	*name;
	char	*host;

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

	message_from(name, LOG_DCC);
	dcc_message_transmit(DCC_RAW, name, host, args, args, 1, NULL);
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
#ifdef INET6
			else if (this_arg[1] == '6')
				family = AF_INET6;
#endif
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
		dcc_open(Client);
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
	char *	  PortName = empty_string;

	lock_dcc(NULL);
	message_from(NULL, LOG_DCC);

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
	if (dcc_open(Client))
		break;

	get_time(&Client->starttime);
	Client->flags |= DCC_ACTIVE;
	Client->user = malloc_strdup(ltoa(Client->want_port));
	unlock_dcc(Client);
    }
    while (0);

	unlock_dcc(NULL);
	dcc_garbage_collect();
	message_from(NULL, LOG_CRAP);
	return malloc_strdup(PortName);
}


/*
 * Usage: $connect(<hostname> <portnum> <family>)
 */
char	*dcc_raw_connect (const char *host, const char *port, int family)
{
	DCC_list *	Client = NULL;
	SS		my_sockaddr;
	char *		retval = empty_string;

	lock_dcc(NULL);
	message_from(NULL, LOG_DCC);

    do
    {
	memset(&my_sockaddr, 0, sizeof(my_sockaddr));
	FAMILY(my_sockaddr) = family;
	if (inet_strton(host, port, (SA *)&my_sockaddr, 0))
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
	Client->offer = my_sockaddr;
	Client->flags = DCC_THEIR_OFFER | DCC_RAW;
	if (dcc_open(Client))
	{
		unlock_dcc(Client);
		break;
	}

	Client->user = malloc_strdup(ltoa(Client->socket));
	if (do_hook(DCC_RAW_LIST, "%s %s E %s", Client->user, host, port))
            if (do_hook(DCC_CONNECT_LIST,"%s RAW %s %s", 
				Client->user, host, port))
		say("DCC RAW connection to %s on %s via %s established", 
				host, Client->user, port);
	retval = LOCAL_COPY(Client->user);
	unlock_dcc(Client);
    }
    while (0);

	unlock_dcc(NULL);
	message_from(NULL, LOG_CRAP);
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
	SS		offer;
	off_t		filesize;

	/* 
	 * Ensure that nobody will mess around with us while we're working.
	 */
	lock_dcc(NULL);
	message_from(NULL, LOG_DCC);

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
		filesize = my_atol(size);
	else
		filesize = 0;

	/*
	 * Figure out if it's a type of offer we support.
	 */
	if (!my_stricmp(type, "CHAT"))
		dtype = DCC_CHAT;
	else if (!my_stricmp(type, "SEND"))
		dtype = DCC_FILEREAD;
#ifdef MIRC_BROKEN_DCC_RESUME
	else if (!my_stricmp(type, "RESUME"))
	{
		/* 
		 * Dont be deceieved by the arguments we're passing it.
		 * The arguments are "out of order" because MIRC doesnt
		 * send them in the traditional order.  Ugh.
		 */
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
	memset(&offer, 0, sizeof(offer));
	V4FAM(offer) = AF_UNSPEC;
	if ((err = inet_strton(address, port, (SA *)&offer, AI_NUMERICHOST)))
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
	if (inet_ntostr((SA *)&offer, p_addr, 256, NULL, 0, NI_NUMERICHOST))
	{
		say("DCC %s (%s) request from %s could not be converted back "
		    "into a p-addr [%s] [%s]", 
			type, description, user, address, port);
		break;
	}

	/*
	 * Check for invalid or illegal IP addresses.
	 */
	if (FAMILY(offer) == AF_INET)
	{
	    if (V4ADDR(offer).s_addr == 0)
	    {
		yell("### DCC handshake from %s ignored becuase it had "
				"an null address", user);
		break;
	    }
	}
#ifdef INET6
	else if (FAMILY(offer) == AF_INET6)
	{
		/* Reserved for future expansion */
	}
#endif

#ifdef HACKED_DCC_WARNING
	/*
	 * Check for hacked (forged) IP addresses.  A handshake is considered
	 * forged if the address in the handshake is not the same address that
	 * the user is using on irc.  This is not turned on by default because
	 * it can make epic block on dns lookups, which rogue users can use
	 * to make your life painful, and also because a lot of networks are
	 * using faked hostnames on irc, which makes this a waste of time.
	 */
	if (FAMILY(offer) == AF_INET)
	{
		char *	fromhost;
		SS	irc_addr;

		if (!(fromhost = strchr(FromUserHost, '@')))
		{
			yell("### Incoming handshake from a non-user peer!");
			break;
		}

		fromhost++;
		FAMILY(irc_addr) = FAMILY(offer);
		if (inet_strton(fromhost, port, (SA *)&irc_addr, 0))
		{
			yell("### Incoming handshake has an address or port "
				"[%s:%s] that could not be figured out!", 
				fromhost, port);
			yell("### Please use caution in deciding whether to "
				"accept it or not");
		}
		else if (FAMILY(offer) == AF_INET)
		{
		   if (V4ADDR(irc_addr).s_addr != V4ADDR(offer).s_addr)
		   {
			say("WARNING: Fake dcc handshake detected! [%x]", 
				V4ADDR(offer).s_addr);
			say("Unless you know where this dcc request is "
				"coming from");
			say("It is recommended you ignore it!");
		   }
		}
	}
#ifdef INET6
	else if (FAMILY(offer) == AF_INET6)
	{
		/* Reserved for future expansion */
	}
#endif
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
		    dcc_create(dtype, user, "chat", NULL, FAMILY(offer), filesize);
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
		    send_ctcp(CTCP_NOTICE, user, CTCP_DCC,
			"DCC %s collision occured while connecting to %s (%s)", 
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
					FAMILY(offer), filesize);

	/*
	 * Otherwise, we're good to go!  Mark this dcc as being something
	 * they offered to us, and copy over the port and address.
	 */
	dcc->flags |= DCC_THEIR_OFFER;
	memcpy(&dcc->offer, &offer, sizeof offer);

	/* 
	 * DCC SEND and CHAT have different arguments, so they can't
	 * very well use the exact same hooked data.  Both now are
	 * identical for $0-4, and SEND adds filename/size in $5-6 
	 */
	lock_dcc(dcc);
	if ((dcc->flags & DCC_TYPES) == DCC_FILEREAD)
	{
		if (do_hook(DCC_REQUEST_LIST, "%s %s %s %s %s %s %s",
				  user, type, description, p_addr, port,
				  dcc->description, ltoa(dcc->filesize)))
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

		if (statit.st_size < dcc->filesize)
		{
		    say("WARNING: File [%s] exists but is smaller than "
			"the file offered.", realfilename);
		    xclose = resume = ren = get = 1;
		}
		else if (statit.st_size == dcc->filesize)
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
		say("DCC %s (%s %s) request received from %s!%s [%s:%s]",
			type, description, ltoa(dcc->filesize), user, 
			FromUserHost, p_addr, port);
	else
		say("DCC %s (%s) request received from %s!%s [%s:%s]", 
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
	message_from(NULL, LOG_CRAP);
	return;
}

/*
 * Check all DCCs for data, and if they have any, perform whatever
 * actions are required.
 */
void	dcc_check (fd_set *Readables, fd_set *Writables)
{
	DCC_list	*Client;
	int		previous_server;
	char		*encoded_description = NULL;
	
	/* Sanity */
	if (!Readables)
		return;

	/* Whats with all this double-pointer chicanery anyhow? */
	lock_dcc(NULL);
	message_from(NULL, LOG_DCC);

	for (Client = ClientList; Client; Client = Client->next)
	{
	    /*
	     * Ignore anything that is pending-delete.
	     */
	    if (Client->flags & DCC_DELETE)
		continue;

	    if (Readables && Client->socket != -1 && 
		FD_ISSET(Client->socket, Readables))
	    {
		FD_CLR(Client->socket, Readables);	/* No more! */
		previous_server = from_server;
		from_server = NOSERV;

		switch (Client->flags & DCC_TYPES)
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

		from_server = previous_server;
	    }
	    /*
	     * Enforce any timeouts
	     */
	    else if (Client->flags & (DCC_MY_OFFER | DCC_THEIR_OFFER) &&
		 dcc_timeout >= 0 &&
		 time_diff(Client->lasttime, now) > dcc_timeout)
	    {
		lock_dcc(Client);

		if (Client->description) {
			if ((Client->flags & DCC_TYPES) == DCC_FILEOFFER)
				encoded_description = dcc_urlencode(Client->description);
			else
				/* assume the other end encoded the filename */
				encoded_description = malloc_strdup(Client->description);
		}

		if (do_hook(DCC_LOST_LIST,"%s %s %s IDLE TIME EXCEEDED",
			Client->user, 
			dcc_types[Client->flags & DCC_TYPES],
			encoded_description ? 
			 encoded_description : "<any>"))
		    say("Auto-rejecting a dcc after [%ld] seconds", 
			(long)time_diff(Client->lasttime, now));

		/* 
		 * This is safe to do after, since the connection
		 * is still open, and the user might want to send
		 * something to the other peer here, and there is no
		 * good reason not to let them.
		 */
		Client->flags |= DCC_DELETE;

		if (encoded_description)
			new_free(&encoded_description);

		unlock_dcc(Client);
	    }


	    /*
	     * This shouldnt be neccesary any more, but what the hey,
	     * I'm still paranoid.
	     */
	    if (!Client)
		break;
	}

	message_from(NULL, LOG_CRAP);
	unlock_dcc(NULL);
	dcc_garbage_collect();
}



/*********************************** DCC CHAT *******************************/
static	void	process_dcc_chat_connection (DCC_list *Client)
{
	SS	remaddr;
	int	sra;
	char	p_addr[256];
	char	p_port[24];
	SA *	addr;
	int	fd;

	sra = sizeof(remaddr);
	fd = Accept(Client->socket, (SA *)&Client->peer_sockaddr, &sra);
	Client->socket = new_close(Client->socket);
	if ((Client->socket = fd) > 0)
		new_open(Client->socket);
	else
	{
		Client->flags |= DCC_DELETE;
		yell("### DCC Error: accept() failed.  Punting.");
		return;
	}

	Client->flags &= ~DCC_MY_OFFER;
	Client->flags |= DCC_ACTIVE;

	addr = (SA *)&Client->peer_sockaddr;
	inet_ntostr(addr, p_addr, 256, p_port, 24, NI_NUMERICHOST);

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
	const char	*err_str;

	err_str = ((dgets_errno == -1) ? 
			"Remote End Closed Connection" : 
			strerror(dgets_errno));

	Client->flags |= DCC_DELETE;
	lock_dcc(Client);
	if (do_hook(DCC_LOST_LIST, "%s CHAT %s", 
			Client->user, err_str))
		say("DCC CHAT connection to %s lost [%s]", 
			Client->user, err_str);
	unlock_dcc(Client);
	return;
}

static	char *	process_dcc_chat_ctcps (DCC_list *Client, char *tmp)
{
	char 	equal_nickname[80];
	int	ctcp_request = 0, ctcp_reply = 0;

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
		snprintf(equal_nickname, sizeof equal_nickname, 
				"=%s", Client->user);

		message_from(Client->user, LOG_CTCP);
		if (ctcp_request == 1)
			tmp = do_ctcp(equal_nickname, nickname, tmp);
		else
			tmp = do_notice_ctcp(equal_nickname, nickname, tmp);
		message_from(NULL, LOG_DCC);

		FromUserHost = OFUH;
	}

	if (!tmp || !*tmp)
		return NULL;

	return tmp;
}

static	void	process_dcc_chat_data (DCC_list *Client)
{
	char	tmp[IO_BUFFER_SIZE + 1];
	ssize_t	bytesread;

	/* Get a new line via dgets. */
	bytesread = dgets(Client->socket, tmp, IO_BUFFER_SIZE, 1, NULL);

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
	message_from(Client->user, LOG_DCC);
	lock_dcc(Client);
	if (do_hook(DCC_CHAT_LIST, "%s %s", Client->user, tmp))
	{
		if (get_server_away(NOSERV))
		{
			strlcat(tmp, "<", sizeof tmp);
			strlcat(tmp, my_ctime(time(NULL)), sizeof tmp);
			strlcat(tmp, ">", sizeof tmp);
		}
		put_it("=%s= %s", Client->user, tmp);
	}
	unlock_dcc(Client);
	message_from(NULL, LOG_DCC);
}

static	void	process_dcc_chat (DCC_list *Client)
{
	if (Client->flags & DCC_MY_OFFER)
		process_dcc_chat_connection(Client);
	else
		process_dcc_chat_data(Client);
}


/****************************** DCC RAW *************************************/
/*
 * This handles when someone establishes a connection on a $listen()ing
 * socket.  This hooks via /on DCC_RAW.
 */
static	void		process_incoming_listen (DCC_list *Client)
{
	SS		remaddr;
	int		sra;
	char		fdstr[10];
	DCC_list	*NewClient;
	int		new_socket;
	char		host[1025];
	socklen_t	len;
	char		p_port[24];
	char		l_port[24];
	char		trash[1025] = "";

	sra = sizeof(remaddr);
	new_socket = Accept(Client->socket, (SA *) &remaddr, &sra);
	if (new_socket < 0)
	{
		yell("### DCC Error: accept() failed.  Punting.");
		return;
	}

	*host = 0;
	inet_ntostr((SA *)&remaddr, host, sizeof(host), 
				p_port, sizeof(p_port), 0);

	strlcpy(fdstr, ltoa(new_socket), sizeof fdstr);
	if (!(NewClient = dcc_searchlist(DCC_RAW, fdstr, host, NULL, 0)))
		NewClient = dcc_create(DCC_RAW, fdstr, host, NULL, 0, 0);

	NewClient->socket = new_socket;
	NewClient->peer_sockaddr = remaddr;
	NewClient->offer = remaddr;

	len = sizeof(NewClient->local_sockaddr);
	getsockname(NewClient->socket, (SA *)&NewClient->local_sockaddr, &len);
	inet_ntostr((SA *)&Client->local_sockaddr, trash, sizeof(trash), 
				l_port, sizeof(l_port), 0);

	NewClient->flags |= DCC_ACTIVE;
	NewClient->bytes_read = NewClient->bytes_sent = 0;
	get_time(&NewClient->starttime);
	new_open(NewClient->socket);

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
 * connection (that was established with a $listen().
 */
static	void		process_incoming_raw (DCC_list *Client)
{
	char	tmp[IO_BUFFER_SIZE + 1];
	char 	*bufptr;
	ssize_t	bytesread;

        bufptr = tmp;
	switch ((bytesread = dgets(Client->socket, bufptr, IO_BUFFER_SIZE, 0, NULL)))
	{
	    case -1:
	    {
		lock_dcc(Client);
		Client->flags |= DCC_DELETE;
		if (do_hook(DCC_RAW_LIST, "%s %s C",
				Client->user, Client->description))
       		if (do_hook(DCC_LOST_LIST,"%s RAW %s", 
				Client->user, Client->description))
			say("DCC RAW connection to %s on %s lost",
				Client->user, Client->description);
		unlock_dcc(Client);
		break;
	    }
	    case 0:
	    default:
	    {
		from_server = primary_server;	/* Colten asked for it */
		if (bytesread > 0 && tmp[strlen(tmp) - 1] == '\n')
			tmp[strlen(tmp) - 1] = '\0';
		Client->bytes_read += bytesread;

		lock_dcc(Client);
		if (do_hook(DCC_RAW_LIST, "%s %s D %s",
				Client->user, Client->description, tmp))
			say("Raw data on %s from %s: %s",
				Client->user, Client->description, tmp);
		unlock_dcc(Client);
	    }
	}
	return;
}


/****************************** DCC SEND ************************************/
/*
 * When youre sending a file, and your peer sends an ACK, this handles
 * whether or not to send the next packet.
 */
static void	process_dcc_send_connection (DCC_list *dcc)
{
	SS		remaddr;
	int		sra;
	int		new_fd;
#ifdef HAVE_SO_SNDLOWAT
	int		size;
#endif
	SA *		addr;
	char		p_addr[256];
	char		p_port[24];
	char		*encoded_description;

	/*
	 * Open up the network connection
	 */
	sra = sizeof(remaddr);
	new_fd = Accept(dcc->socket, (SA *)&dcc->peer_sockaddr, &sra);
	dcc->socket = new_close(dcc->socket);
	if ((dcc->socket = new_fd) < 0)
	{
		dcc->flags |= DCC_DELETE;
		yell("### DCC Error: accept() failed.  Punting.");
		return;
	}
	new_open(dcc->socket);
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

	addr = (SA *)&dcc->peer_sockaddr;
	inet_ntostr(addr, p_addr, 256, p_port, 24, NI_NUMERICHOST);

	/*
	 * Tell the user
	 */
	lock_dcc(dcc);
	encoded_description = dcc_urlencode(dcc->description);
	if (do_hook(DCC_CONNECT_LIST, "%s SEND %s %s %s %s",
			dcc->user, p_addr, p_port,
			dcc->description, ltoa(dcc->filesize)))
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
		say("Unable to open %s: %s", dcc->description,
			errno ? strerror(errno) : "Unknown Host");
		return;
	}

	/*
	 * Set up any DCC RESUME as needed
	 */
	if (dcc->bytes_sent)
	{
		if (x_debug & DEBUG_DCC_XMIT)
			yell("Resuming at address (%ld)", 
				(long)dcc->bytes_sent);
		lseek(dcc->file, dcc->bytes_sent, SEEK_SET);
	}
}

static void	process_dcc_send_handle_ack (DCC_list *dcc)
{
	char		*encoded_description;
	u_32int_t	bytesrecvd;

	if (x_debug & DEBUG_DCC_XMIT)
		yell("Reading a packet from [%s:%s(%s)]", 
			dcc->user, 
			dcc->othername, 
			ltoa(dcc->packets_ack));

	/*
	 * It is important to note here that the ACK must always
	 * be exactly four bytes.  Never more, never less.
	 */
	if (read(dcc->socket, (char *)&bytesrecvd, 
			sizeof(u_32int_t)) < (int) sizeof(u_32int_t))
	{
		dcc->flags |= DCC_DELETE;

		lock_dcc(dcc);
		encoded_description = dcc_urlencode(dcc->description);
		if (do_hook(DCC_LOST_LIST,"%s SEND %s CONNECTION LOST",
			dcc->user, encoded_description))
		    say("DCC SEND:%s connection to %s lost",
			dcc->description, dcc->user);
		new_free(&encoded_description);
		unlock_dcc(dcc);
		return;
	}
	bytesrecvd = ntohl(bytesrecvd);

	/*
	 * Check to see if we need to move the sliding window up
	 */
	if (bytesrecvd >= (dcc->packets_ack + 1) * DCC_BLOCK_SIZE)
	{
		if (x_debug & DEBUG_DCC_XMIT)
			yell("Packet #%s ACKed", 
				ltoa(dcc->packets_ack));
		dcc->packets_ack++;
		dcc->packets_outstanding--;
	}

	if (bytesrecvd > dcc->bytes_sent)
	{
yell("### WARNING!  The other peer claims to have recieved more bytes than");
yell("### I have actually sent so far.  Please report this to ");
yell("### jnelson@acronet.net or #epic on EFNet right away.");
yell("### Ask the person who you sent the file to look for garbage at the");
yell("### end of the file you just sent them.  Please enclose that ");
yell("### information as well as the following:");
yell("###");
yell("###    bytesrecvd [%ld]        dcc->bytes_sent [%s]", 
			ntohl(bytesrecvd), 
			ltoa(dcc->bytes_sent));
yell("###    dcc->filesize [%s]", 
			ltoa(dcc->filesize));
yell("###    dcc->packets_ack [%s]", 
			ltoa(dcc->packets_ack));
yell("###    dcc->packets_outstanding [%s]", 
			ltoa(dcc->packets_outstanding));

		/* And just cope with it to avoid whining */
		dcc->bytes_sent = bytesrecvd;
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
		if (bytesrecvd >= dcc->filesize)
			DCC_close_filesend(dcc, "SEND");

		/*
		 * Either way there's nothing more to do.
		 */
		return;
	}
}


static void	process_dcc_send_data (DCC_list *dcc)
{
	fd_set	fd;
	int	maxpackets;
	Timeval	to;
	ssize_t	bytesread;
	char	tmp[DCC_BLOCK_SIZE+1];
	int	old_from_server = from_server;

	/*
	 * We use a nonblocking sliding window algorithm.  We send as many
	 * packets as we can *without blocking*, up to but never more than
	 * the value of /SET DCC_SLIDING_WINDOW.  Whenever we recieve some
	 * stimulus (like from an ACK) we re-fill the window.  We always do
	 * a select() before we write() to make sure that it wont block.
	 */
	FD_ZERO(&fd);
	maxpackets = get_int_var(DCC_SLIDING_WINDOW_VAR);
	if (maxpackets < 1)
		maxpackets = 1;		/* Sanity */

	while (dcc->packets_outstanding < (unsigned) maxpackets)
	{
		/*
		 * Check to make sure the write won't block.
		 */
		FD_SET(dcc->socket, &fd);
		to.tv_sec = 0;
		to.tv_usec = 0;
		if (select(dcc->socket + 1, NULL, &fd, NULL, &to) <= 0)
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
		    yell("Sending packet [%s [%s] (packet %s) (%d bytes)]",
			dcc->user, 
			dcc->othername, 
			ltoa(dcc->packets_transfer),
			bytesread);

		/*
		 * Attempt to write the file.  If it chokes, whine.
		 */
		if (write(dcc->socket, tmp, bytesread) < bytesread)
		{
			dcc->flags |= DCC_DELETE;
			say("Outbound write() failed: %s", 
				errno ? strerror(errno) : 
					"I'm not sure why");

			from_server = old_from_server;
			return;
		}

		dcc->packets_outstanding++;
		dcc->packets_transfer++;
		dcc->bytes_sent += bytesread;

		/*
		 * Update the status bar
		 */
		if (dcc->filesize)
		{
		    update_transfer_buffer("(to %10s: %d of %d: %d%%)",
			dcc->user, 
			(int)dcc->packets_transfer, 
			(int)dcc->packets_total, 
			(int)(dcc->bytes_sent * 100.0 / dcc->filesize));
		}
		else
		{
		    update_transfer_buffer("(to %10s: %ld kbytes     )",
			dcc->user, 
			(long)(dcc->bytes_sent / 1024));
		}
		update_all_status();
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
 */
static	void		process_incoming_file (DCC_list *dcc)
{
	char		tmp[DCC_BLOCK_SIZE+1];
	u_32int_t	bytestemp;
	ssize_t		bytesread;

	if ((bytesread = read(dcc->socket, tmp, DCC_BLOCK_SIZE)) <= 0)
	{
		if (dcc->bytes_read < dcc->filesize)
		    say("DCC GET to %s lost -- Remote peer closed connection", 
			dcc->user);
		DCC_close_filesend(dcc, "GET");
		return;
	}

	if ((write(dcc->file, tmp, bytesread)) == -1)
	{
		dcc->flags |= DCC_DELETE;
		say("Write to local file [%d] failed, giving up: [%s]", 
			dcc->file, strerror(errno));
		return;
	}

	dcc->bytes_read += bytesread;
	bytestemp = htonl(dcc->bytes_read);
	if (write(dcc->socket, (char *)&bytestemp, sizeof(u_32int_t)) == -1)
	{
		dcc->flags |= DCC_DELETE;
		yell("### Write to remote peer failed.  Giving up.");
		return;
	}

	dcc->packets_transfer = dcc->bytes_read / DCC_BLOCK_SIZE;

	/* TAKE THIS OUT IF IT CAUSES PROBLEMS */
	if ((dcc->filesize) && (dcc->bytes_read > dcc->filesize))
	{
		dcc->flags |= DCC_DELETE;
		yell("### DCC GET WARNING: incoming file is larger then the "
			"handshake said");
		yell("### DCC GET: Closing connection");
		return;
	}

	if (((dcc->flags & DCC_TYPES) == DCC_FILEOFFER) || 
	     ((dcc->flags & DCC_TYPES) == DCC_FILEREAD))
	{
		if (dcc->filesize)
			update_transfer_buffer("(%10s: %d of %d: %d%%)", 
				dcc->user, (int)dcc->packets_transfer,
				(int)dcc->packets_total, 
				(int)(dcc->bytes_read * 100.0 / dcc->filesize));
		else
			update_transfer_buffer("(%10s %d packets: %ldK)", 
				dcc->user, (int)dcc->packets_transfer, 
				(long)(dcc->bytes_read / 1024));
		update_all_status();
	}
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
	char	*nickname_requested;
	char	*nickname_recieved;
	char	*type;
	char	*description;
	int	old_fs;

	/*
	 * XXX This is, of course, a monsterous hack.
	 */
	nickname_requested 	= next_arg(original, &original);
	type 			= next_arg(original, &original);
	description 		= next_arg(original, &original);
	nickname_recieved 	= next_arg(received, &received);

	if (nickname_recieved && *nickname_recieved)
	{
		/* XXX -- Ok, whatever.  */
		dccs_rejected++;

		old_fs = from_server;
		from_server = refnum;
		send_ctcp(CTCP_NOTICE, nickname_recieved, CTCP_DCC,
				"REJECT %s %s", type, description);
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

	for (CType = 0; dcc_types[CType] != NULL; CType++)
		if (!my_stricmp(type, dcc_types[CType]))
			break;

	if (!dcc_types[CType])
		return;

	message_from(NULL, LOG_DCC);
	description = next_arg(args, &args);

	if ((Client = dcc_searchlist(CType, from, description,
					description, -1)))
	{
		lock_dcc(Client);
		Client->flags |= DCC_REJECTED;
		Client->flags |= DCC_DELETE; 

                if (do_hook(DCC_LOST_LIST,"%s %s %s REJECTED", from, type,
                        description ? description : "<any>"))
		    say("DCC %s:%s rejected by %s: closing", type,
			description ? description : "<any>", from);
		unlock_dcc(Client);
	}

	dcc_garbage_collect();
	message_from(NULL, LOG_CRAP);
}


static void	DCC_close_filesend (DCC_list *Client, const char *info)
{
	char	lame_ultrix[10];	/* should be plenty */
	char	lame_ultrix2[10];
	char	lame_ultrix3[10];
	double 	xtime, xfer;
	char	*encoded_description;

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
	Client->flags |= DCC_DELETE;
	
	if ((Client->flags & DCC_TYPES) == DCC_FILEOFFER)
		encoded_description = dcc_urlencode(Client->description);
	else
		/* assume the other end encoded the filename */
		encoded_description = malloc_strdup(Client->description);

	if (do_hook(DCC_LOST_LIST,"%s %s %s %s TRANSFER COMPLETE",
		Client->user, info, encoded_description, lame_ultrix))
	     say("DCC %s:%s [%skb] with %s completed in %s sec (%s kb/sec)",
		info, Client->description, lame_ultrix2, Client->user, 
		lame_ultrix3, lame_ultrix);

	new_free(&encoded_description);
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


static void 	update_transfer_buffer (const char *format, ...)
{
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

}


static	char *	dcc_urlencode (const char *s)
{
	const char *p1;
	char *str, *p2, *ret;

	str = malloc_strdup(s);

	for (p1 = s, p2 = str; *p1; p1++)
	{
		if (*p1 == '\\')
			continue;
		*p2++ = *p1;
	}

	*p2 = 0;
	ret = urlencode(str);
	new_free(&str);
	return ret;
}

static	char *	dcc_urldecode (const char *s)
{
	char *str, *p1;

	str = malloc_strdup(s);
	urldecode(str, NULL);

	for (p1 = str; *p1; p1++)
	{
		if (*p1 != '.')
			break;
		*p1 = '_';
	}

#if 0		/* (hop, 09/24/2003) */
	for (p1 = str; *p1; p1++)
	{
		if (*p1 == '/')
			*p1 = '_';
	}
#endif

	return str;
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
static void dcc_getfile_resume_demanded (const char *user, char *filename, char *port, char *offset)
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

	Client->bytes_sent = Client->resume_size = my_atol(offset);
	Client->bytes_read = 0;

	/* Just in case we have to fool the protocol enforcement. */
	proto = get_server_protocol_state(from_server);
	set_server_protocol_state(from_server, 0);

	send_ctcp(CTCP_PRIVMSG, user, CTCP_DCC, "ACCEPT %s %s %s",
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
	Filename	fullname, pathname;
	char		*realfilename = NULL;

	if (!get_int_var(MIRC_BROKEN_DCC_RESUME_VAR))
		return;

	if (x_debug & DEBUG_DCC_XMIT)
		yell("GOT CONFIRMATION FOR DCC RESUME from [%s] [%s]", nick, filename);

	if (!strcmp(filename, "file.ext"))
		filename = NULL;

	if (!(Client = dcc_searchlist(DCC_FILEREAD, nick, filename, port, 0)))
		return;		/* Its fake. */

	Client->flags |= DCC_TWOCLIENTS;
	if (dcc_open(Client))
		return;


	if (get_string_var(DCC_STORE_PATH_VAR))
	{
		strlcpy(pathname, get_string_var(DCC_STORE_PATH_VAR), 
					sizeof(pathname));
	} else /* SUSv2 doesn't specify realpath() behavior for "" */
		strcpy(pathname, "./");


	if (normalize_filename(pathname, fullname))
	{
		say("%s is not a valid directory", fullname);
		Client->flags |= DCC_DELETE;
		return;
	}

	if (fullname && *fullname)
		strlcat(fullname, "/", sizeof(fullname));

	realfilename = dcc_urldecode(Client->description);
	if (*realfilename == '/')
		strlcpy(fullname, realfilename, sizeof(fullname));
	else
		strlcat(fullname, realfilename, sizeof(fullname));

	new_free(&realfilename);

	if (!(Client->file = open(fullname, O_WRONLY | O_APPEND, 0644)))
	{
		Client->flags |= DCC_DELETE;
		say("Unable to open %s: %s", fullname, errno ? strerror(errno) : "<No Error>");
	}
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

	GET_STR_ARG(listc, input);
	len = strlen(listc);
	if (!my_strnicmp(listc, "REFNUMS", len)) {
		for (client = ClientList; client; client = client->next)
			malloc_strcat_wordlist_c(&retval, space, ltoa(client->refnum), &clue);
	} else if (!my_strnicmp(listc, "GET", len)) {
		GET_INT_ARG(ref, input);

		if (!(client = get_dcc_by_refnum(ref)))
			RETURN_EMPTY;

		GET_STR_ARG(listc, input);
		len = strlen(listc);
		if (!my_strnicmp(listc, "REFNUM", len)) {
			RETURN_INT(client->refnum);
		} else if (!my_strnicmp(listc, "TYPE", len)) {
			RETURN_STR(dcc_types[client->flags & DCC_TYPES]);
		} else if (!my_strnicmp(listc, "DESCRIPTION", len)) {
			RETURN_STR(client->description);
		} else if (!my_strnicmp(listc, "FILENAME", len)) {
			RETURN_STR(client->filename);
		} else if (!my_strnicmp(listc, "USER", len)) {
			RETURN_STR(client->user);
		} else if (!my_strnicmp(listc, "USERHOST", len)) {
			RETURN_STR(client->userhost);
		} else if (!my_strnicmp(listc, "OTHERNAME", len)) {
			RETURN_STR(client->othername);
		} else if (!my_strnicmp(listc, "SIZE", len)) {
			RETURN_INT(client->filesize);
		} else if (!my_strnicmp(listc, "FILESIZE", len)) {  /* DEPRECATED */
			RETURN_INT(client->filesize);
		} else if (!my_strnicmp(listc, "RESUMESIZE", len)) {
			RETURN_INT(client->resume_size);
		} else if (!my_strnicmp(listc, "READBYTES", len)) {
			RETURN_INT(client->bytes_read);
		} else if (!my_strnicmp(listc, "SENTBYTES", len)) {
			RETURN_INT(client->bytes_sent);
		} else if (!my_strnicmp(listc, "SERVER", len)) {
			RETURN_INT(client->server);
		} else if (!my_strnicmp(listc, "LOCKED", len)) {
			RETURN_INT(client->locked);
		} else if (!my_strnicmp(listc, "HELD", len)) {
			RETURN_INT(client->held);
		} else if (!my_strnicmp(listc, "HELDTIME", len)) {
			RETURN_FLOAT(client->heldtime);
		} else if (!my_strnicmp(listc, "FLAGS", len)) {
			/* This is pretty much a crock. */
			RETURN_INT(client->flags);
		} else if (!my_strnicmp(listc, "LASTTIME", len)) {
			malloc_strcat_wordlist_c(&retval, space, ltoa(client->lasttime.tv_sec), &clue);
			malloc_strcat_wordlist_c(&retval, space, ltoa(client->lasttime.tv_usec), &clue);
		} else if (!my_strnicmp(listc, "STARTTIME", len)) {
			malloc_strcat_wordlist_c(&retval, space, ltoa(client->starttime.tv_sec), &clue);
			malloc_strcat_wordlist_c(&retval, space, ltoa(client->starttime.tv_usec), &clue);
		} else if (!my_strnicmp(listc, "HOLDTIME", len)) {
			malloc_strcat_wordlist_c(&retval, space, ltoa(client->holdtime.tv_sec), &clue);
			malloc_strcat_wordlist_c(&retval, space, ltoa(client->holdtime.tv_usec), &clue);
		} else if (!my_strnicmp(listc, "OFFERADDR", len)) {
			char	host[1025], port[25];
			if (inet_ntostr((SA *)&client->offer,
					host, sizeof(host),
					port, sizeof(port), NI_NUMERICHOST))
				RETURN_EMPTY;
			malloc_strcat_wordlist_c(&retval, space, host, &clue);
			malloc_strcat_wordlist_c(&retval, space, port, &clue);
		} else if (!my_strnicmp(listc, "REMADDR", len)) {
			char	host[1025], port[25];
			if (!(client->flags & DCC_ACTIVE) ||
				inet_ntostr((SA *)&client->peer_sockaddr,
					host, sizeof(host),
					port, sizeof(port), NI_NUMERICHOST))
				RETURN_EMPTY;
			malloc_strcat_wordlist_c(&retval, space, host, &clue);
			malloc_strcat_wordlist_c(&retval, space, port, &clue);
		} else if (!my_strnicmp(listc, "LOCADDR", len)) {
			char	host[1025], port[25];
			if (!(client->flags & DCC_ACTIVE) ||
				inet_ntostr((SA *)&client->local_sockaddr,
					host, sizeof(host),
					port, sizeof(port), NI_NUMERICHOST))
				RETURN_EMPTY;
			malloc_strcat_wordlist_c(&retval, space, host, &clue);
			malloc_strcat_wordlist_c(&retval, space, port, &clue);
		} else {
			RETURN_EMPTY;
		}
	} else if (!my_strnicmp(listc, "SET", len)) {
		GET_INT_ARG(ref, input);

		if (!(client = get_dcc_by_refnum(ref)))
			RETURN_EMPTY;

		GET_STR_ARG(listc, input);
		len = strlen(listc);
		if (!my_strnicmp(listc, "REFNUM", len)) {
			long	newref;

			GET_INT_ARG(newref, input);
			client->refnum = newref;

			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "DESCRIPTION", len)) {
			malloc_strcpy(&client->description, input);
		} else if (!my_strnicmp(listc, "FILENAME", len)) {
			malloc_strcpy(&client->filename, input);
		} else if (!my_strnicmp(listc, "USER", len)) {
			malloc_strcpy(&client->user, input);
		} else if (!my_strnicmp(listc, "USERHOST", len)) {
			malloc_strcpy(&client->userhost, input);
		} else if (!my_strnicmp(listc, "OTHERNAME", len)) {
			malloc_strcpy(&client->othername, input);
		} else if (!my_strnicmp(listc, "HELD", len)) {
			long	hold, held;

			GET_INT_ARG(hold, input);
			if (hold)
				held = dcc_hold(client);
			else
				held = dcc_unhold(client);

			RETURN_INT(held);
		} else if (!my_strnicmp(listc, "OFFERADDR", len)) {
			char *host, *port;
			SS a;

			GET_STR_ARG(host, input);
			GET_STR_ARG(port, input);

			V4FAM(a) = AF_UNSPEC;
			if ((client->flags & DCC_ACTIVE) ||
					inet_strton(host, port, (SA *)&a, 0))
				RETURN_EMPTY;

			memcpy(&client->offer, &a, sizeof client->offer);
			RETURN_INT(1);
		} else {
			RETURN_EMPTY;
		}
		RETURN_INT(1);
	} else if (!my_strnicmp(listc, "TYPEMATCH", len)) {
		for (client = ClientList; client; client = client->next)
			if (wild_match(input, dcc_types[client->flags & DCC_TYPES]))
				malloc_strcat_wordlist_c(&retval, space, ltoa(client->refnum), &clue);
	} else if (!my_strnicmp(listc, "DESCMATCH", len)) {
		for (client = ClientList; client; client = client->next)
			if (wild_match(input, client->description ? client->description : EMPTY))
				malloc_strcat_wordlist_c(&retval, space, ltoa(client->refnum), &clue);
	} else if (!my_strnicmp(listc, "FILEMATCH", len)) {
		for (client = ClientList; client; client = client->next)
			if (wild_match(input, client->filename ? client->filename : EMPTY))
				malloc_strcat_wordlist_c(&retval, space, ltoa(client->refnum), &clue);
	} else if (!my_strnicmp(listc, "USERMATCH", len)) {
		for (client = ClientList; client; client = client->next)
			if (wild_match(input, client->user ? client->user : EMPTY))
				malloc_strcat_wordlist_c(&retval, space, ltoa(client->refnum), &clue);
	} else if (!my_strnicmp(listc, "USERHOSTMATCH", len)) {
		for (client = ClientList; client; client = client->next)
			if (wild_match(input, client->userhost ? client->userhost : EMPTY))
				malloc_strcat_wordlist_c(&retval, space, ltoa(client->refnum), &clue);
	} else if (!my_strnicmp(listc, "OTHERMATCH", len)) {
		for (client = ClientList; client; client = client->next)
			if (wild_match(input, client->othername ? client->othername : EMPTY))
				malloc_strcat_wordlist_c(&retval, space, ltoa(client->refnum), &clue);
	} else if (!my_strnicmp(listc, "LOCKED", len)) {
		for (client = ClientList; client; client = client->next)
			if (client->locked)
				malloc_strcat_wordlist_c(&retval, space, ltoa(client->refnum), &clue);
	} else if (!my_strnicmp(listc, "HELD", len)) {
		for (client = ClientList; client; client = client->next)
			if (client->held)
				malloc_strcat_wordlist_c(&retval, space, ltoa(client->refnum), &clue);
	} else if (!my_strnicmp(listc, "UNHELD", len)) {
		for (client = ClientList; client; client = client->next)
			if (!client->held)
				malloc_strcat_wordlist_c(&retval, space, ltoa(client->refnum), &clue);
	} else
		RETURN_EMPTY;

	RETURN_MSTR(retval);
}
