/*
 * dcc.c: Things dealing client to client connections. 
 *
 * Written By Troy Rollo <troy@cbme.unsw.oz.au> 
 *
 * Copyright(c) 1991 
 *
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#if 0
static	char	rcsid[] = "@(#)$Id: dcc.c,v 1.26 2002/05/28 04:55:57 jnelson Exp $";
#endif

#include "irc.h"
#include "crypt.h"
#include "ctcp.h"
#include "dcc.h"
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
#include <sys/stat.h>

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
		int		locked;		/* XXX - Sigh */
		int		socket;
		int		file;
		unsigned long	filesize;
		char *		description;
		char *		filename;
		char *		user;
		char *		othername;
		char *		encrypt;
	struct	DCC_struct *	next;

		SS		offer;			/* Their offer */
		SS		peer_sockaddr;		/* Their saddr */
		SS		local_sockaddr;		/* Our saddr */
		u_short		want_port;		/* HOST ORDER */

		off_t		bytes_read;
		off_t		bytes_sent;
		int		window_sent;
		int		window_max;
		Timeval		lasttime;
		Timeval		starttime;
		unsigned long	packets_total;
		unsigned long	packets_transfer;
		unsigned long	packets_ack;
		long		packets_outstanding;
		off_t		resume_size;

		int		(*open_callback) (struct DCC_struct *);
}	DCC_list;

static	off_t		filesize = 0;
static	DCC_list *	ClientList = NULL;
static	char		DCC_current_transfer_buffer[256];
	time_t		dcc_timeout = 600;		/* Backed by a /set */

static	void 		dcc_add_deadclient 	(DCC_list *);
static	void		dcc_chat 		(char *);
static	void 		dcc_close 		(char *);
static	void		dcc_closeall		(char *);
static	void		dcc_erase 		(DCC_list *);
static	void		dcc_filesend 		(char *);
static	void		dcc_getfile 		(char *);
static	int		dcc_open 		(DCC_list *);
static	int		dcc_opened		(int, int);
static	void 		dcc_really_erase 	(void);
static	void		dcc_rename 		(char *);
static	DCC_list *	dcc_searchlist 		(const char *, const char *, unsigned, int, const char *, int);
static	void		dcc_send_raw 		(char *);
static	void 		output_reject_ctcp 	(char *, char *);
static	void		process_incoming_chat	(DCC_list *);
static	void		process_incoming_listen (DCC_list *);
static	void		process_incoming_raw 	(DCC_list *);
static	void		process_outgoing_file 	(DCC_list *);
static	void		process_incoming_file 	(DCC_list *);
static	void		DCC_close_filesend 	(DCC_list *, char *);
static	void		update_transfer_buffer 	(char *format, ...);
static 	void		dcc_send_booster_ctcp 	(DCC_list *dcc);
static	char *		dcc_urlencode		(const char *);
static	char *		dcc_urldecode		(const char *);

#ifdef MIRC_BROKEN_DCC_RESUME
static	void		dcc_getfile_resume 	    (char *);
static 	void 		dcc_getfile_resume_demanded (char *, char *, char *, char *);
static	void		dcc_getfile_resume_start    (char *, char *, char *, char *);
#endif


/*
 * These are the possible <type> arguments to /DCC
 */
typedef void (*dcc_function) (char *);
struct
{
	char	*	name;
	dcc_function 	function;
}	dcc_commands[] =
{
	{ "CHAT",	dcc_chat 		},	/* DCC_CHAT */
	{ "SEND",	dcc_filesend 		},	/* DCC_FILEOFFER */
	{ "GET",	dcc_getfile 		},	/* DCC_FILEREAD */
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
char	*dcc_types[] =
{
	"<none>",
	"CHAT",		/* DCC_CHAT */
	"SEND",		/* DCC_FILEOFFER */
	"GET",		/* DCC_FILEREAD */
	"RAW",		/* DCC_RAW */
	"RAW_LISTEN",
	NULL
};

struct	deadlist
{
	DCC_list *	it;
	struct deadlist *next;
}	*deadlist = NULL;

int	dcc_dead (void) 
{ 
	struct deadlist *dies;

	if (!deadlist)
		return 0;

	for (dies = deadlist; dies; dies = dies->next)
		if (dies->it->locked)
			return 0;		/* Erase list is locked */

	return 1;		/* All clear! */
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
 * When a DCC is considered to be "invalid", the DCC_DELETE bit is set.
 * The structure is not immediately removed, however -- it sticks around
 * until the next looping synchornization point where dcc_check() collects
 * the "deleted" DCC structures into the 'deadlist'.  After all of the
 * DCCs have been parsed, it then goes through and cleans the list.  This
 * ensures that no DCCs are removed from under the feet of some unsuspecting
 * function -- they are only deleted synchronously.
 *
 * The downside is that it requires an M*N algorithm to remove the DCCs.
 */
static void 	dcc_add_deadclient (DCC_list *client)
{
	struct deadlist *new_dl;

	for (new_dl = deadlist; new_dl; new_dl = new_dl->next)
		if (new_dl->it == client)
			return;

	new_dl = new_malloc(sizeof(struct deadlist));
	new_dl->next = deadlist;
	new_dl->it = client;
	deadlist = new_dl;
}

static void 	dcc_really_erase (void)
{
	struct deadlist *dies;

	/* Quick and dirty hack to avoid _NiC's bug */
	/* XXX -- Emphasis on DIRTY and HACK. */
	for (dies = deadlist; dies; dies = dies->next)
		if (dies->it->locked)
			return;		/* Erase list is locked */

	while ((dies = deadlist))
	{
		deadlist = deadlist->next;
		dcc_erase(dies->it);
		new_free((char **)&dies);
	}
	update_transfer_buffer(NULL);	/* Whatever */
	update_all_status();
}

/*
 * Note that 'erased' does not neccesarily have to be on ClientList.
 * In fact, it may very well NOT be on ClientList.  The handling at the
 * beginning of the function is only to sanity check that it isnt on 
 * ClientList when we blow it away.
 */
static 	void		dcc_erase (DCC_list *erased)
{
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
		time_t	now;

		time(&now);
		if (now - last_reject < 2)
			break;		/* Throttle flood attempts */

		last_reject = now;

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
		dummy_ptr = m_sprintf("%s %s %s", 
			erased->user,
			(my_type == DCC_FILEOFFER ? "GET" :
			 (my_type == DCC_FILEREAD  ? "SEND" : 
			   dcc_types[my_type])),
			nopath ? nopath : "<any>");

		erased->flags |= DCC_REJECTED;

		/*
		 * And output it to the user
		 */
		if (!dead && *(erased->user) != '=')
			isonbase(dummy_ptr, output_reject_ctcp);
		else
			output_reject_ctcp(dummy_ptr, erased->user);
	      }
	      while (0);
	}

	dcc_remove_from_list(erased);

	/*
	 * In any event, blow it away.
	 */
	erased->socket = new_close(erased->socket);
	erased->file = new_close(erased->file);
	new_free(&erased->description);	/* Magic check failure here */
	new_free(&erased->filename);
	new_free(&erased->user);
	new_free(&erased->othername);
	new_free(&erased->encrypt);
	new_free((char **)&erased);
}

/*
 * close_all_dcc:  We call this when we create a new process so that
 * we don't leave any fd's lying around, that won't close when we
 * want them to..
 */
void 	close_all_dcc (void)
{
	DCC_list *Client;
	int	pause = 0;

	if (ClientList && dead)
		pause = 1;

	while ((Client = ClientList))
		dcc_erase(Client);

	if (pause)
	{
		say("Waiting for DCC REJECTs to be sent");
		sleep(1);
	}
}




/*
 * These functions handle important DCC jobs.
 */

/*
 * dcc_searchlist searches through the dcc_list and finds the client
 * with the the flag described in type set.  This function should never
 * return a delete'd entry.
 */
static	DCC_list *dcc_searchlist (
	const char *	description,	/* 
					 * DCC Type specific information,
					 * Usually a full pathname for 
					 * SEND/GET, "listen" or "connect"
					 * for RAW, or NULL for others.
					 */
	const char *	user, 		/* Nick of the remote peer */
	unsigned 	type, 		/* Flags, including type of conn */
	int 		create, 	/* Create if it doesnt exist? */
	const char *	othername, 	/* Alias filename for SEND/GET */
	int 		active)		/* Only get active/non-active? */
{
	DCC_list 	*client, 
			*new_client;
	const char 	*last = NULL;

	if (x_debug & DEBUG_DCC_SEARCH)
		yell("entering dcc_sl.  desc (%s) user (%s) type (%d) "
		     "create (%d) other (%s) active (%d)", 
			description, user, type, create, othername, active);

	/*
	 * Walk all of the DCC connections that we know about.
	 */
	for (client = ClientList; client ; client = client->next)
	{
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
		 */
		if (type != -1 && ((client->flags & DCC_TYPES) != type))
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
			my_stricmp(description, client->description))
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
			if (my_stricmp(description, last + 1))
			{
				if (!othername || !client->othername)
					continue;

				if (my_stricmp(othername, client->othername))
					continue;
			}
		}

		/* Never return deleted entries */
		if (client->flags & DCC_DELETE)
			continue;

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
			;

		if (x_debug & DEBUG_DCC_SEARCH)
			yell("We have a winner!");

		/* Looks like we have a winner! */
		return client;
	}

	/*
	 * If flag == 1, then the user wants us to create a new DCC
	 * client if neccesary.  Otherwise, they were just checking to see
	 * if one existed and dont want to create it.
	 */
	if (!create)
		return NULL;

	new_client 			= new_malloc(sizeof(DCC_list));
	new_client->flags 		= type;
	new_client->locked		= 0;
	new_client->socket 		= -1;
	new_client->file 		= -1;
	new_client->filesize 		= filesize;
	new_client->filename 		= NULL;
	new_client->packets_total 	= filesize ? 
					  (filesize / DCC_BLOCK_SIZE + 1) : 0;
	new_client->packets_transfer 	= 0;
	new_client->packets_outstanding = 0;
	new_client->packets_ack 	= 0;
	new_client->next 		= ClientList;
	new_client->user 		= m_strdup(user);
	new_client->description 	= m_strdup(description);
	new_client->othername 		= m_strdup(othername);
	new_client->bytes_read 		= 0;
	new_client->bytes_sent 		= 0;
	new_client->starttime.tv_sec 	= 0;
	new_client->starttime.tv_usec 	= 0;
	new_client->window_max 		= 0;
	new_client->window_sent 	= 0;
	new_client->want_port 		= 0;
	new_client->encrypt 		= NULL;
	new_client->resume_size		= 0;
	new_client->open_callback	= NULL;
	get_time(&new_client->lasttime);

	ClientList = new_client;
	return new_client;
}

/*
 * Added by Chaos: Is used in edit.c for checking redirect.
 */
int	dcc_chat_active (char *user)
{
	return (dcc_searchlist("chat", user, DCC_CHAT, 0, NULL, 1)) ? 1 : 0;
}


int	dcc_opened (int fd, int result)
{
	DCC_list *dcc;
	int	len;
	char *	type;
	int	jvs_blah;

	for (dcc = ClientList; dcc ; dcc = dcc->next)
	{
		/* Never return deleted entries */
		if (dcc->flags & DCC_DELETE)
			continue;

		if (dcc->socket == fd)
			break;
	}

	if (!dcc)
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
	dcc->locked++;
	if ((dcc->flags & DCC_TYPES) != DCC_RAW)
	{
		char p_addr[256];
		char p_port[24];
		SA *addr = (SA *)&dcc->peer_sockaddr;

		inet_ntostr(addr, socklen(addr), p_addr, 256, 
				p_port, 24, NI_NUMERICHOST);

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
		    jvs_blah = do_hook(DCC_CONNECT_LIST,
				"%s %s %s %s %s %ld", 
				dcc->user, type, p_addr, p_port,
				dcc->description,
				dcc->filesize);

		    /*
		     * Compatability with bitchx
		     */
		    if (jvs_blah)
			jvs_blah = do_hook(DCC_CONNECT_LIST,
				"%s GET %s %s %s %ld", 
				dcc->user, p_addr, p_port,
				dcc->description,
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
		    message_from(NULL, LOG_DCC);
		    say("DCC %s connection with %s[%s:%s] established",
				type, dcc->user, p_addr, p_port);
		    message_from(NULL, LOG_CURRENT);
		}
	}
	dcc->locked--;

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
	int	old_server;

	/*
	 * Initialize our idea of what is going on.
	 */
	old_server = from_server;
	if (from_server == -1)
		from_server = get_window_server(0);

	/*
	 * DCC GET or DCC CHAT -- accept someone else's offer.
	 */
	if (dcc->flags & DCC_THEIR_OFFER)
	{
		dcc->socket = client_connect(NULL, 0, (SA *)&dcc->offer, 
						sizeof(dcc->offer));

		if (dcc->socket < 0)
		{
			dcc->flags |= DCC_DELETE;

			message_from(NULL, LOG_DCC);
			say("Unable to create connection: (%d) [%d] %s", 
				dcc->socket, errno, my_strerror(errno));
			message_from(NULL, LOG_CURRENT);

			from_server = old_server;
			return -1;
		}

		dcc_opened(dcc->socket, 0);
		from_server = old_server;
		return 0;
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
		dcc->socket = ip_bindery(AF_INET, dcc->want_port, 
					 &dcc->local_sockaddr);
		if (dcc->socket < 0)
		{
			dcc->flags |= DCC_DELETE;
			message_from(NULL, LOG_DCC);
			say("Unable to create connection [%d]: %s", 
				dcc->socket, my_strerror(errno));
			message_from(NULL, LOG_CURRENT);
			from_server = old_server;
			return -1;
		}

#ifdef MIRC_BROKEN_DCC_RESUME
		/*
		 * For stupid MIRC dcc resumes, we need to stash the
		 * local port number, because the remote client will send
		 * back that port number as its ID of what file it wants
		 * to resume (rather than the filename. ick.)
		 */
		if (FAMILY(dcc->local_sockaddr) == AF_INET)
			malloc_strcpy(&dcc->othername, ltoa((long)ntohs(V4PORT(dcc->local_sockaddr))));
#endif
		new_open(dcc->socket);

		/*
		 * If this is to be a 2-peer connection, then we need to
		 * send the remote peer a CTCP request.  I suppose we should
		 * do an ISON request first, but thats another project.
		 */
		if (dcc->flags & DCC_TWOCLIENTS)
			dcc_send_booster_ctcp(dcc);

		from_server = old_server;
		return 0;
	}
}

/*
 * send_booster_ctcp: This is called by dcc_open and also by dcc_filesend
 * to send a CTCP handshake message to a remote peer.  The reason its a 
 * function is because its a rather large chunk of code, and it needs to be
 * done basically identically by both places.  Whatever.
 */
static void	dcc_send_booster_ctcp (DCC_list *dcc)
{
	char *	nopath;
	char *	type = dcc_types[dcc->flags & DCC_TYPES];
	SS	my_sockaddr;
	int	family;

	family = FAMILY(dcc->local_sockaddr);

	if (get_int_var(DCC_USE_GATEWAY_ADDR_VAR))
		my_sockaddr = get_server_uh_addr(from_server);
	else if (family == AF_INET && V4ADDR(dcc->local_sockaddr).s_addr == htonl(INADDR_ANY))
		my_sockaddr = get_server_local_addr(from_server);
	else
		my_sockaddr = dcc->local_sockaddr;

	if (family == AF_INET)
		V4PORT(my_sockaddr) = V4PORT(dcc->local_sockaddr);

	/*
	 * If this is to be a 2-peer connection, then we need to
	 * send the remote peer a CTCP request.  I suppose we should
	 * do an ISON request first, but thats another project.
	 */
	if (!(dcc->flags & DCC_TWOCLIENTS))
		return;

	get_time(&dcc->starttime);
	get_time(&dcc->lasttime);

	if ((dcc->flags & DCC_FILEOFFER) && 
		  (nopath = strrchr(dcc->description, '/')))
		nopath++;
	else
		nopath = dcc->description;

	/*
	 * If this is a DCC SEND...
	 */
	dcc->locked++;
	if (dcc->flags & DCC_FILEOFFER)
	{
		char *	url_name = dcc_urlencode(nopath);

		if (family == AF_INET)
		{
			/*
			 * Dont bother with the checksum.
			 */
			send_ctcp(CTCP_PRIVMSG, dcc->user, CTCP_DCC,
				 "%s %s %lu %hu %ld",
				 type, url_name,
				 (u_long)ntohl(V4ADDR(my_sockaddr).s_addr),
				 ntohs(V4PORT(my_sockaddr)),
				 dcc->filesize);
		}

		/*
		 * Tell the user we sent out the request
		 */
		message_from(NULL, LOG_DCC);
		if (do_hook(DCC_OFFER_LIST, "%s %s %s %ld", 
			dcc->user, type, url_name, dcc->filesize))
		    say("Sent DCC %s request (%s %ld) to %s", 
			type, nopath, dcc->filesize, dcc->user);
		message_from(NULL, LOG_CURRENT);

		new_free(&url_name);
	}

	/*
	 * Otherwise its a DCC CHAT request
	 */
	else
	{
		if (family == AF_INET)
		{
			/*
			 * Send out the handshake
			 */
			send_ctcp(CTCP_PRIVMSG, dcc->user, CTCP_DCC,
				 "%s %s %lu %u", 
				 type, nopath,
				 (u_long)ntohl(V4ADDR(my_sockaddr).s_addr),
				 ntohs(V4PORT(my_sockaddr)));
		}

		/*
		 * And tell the user
		 */
		message_from(NULL, LOG_DCC);
		if (do_hook(DCC_OFFER_LIST, "%s %s", dcc->user, type))
		    say("Sent DCC %s request to %s", type, dcc->user);
		message_from(NULL, LOG_CURRENT);
	}
	dcc->locked--;
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

	if (!(dcc = dcc_searchlist(desc, user, type, 0, NULL, -1)))
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
		if (!strcmp(cmd, "PRIVMSG"))
			strmcpy(tmp, "CTCP_MESSAGE ", DCC_BLOCK_SIZE);
		else
			strmcpy(tmp, "CTCP_REPLY ", DCC_BLOCK_SIZE);
	}

	strmcat(tmp, text, DCC_BLOCK_SIZE);
	strmcat(tmp, "\n", DCC_BLOCK_SIZE); 

	message_from(dcc->user, LOG_DCC);

	if (x_debug & DEBUG_OUTBOUND) 
		yell("-> [%s] [%s]", desc, tmp);

	if (write(dcc->socket, tmp, strlen(tmp)) == -1)
	{
		dcc->flags |= DCC_DELETE;
		say("Outbound write() failed: %s", 
			errno ? strerror(errno) : 
			"What the heck is wrong with your disk?");

		message_from(NULL, LOG_CURRENT);
		from_server = old_from_server;
		return;
	}

	dcc->bytes_sent += strlen(tmp);

	if (noisy)
	{
		dcc->locked++;
		if (do_hook(list, "%s %s", dcc->user, text_display))
			put_it("=> %c%s%c %s", 
				thing, dcc->user, thing, text_display);
		dcc->locked--;
	}

	message_from(NULL, LOG_CURRENT);
	return;
}

/*
 * This is used to send a message to a remote DCC peer.  This is called
 * by send_text.
 */
void	dcc_chat_transmit (char *user, char *text, const char *orig, const char *type, int noisy)
{
	int	fd;

	/*
	 * This converts a message being sent to a number into whatever
	 * its local port is (which is what we think the nickname is).
	 * Its just a 15 minute hack. dont read too much into it.
	 */
	if ((fd = atol(user)))
	{
		DCC_list *	dcc;

		for (dcc = ClientList; dcc; dcc = dcc->next)
			if (dcc->socket == fd)
				break;

		if (!dcc)
		{
			put_it("Descriptor %d is not an open DCC RAW", fd);
			return;
		}

		dcc_message_transmit(DCC_RAW, dcc->user, dcc->description, 
					text, orig, noisy, type);
	}
	else
		dcc_message_transmit(DCC_CHAT, user, "chat",
					text, orig, noisy, type);
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
void	process_dcc(char *args)
{
	char	*command;
	int	i;

	if (!(command = next_arg(args, &args)))
		return;

	for (i = 0; dcc_commands[i].name != NULL; i++)
	{
		if (!my_stricmp(dcc_commands[i].name, command))
		{
			message_from(NULL, LOG_DCC);
			dcc_commands[i].function(args);
			message_from(NULL, LOG_CURRENT);
#if 0
			/*
			 * If the user called /dcc close, and we were
			 * called by do_hook() via an /on, then it may be
			 * very possible that somewhere up the stack is
			 * someone who is expecting their DCC client pointer
			 * to be valid when do_hook() gets back.  Rather than
			 * "fixing" all those callers, we just try not to be
			 * so aggressive about garbage collecting.  We instead
			 * defer to the next call to dcc_check() which will
			 * happen no later than the top of the next minute.
			 */
			dcc_check(NULL);	/* Clean up - XXX Wrong */
#endif
			return;
		}
	}
	say("Unknown DCC command: %s", command);
}


/*
 * Usage: /DCC CHAT <nick> [-e passkey]
 */
static void	dcc_chat (char *args)
{
	char		*user;
	DCC_list	*dcc;
	unsigned short	portnum = 0;

	if ((user = next_arg(args, &args)) == NULL)
	{
		say("You must supply a nickname for DCC CHAT");
		return;
	}

	/*
	 * Check to see if there is a flag
	 */
	if (*args == '-')
	{
		if (args[1] == 'p')
		{
			if (args && *args)
			    portnum = my_atol(next_arg(args, &args));
		}
	}

	dcc = dcc_searchlist("chat", user, DCC_CHAT, 1, NULL, -1);
	if ((dcc->flags & DCC_ACTIVE) || (dcc->flags & DCC_MY_OFFER))
	{
		say("A previous DCC CHAT to %s exists", user);
		return;
	}

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
	int		any_type = 0;
	int		any_user = 0;
	int		i;
	int		count = 0;

	type = next_arg(args, &args);
	user = next_arg(args, &args);
	file = next_arg(args, &args);

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

	while ((dcc = dcc_searchlist(file, user, i, 0, file, -1)))
	{
		unsigned	my_type = dcc->flags & DCC_TYPES;

		count++;
		dcc->flags |= DCC_DELETE;

		dcc->locked++;
                if (do_hook(DCC_LOST_LIST,"%s %s %s USER ABORTED CONNECTION",
			dcc->user, 
			dcc_types[my_type],
                        dcc->description ? dcc->description : "<any>"))
		    say("DCC %s:%s to %s closed", 
			dcc_types[my_type],
			file ? file : "<any>", 
			dcc->user);
		dcc->locked--;
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
	char	*my_local_args = "-all -all";
	char	*breakage = LOCAL_COPY(my_local_args);
	dcc_close(breakage);	/* XXX Bleh */
}

/*
 * Usage: /DCC GET <nick> [file|*]
 * The '*' file gets all offered files.
 */
static	void	dcc_getfile (char *args)
{
	char		*user;
	char		*filename = NULL;
	DCC_list	*dcc;
	char		*fullname = NULL;
	char 		*realname = NULL;
	char		pathname[MAXPATHLEN * 2 + 1];
	int		get_all = 0;
	int		count = 0;

	if (!(user = next_arg(args, &args)))
	{
		say("You must supply a nickname for DCC GET");
		return;
	}

	filename = next_arg(args, &args);
	if (filename && *filename && !strcmp(filename, "*"))
		get_all = 1, filename = NULL;

	while ((dcc = dcc_searchlist(filename, user, DCC_FILEREAD, 0, NULL, 0)))
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

		dcc->flags |= DCC_TWOCLIENTS;
		dcc->open_callback = NULL;
		if (dcc_open(dcc))
		{
			if (get_all)
				continue;
			else
				return;
		}

		*pathname = 0;
		if (get_string_var(DCC_STORE_PATH_VAR))
		{
			strmcpy(pathname, get_string_var(DCC_STORE_PATH_VAR), 
						MAXPATHLEN * 2);
			strlcat(pathname, "/", MAXPATHLEN * 2);
		}

		realname = dcc_urldecode(dcc->description);
		strlcat(pathname, realname, MAXPATHLEN * 2);
		new_free(&realname);

		if (!(fullname = expand_twiddle(pathname)))
			malloc_strcpy(&fullname, pathname);

		dcc->filename = fullname;
		if ((dcc->file = open(fullname, 
				O_WRONLY|O_TRUNC|O_CREAT, 0644)) == -1)
		{
			dcc->flags |= DCC_DELETE;
			say("Unable to open %s: %s", 
				fullname, errno ? 
					strerror(errno) : 
					"<No Error>");
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

/*
 * Calculates transfer speed based on size, start time, and current time.
 */
static char *	calc_speed (off_t sofar, struct timeval sta, struct timeval cur)
{
	static char	buf[7];
	double		tdiff = time_diff(sta, cur);

	if (sofar == 0 || tdiff <= 0.0)
		snprintf(buf, 7, "N/A");
	else
		snprintf(buf, 7, "%4.1f", (double)sofar / tdiff / 1024.0);
	return buf;
}

/*
 * Packs a file size into a smaller representation of Kb, Mb, or Gb.
 * I'm sure it can be done less kludgy.
 */
static char *	calc_size (off_t fsize)
{
	static char	buf[8];

	if (fsize < 1 << 10)
		snprintf(buf, 8, "%ld", (long)fsize);
	else if (fsize < 1 << 20)
		snprintf(buf, 8, "%3.1fKb", (float)fsize / (1 << 10));
	else if (fsize < 1 << 30)
		snprintf(buf, 8, "%3.1fMb", (float)fsize / (1 << 20));
	else
		snprintf(buf, 8, "%3.1fGb", (float)fsize / (1 << 30));

	return buf;
}

/*
 * Usage: /DCC LIST
 */
void	dcc_list (char *args)
{
	DCC_list	*Client;
static	char		*format =
		"%-7.7s%-3.3s %-9.9s %-9.9s %-20.20s %-6.6s %-5.5s %-6.6s %s";

	if (do_hook(DCC_LIST_LIST, "Start * * * * * * *"))
	{
		put_it(format, "Type", " ", "Nick", "Status", "Start time", 
				"Size", "Compl", "Kb/s", "Args");
	}

	for (Client = ClientList ; Client != NULL ; Client = Client->next)
	{
	    unsigned	flags = Client->flags;

	    Client->locked++;
	    if (do_hook(DCC_LIST_LIST, "%s %s %s %s %ld %ld %ld %s",
				dcc_types[flags & DCC_TYPES],
				Client->encrypt ? one : zero,
				Client->user,
					flags & DCC_DELETE       ? "Closed"  :
					flags & DCC_ACTIVE       ? "Active"  :
					flags & DCC_MY_OFFER     ? "Waiting" :
					flags & DCC_THEIR_OFFER  ? "Offered" :
							           "Unknown",
				Client->starttime.tv_sec,
			  (long)Client->filesize,
				Client->bytes_sent ? (u_long)Client->bytes_sent
						   : (u_long)Client->bytes_read,
				Client->description))
	    {
		char *	filename = strrchr(Client->description, '/');
		char	completed[9];
		char	size[9];
		char	speed[9];
		char	buf[23];
		char *	time_f;
		off_t	tot_size;
		off_t	act_sent;

		/*
		 * Figure out something sane for the filename
		 */
		if (!filename || get_int_var(DCC_LONG_PATHNAMES_VAR))
			filename = Client->description;
		else if (filename && *filename)
			filename++;

		if (!filename)
			filename = LOCAL_COPY(ltoa(get_pending_bytes(Client->socket)));

		/*
		 * Figure out how many bytes we have sent for *this*
		 * session, and how many bytes of the file have been
		 * sent *in total*.
		 */
		if (Client->bytes_sent)
		{
			tot_size = Client->bytes_sent;
			act_sent = tot_size - Client->resume_size;
		}
		else
		{
			tot_size = Client->bytes_read;
			act_sent = tot_size - Client->resume_size;
		}

		/*
		 * Figure out something sane for the "completed" and
		 * "size" fields.
		 */
		if (Client->filesize)
		{
			double	prop;
			long	perc;

			prop = (double)tot_size / Client->filesize;
			perc = prop * 100;

			sprintf(completed, "%ld%%", perc);
			strmcpy(size, calc_size(Client->filesize), 8);
		}
		else
		{
			strlcpy(completed, calc_size(tot_size), 9);
			*size = 0;
		}


		/*
		 * Figure out something sane for starting time
		 */
		if (Client->starttime.tv_sec)
		{
			time_t 	blech = Client->starttime.tv_sec;
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
				Client->starttime, get_time(NULL)), 9);
		}
		else
			*speed = 0;

		/*
		 * And do the dirty work.
		 */
		put_it(format,
			dcc_types[flags&DCC_TYPES],
			Client->encrypt ? "[E]" : empty_string,
			Client->user,
			flags & DCC_DELETE      ? "Closed" :
			flags & DCC_ACTIVE      ? "Active" : 
			flags & DCC_MY_OFFER    ? "Waiting" :
			flags & DCC_THEIR_OFFER ? "Offered" : 
						  "Unknown",
			time_f, size, completed, speed, filename);
	    }
	    Client->locked--;
	}
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
	dcc_message_transmit(DCC_RAW, name, host, args, args, 1, NULL);
}

/*
 * Usage: /DCC RENAME <nick> [<oldname>] <newname>
 */
static	void	dcc_rename (char *args)
{
	DCC_list	*dcc;
	char	*user;
	char	*oldf;
	char	*newf;
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

	if ((dcc = dcc_searchlist(oldf, user, type, 0, NULL, -1)))
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
			*fullname,
			*this_arg,
			FileBuf[BIG_BUFFER_SIZE+1];
	u_short		portnum = 0;
	int		filenames_parsed = 0;
	DCC_list	*Client;
	Stat		stat_buf;

	/*
	 * For sure, at least one argument is needed, the target
	 */
	if ((user = next_arg(args, &args)))
	{
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
			continue;
		}

		/*
		 * Ok.  We have a filename they want to send.  Check to
		 * see what kind it is.
		 */

		/*
		 * Absolute pathnames are a can of corn.
		 */
		if (*this_arg == '/')
			strlcpy(FileBuf, this_arg, BIG_BUFFER_SIZE);
		/*
		 * Twiddle pathnames need to be expanded.
		 */
		else if (*this_arg == '~')
		{
			if (!(fullname = expand_twiddle(this_arg)))
			{
				say("Unable to expand %s", this_arg);
				return;
			}
			strlcpy(FileBuf, fullname, BIG_BUFFER_SIZE);
			new_free(&fullname);
		}
		/*
		 * Relative pathnames get the cwd tacked onto them.
		 */
		else
		{
			getcwd(FileBuf, BIG_BUFFER_SIZE);
			strmcat(FileBuf, "/", BIG_BUFFER_SIZE);
			strmcat(FileBuf, this_arg, BIG_BUFFER_SIZE);
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
		if (!strncmp(FileBuf, "/etc/", 5) || 
				!end_strcmp(FileBuf, "/passwd", 7))
		{
			say("Send Request Rejected");
			continue;
		}
#endif

		if (stat(FileBuf, &stat_buf))
		{
			say("The file %s is unaccessable (it doesn't exist or you dont have permission to access it.)", FileBuf);
			continue;
		}

		if (access(FileBuf, R_OK))
		{
			say("Cannot send %s because you dont have read permission", FileBuf);
			continue;
		}

		if (stat_buf.st_mode & S_IFDIR)
		{
			say("Cannot send %s because it is a directory", FileBuf);
			continue;
		}

		/* XXXXX filesize is a global XXXXX */
		filesize = stat_buf.st_size;
		Client = dcc_searchlist(FileBuf, user, DCC_FILEOFFER, 
					1, this_arg, -1);
		filesize = 0;

		if ((Client->flags & DCC_ACTIVE) ||
		    (Client->flags & DCC_MY_OFFER))
		{
			say("Sending a booster CTCP handshake for an existing DCC SEND:%s to %s", FileBuf, user);
			dcc_send_booster_ctcp(Client);
			continue;
		}

		Client->flags |= DCC_TWOCLIENTS;
		Client->want_port = portnum;
		dcc_open(Client);
	    } /* The WHILE */
	} /* The IF */

	if (!filenames_parsed)
		yell("Usage: /DCC SEND <[=]nick> <file> [<file> ...]");
}


/*
 * Usage: $listen(<port>)
 */
char	*dcc_raw_listen (unsigned short port)
{
	DCC_list	*Client;
	char		*PortName;

	message_from(NULL, LOG_DCC);

	if (port && port < 1024)
	{
		say("May not bind to a privileged port");
		message_from(NULL, LOG_CURRENT);
		return m_strdup(empty_string);
	}
	PortName = LOCAL_COPY(ltoa(port));
	Client = dcc_searchlist("raw_listen", PortName, 
					DCC_RAW_LISTEN, 1, NULL, -1);

	if (Client->flags & DCC_ACTIVE)
	{
		say("A previous DCC RAW_LISTEN on %s exists", PortName);
		message_from(NULL, LOG_CURRENT);
		return m_strdup(empty_string);
	}

	Client->want_port = port;
	Client->socket = ip_bindery(AF_INET, Client->want_port, 
				 &Client->local_sockaddr);
	if (Client->socket < 0)
	{
		Client->flags |= DCC_DELETE; 
		say("Couldnt establish listening socket: [%d] %s", 
			Client->socket, my_strerror(errno));
		message_from(NULL, LOG_CURRENT);
		return m_strdup(empty_string);
	}

	new_open(Client->socket);
	get_time(&Client->starttime);
	Client->flags |= DCC_ACTIVE;
	Client->user = m_strdup(ltoa(Client->want_port));
	message_from(NULL, LOG_CURRENT);

	return m_strdup(Client->user);
}


/*
 * Usage: $connect(<hostname> <portnum> <family>)
 */
char	*dcc_raw_connect (const char *host, const char *port, int family)
{
	DCC_list *	Client;
	SS		my_sockaddr;

	memset(&my_sockaddr, 0, sizeof(my_sockaddr));

	message_from(NULL, LOG_DCC);
	if (family == AF_INET)
	{
		FAMILY(my_sockaddr) = AF_INET;
		if (inet_strton(host, port, (SA *)&my_sockaddr, 0))
		{
			say("Unknown host: %s", host);
			message_from(NULL, LOG_CURRENT);
			return m_strdup(empty_string);
		}
	}

	Client = dcc_searchlist(host, port, DCC_RAW, 1, NULL, -1);
	if (Client->flags & DCC_ACTIVE)
	{
		say("A previous DCC RAW to %s on %s exists", host, port);
		message_from(NULL, LOG_CURRENT);
		return m_strdup(empty_string);
	}

	Client->offer = my_sockaddr;
	Client->flags = DCC_THEIR_OFFER | DCC_RAW;
	if (dcc_open(Client))
	{
		message_from(NULL, LOG_CURRENT);
		return m_strdup(empty_string);
	}

	Client->user = m_strdup(ltoa(Client->socket));
	Client->locked++;
	if (do_hook(DCC_RAW_LIST, "%s %s E %s", Client->user, host, port))
            if (do_hook(DCC_CONNECT_LIST,"%s RAW %s %s", 
				Client->user, host, port))
		say("DCC RAW connection to %s on %s via %s established", 
				host, Client->user, port);
	Client->locked--;

	message_from(NULL, LOG_CURRENT);
	return m_strdup(Client->user);
}





/*
 *
 * All the rest of this file is dedicated to automatic replies generated
 * by the client in response to external stimuli from DCC connections.
 *
 */



/*
 * When a user does a CTCP DCC, it comes here for preliminary parsing.
 */
void	register_dcc_offer (char *user, char *type, char *description, char *address, char *port, char *size, char *extra, char *rest)
{
	DCC_list *	Client;
	int		CType, jvs_blah;
	char *		c;
	int		do_auto = 0;	/* used in dcc chat collisions */
	u_short		realport;	/* Bleh */
	char 		p_addr[256];
	int		err;

	if (x_debug & DEBUG_DCC_SEARCH)
		yell("register_dcc_offer: [%s|%s|%s|%s|%s|%s|%s]", user, type, description, address, port, size, extra);

	if ((c = strrchr(description, '/')))
		description = c + 1;
	if ('.' == *description)
		*description = '_';

	if (size && *size)
		filesize = my_atol(size);

	     if (!my_stricmp(type, "CHAT"))
		CType = DCC_CHAT;
	else if (!my_stricmp(type, "SEND"))
		CType = DCC_FILEREAD;
#ifdef MIRC_BROKEN_DCC_RESUME
	else if (!my_stricmp(type, "RESUME"))
	{
		/* 
		 * Dont be deceieved by the arguments we're passing it.
		 * The arguments are "out of order" because MIRC doesnt
		 * send them in the traditional order.  Ugh.
		 */
		dcc_getfile_resume_demanded(user, description, address, port);
		return;
	}
	else if (!my_stricmp(type, "ACCEPT"))
	{
		/*
		 * See the comment above.
		 */
		dcc_getfile_resume_start (user, description, address, port);
		return;
	}
#endif
        else
        {
                say("Unknown DCC %s (%s) received from %s", 
				type, description, user);
                return;
        }

	Client = dcc_searchlist(description, user, CType, 1, NULL, -1);
	filesize = 0;

	if (Client->flags & DCC_MY_OFFER)
	{
		Client->flags |= DCC_DELETE;

		if (DCC_CHAT == CType)
		{
			Client = dcc_searchlist(description, user, CType, 1, NULL, -1);
			do_auto = 1;
		}
		else
		{
			say("DCC %s collision for %s:%s", type, user, description);
			send_ctcp(CTCP_NOTICE, user, CTCP_DCC,
				"DCC %s collision occured while connecting to %s (%s)", 
				type, get_server_nickname(from_server), description);
			return;
		}
	}
	if (Client->flags & DCC_ACTIVE)
	{
		say("Received DCC %s request from %s while previous session still active", type, user);
		return;
	}
	Client->flags |= DCC_THEIR_OFFER;

	/* Make sure the struct sockaddr_storage is clear. */
	memset(&Client->offer, 0, sizeof(Client->offer));

	/*
	 * Fill in the "offer" sockaddr.
	 * Unfortunately, we have to check the port like this because
	 * putting and getting the port out of a sockaddr is a PITA.
	 */
	realport = strtoul(port, NULL, 10);	/* bleh */
	if (realport < 1024)
	{
		Client->flags |= DCC_DELETE;
		say("DCC %s (%s) request from %s rejected because it "
			"specified reserved port number (%hu) [%s]", 
				type, description, user, 
				realport, port);
		return;
	}

	/*
	 * Convert the handshake address to a sockaddr.  If it cannot be
	 * figured out, warn the user and punt.
	 */
	V4FAM(Client->offer) = AF_UNSPEC;
	if ((err = inet_strton(address, port, (SA *)&Client->offer, AI_NUMERICHOST)))
	{
		say("DCC %s (%s) request from %s had mangled return address "
			"[%s] (%d)", type, description, user, address, err);
		return;
	}

	/*
	 * Convert the sockaddr back to a name that we can print.
	 */
	if (!inet_ntostr((SA *)&Client->offer, socklen((SA *)&Client->offer),
			p_addr, 256, NULL, 0, NI_NUMERICHOST))
	{
		say("DCC %s (%s) request from %s could not be converted back "
		    "into a p-addr [%s] [%s]", 
			type, description, user, address, port);
		return;
	}

#ifdef HACKED_DCC_WARNING
	/* 
	 * This right here compares the hostname from the userhost stamped
	 * on the incoming privmsg to the address stamped in the handshake.
	 * We do not automatically reject any discrepencies, but warn the
	 * user instead to be cautious.
	 */
	if (FAMILY(Client->offer) == AF_INET)
	{
		char 	*fromhost;
		SS	irc_addr;

		if (!(fromhost = strchr(FromUserHost, '@')))
		{
			yell("### Incoming handshake from a non-user peer!");
			return;
		}

		fromhost++;
		FAMILY(irc_addr) = FAMILY(Client->offer);
		if (inet_strton(fromhost, port, (SA *)&irc_addr, 0))
		{
			yell("### Incoming handshake has an address or port [%s:%s] that could not be figured out!", fromhost, port);
			yell("### Please use caution in deciding whether to accept it or not");
		}
		else if (FAMILY(Client->offer) == AF_INET)
		{
		   if (V4ADDR(irc_addr).s_addr != V4ADDR(Client->offer).s_addr)
		   {
			say("WARNING: Fake dcc handshake detected! [%x]", V4ADDR(Client->offer).s_addr);
			say("Unless you know where this dcc request is coming from");
			say("It is recommended you ignore it!");
		   }
		}
	}
#endif

	/*
	 * Now check to make sure that the address is reasonable.
	 */
	if (FAMILY(Client->offer) == AF_INET)
	{
	    if (V4ADDR(Client->offer).s_addr == 0)
	    {
		Client->flags |= DCC_DELETE;
		yell("### DCC handshake from %s ignored becuase it had an null address", user);
		return;
	    }
	}

  	if (do_auto)
  	{
                if (do_hook(DCC_CONNECT_LIST,"%s CHAT", user))
			say("DCC CHAT already requested by %s, connecting...", user);
  		dcc_chat(user);
		return;
	}

	/* 
	 * DCC SEND and CHAT have different arguments, so they can't
	 * very well use the exact same hooked data.  Both now are
	 * identical for $0-4, and SEND adds filename/size in $5-6 
	 */
	Client->locked++;
	if ((Client->flags & DCC_TYPES) == DCC_FILEREAD)
	       jvs_blah = do_hook(DCC_REQUEST_LIST, "%s %s %s %s %s %s %ld",
			  user, type, description, p_addr, port,
			  Client->description, Client->filesize);
	else
	       jvs_blah = do_hook(DCC_REQUEST_LIST, "%s %s %s %s %s",
			  user, type, description, p_addr, port);
	Client->locked--;

	if (jvs_blah)
	{
	    /* These should be more helpful messages now. */
	    /* WC asked to have them moved here */
	    if ((Client->flags & DCC_TYPES) == DCC_FILEREAD)
	    {
		struct stat statit;
		char *realpath;

		if (get_string_var(DCC_STORE_PATH_VAR))
		    realpath = m_sprintf("%s/%s", get_string_var(DCC_STORE_PATH_VAR), Client->description);
		else
		    realpath = m_strdup(Client->description);


		if (stat(realpath, &statit) == 0)
		{
		    if (statit.st_size < Client->filesize)
		    {
			say("WARNING: File [%s] exists but is smaller then the file offered.", realpath);
			say("Use /DCC CLOSE GET %s %s        to not get the file.", user, Client->description);
#ifdef MIRC_BROKEN_DCC_RESUME
			say("Use /DCC RESUME %s %s           to continue the copy where it left off.", user, Client->description);
#endif
			say("Use /DCC RENAME %s %s newname   to save it to a different filename", user, Client->description);
			say("Use /DCC GET %s %s              to overwrite the existing file.", user, Client->description);
		    }
		    else if (statit.st_size == Client->filesize)
		    {
			say("WARNING: File [%s] exists, and its the same size.", realpath);
			say("Use /DCC CLOSE GET %s %s        to not get the file.", user, Client->description);
			say("Use /DCC RENAME %s %s           to get the file anyways under a different name.", user, Client->description);
			say("Use /DCC GET %s %s              to overwrite the existing file.", user, Client->description);
		    }
		    else 	/* Bigger than */
		    {
			say("WARNING: File [%s] already exists.", realpath);
			say("Use /DCC CLOSE GET %s %s        to not get the file.", user, Client->description);
			say("Use /DCC RENAME %s %s           to get the file anyways under a different name.", user, Client->description);
			say("Use /DCC GET %s %s              to overwrite the existing file.", user, Client->description);
		    }
		}
		new_free(&realpath);
	    }

	    /* Thanks, Tychy! (lherron@imageek.york.cuny.edu) */
	    if ((Client->flags & DCC_TYPES) == DCC_FILEREAD)
		say("DCC %s (%s %ld) request received from %s!%s [%s:%s]",
			type, description, Client->filesize, user, 
			FromUserHost, p_addr, port);
	    else
		say("DCC %s (%s) request received from %s!%s [%s:%s]", 
			type, description, user, FromUserHost, p_addr, port);
	}

	get_time(&Client->lasttime);
	get_time(&Client->starttime);
	return;
}


/*
 * Check all DCCs for data, and if they have any, perform whatever
 * actions are required.
 */
void	dcc_check (fd_set *Readables)
{
	DCC_list	*Client;
	int		previous_server;

	/* Garbage collect whenever we can */
	if (deadlist)
		dcc_really_erase();

	/* Sanity */
	if (!Readables)
		return;

	/* Whats with all this double-pointer chicanery anyhow? */
	for (Client = ClientList; Client; Client = Client->next)
	{
		/* 
		 * There are some ways that this can happen.
		 * Rather than track them all down and "fix" them,
		 * I'll just conceed that this is possible and skip them.
		 */
		if (Client->flags & DCC_DELETE)
		{
			dcc_add_deadclient(Client);
			continue;
		}

		if (Readables && Client->socket != -1 && FD_ISSET(Client->socket, Readables))
		{
			FD_CLR(Client->socket, Readables);	/* No more! */
			previous_server = from_server;
			from_server = -1;
			message_from(NULL, LOG_DCC);

			switch (Client->flags & DCC_TYPES)
			{
				case DCC_CHAT:
					process_incoming_chat(Client);
					break;
				case DCC_RAW_LISTEN:
					process_incoming_listen(Client);
					break;
				case DCC_RAW:
					process_incoming_raw(Client);
					break;
				case DCC_FILEOFFER:
					process_outgoing_file(Client);
					break;
				case DCC_FILEREAD:
					process_incoming_file(Client);
					break;
			}

			message_from(NULL, LOG_CRAP);
			from_server = previous_server;
		}
		/*
		 * Enforce any timeouts
		 */
		else if (Client->flags & (DCC_MY_OFFER | DCC_THEIR_OFFER) &&
			 dcc_timeout >= 0 &&
			 time_diff(Client->lasttime, now) > dcc_timeout)
		{
			Client->locked++;
			if (do_hook(DCC_LOST_LIST,"%s %s %s IDLE TIME EXCEEDED",
				Client->user, 
				dcc_types[Client->flags & DCC_TYPES],
				Client->description ? 
				 Client->description : "<any>"))
			    say("Auto-rejecting a dcc after [%ld] seconds", 
				(long)time_diff(Client->lasttime, now));

			/* 
			 * This is safe to do after, since the connection
			 * is still open, and the user might want to send
			 * something to the other peer here, and there is no
			 * good reason not to let them.
			 */
			Client->flags |= DCC_DELETE;
			Client->locked--;
		}


		/*
		 * This shouldnt be neccesary any more, but what the hey,
		 * im still paranoid.
		 */
		if (!Client)
			break;

		if (Client->flags & DCC_DELETE)
			dcc_add_deadclient(Client);
	}
}



/*
 * This handles DCC CHAT messages sent to you.
 */
static	void	process_incoming_chat (DCC_list *Client)
{
	char	tmp[IO_BUFFER_SIZE + 1];
	char	tmp2[IO_BUFFER_SIZE + 1];
	char	*bufptr;
	long	bytesread;
	int	fd;

	if (Client->flags & DCC_MY_OFFER)
	{
		SS	remaddr;
		int	sra;
		char	p_addr[256];
		char	p_port[24];
		SA *	addr;

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
		inet_ntostr(addr, socklen(addr), p_addr, 256, 
				p_port, 24, NI_NUMERICHOST);

		Client->locked++;
                if (do_hook(DCC_CONNECT_LIST, "%s CHAT %s %s", 
					Client->user, p_addr, p_port))
		    say("DCC chat connection to %s[%s:%s] established", 
					Client->user, p_addr, p_port);
		Client->locked--;

		get_time(&Client->starttime);
		return;
	}

        bufptr = tmp;
	bytesread = dgets(bufptr, Client->socket, 1);

	switch ((int)bytesread)
	{
	case -1:
	{
		char *	real_tmp = ((dgets_errno == -1) ? 
				"Remote End Closed Connection" : 
				strerror(dgets_errno));

		Client->flags |= DCC_DELETE;
		Client->locked++;
                if (do_hook(DCC_LOST_LIST, "%s CHAT %s", 
				Client->user, real_tmp))
        		say("DCC CHAT connection to %s lost [%s]", 
				Client->user, real_tmp);
		Client->locked--;
		return;
	}
	case 0:		/* We do buffering, so just ignore incompletes */
		break;
	default:
	{
		char 	uh[80];
		char 	equal_nickname[80];
		char	p_addr[256];
		SA *	addr;

		chomp(tmp);
		my_decrypt(tmp, strlen(tmp), Client->encrypt);
		Client->bytes_read += bytesread;
		message_from(Client->user, LOG_DCC);

		if (x_debug & DEBUG_INBOUND) 
			yell("%s", tmp);

		addr = (SA *)&Client->peer_sockaddr;
		inet_ntostr(addr, socklen(addr), p_addr, 256, 
				NULL, 0, NI_NUMERICHOST);

#define CTCP_MESSAGE "CTCP_MESSAGE "
#define CTCP_MESSAGE_LEN strlen(CTCP_MESSAGE)
#define CTCP_REPLY "CTCP_REPLY "
#define CTCP_REPLY_LEN strlen(CTCP_REPLY)

		if (*tmp == CTCP_DELIM_CHAR)
		{
			snprintf(uh, 80, "Unknown@%s", p_addr);
			FromUserHost = uh;
			snprintf(equal_nickname, 80, "=%s", Client->user);

			/*
			 * 'tmp' and 'tmp2' are the same size, so this
			 * strcpy is safe.
			 *
			 * XXX - That doesnt make what im doing here any
			 * less of a hack, though. 
			 */
			strcpy(tmp2, tmp);

			/* do_ctcp returns 'tmp2' here, so this is safe. */
			strcpy(tmp, do_ctcp(equal_nickname, nickname, tmp2));
			FromUserHost = empty_string;
		}
		if (!strncmp(tmp, CTCP_MESSAGE, CTCP_MESSAGE_LEN))
		{
			snprintf(uh, 80, "Unknown@%s", p_addr);
			FromUserHost = uh;
			snprintf(equal_nickname, 80, "=%s", Client->user);

			/* See above for why these are safe. */
			strcpy(tmp2, tmp);
			strcpy(tmp, do_ctcp(equal_nickname, nickname, 
						tmp2 + CTCP_MESSAGE_LEN));
			FromUserHost = empty_string;
		}
		else if (!strncmp(tmp, CTCP_REPLY, CTCP_REPLY_LEN) || *tmp == CTCP_DELIM_CHAR)
		{
			snprintf(uh, 80, "Unknown@%s", p_addr);
			FromUserHost = uh;
			snprintf(equal_nickname, 80, "=%s", Client->user);

			/* See above for why these are safe. */
			strcpy(tmp2, tmp);
			strcpy(tmp, do_notice_ctcp(equal_nickname, nickname,
				tmp2 + ((*tmp2 == CTCP_DELIM_CHAR) ? 0 : CTCP_REPLY_LEN)));
			FromUserHost = empty_string;
		}

		if (!*tmp)
			break;

		Client->locked++;
		if (do_hook(DCC_CHAT_LIST, "%s %s", Client->user, tmp))
		{
			if (get_server_away(-2))
			{
				strlcat(tmp, "<", IO_BUFFER_SIZE);
				strlcat(tmp, my_ctime(time(NULL)), 
						IO_BUFFER_SIZE);
				strlcat(tmp, ">", IO_BUFFER_SIZE);
			}
			put_it("=%s= %s", Client->user, tmp);
		}
		Client->locked--;
		message_from(NULL, LOG_CURRENT);
		return;
	}
	}
}


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
	int		len;
	char		p_port[24];

	sra = sizeof(remaddr);
	new_socket = Accept(Client->socket, (SA *) &remaddr, &sra);
	if (new_socket < 0)
	{
		yell("### DCC Error: accept() failed.  Punting.");
		return;
	}

	*host = 0;
	inet_ntostr((SA *)&remaddr, socklen((SA *)&remaddr), host, sizeof(host), p_port, sizeof(p_port), 0);

	strlcpy(fdstr, ltoa(new_socket), 10);
	NewClient = dcc_searchlist(host, fdstr, DCC_RAW, 1, NULL, 0);
	NewClient->socket = new_socket;

	NewClient->peer_sockaddr = remaddr;
	NewClient->offer = remaddr;

	len = sizeof(NewClient->local_sockaddr);
	getsockname(NewClient->socket, (SA *)&NewClient->local_sockaddr, &len);

	NewClient->flags |= DCC_ACTIVE;
	NewClient->bytes_read = NewClient->bytes_sent = 0L;
	get_time(&NewClient->starttime);
	new_open(NewClient->socket);

	Client->locked++;
	if (do_hook(DCC_RAW_LIST, "%s %s N %s", 
			NewClient->user, NewClient->description, p_port))
            if (do_hook(DCC_CONNECT_LIST,"%s RAW %s %s", 
			NewClient->user, NewClient->description, p_port))
		say("DCC RAW connection to %s on %s via %s established",
			NewClient->description, NewClient->user, p_port);
	Client->locked--;
}



/*
 * This handles when someone sends you a line of info over a DCC RAW
 * connection (that was established with a $listen().
 */
static	void		process_incoming_raw (DCC_list *Client)
{
	char	tmp[IO_BUFFER_SIZE + 1];
	char 	*bufptr;
	long	bytesread;

        bufptr = tmp;
	switch ((int)(bytesread = dgets(bufptr, Client->socket, 0)))
	{
	    case -1:
	    {
		Client->flags |= DCC_DELETE;
		Client->locked++;
		if (do_hook(DCC_RAW_LIST, "%s %s C",
				Client->user, Client->description))
       		if (do_hook(DCC_LOST_LIST,"%s RAW %s", 
				Client->user, Client->description))
			say("DCC RAW connection to %s on %s lost",
				Client->user, Client->description);
		Client->locked--;
		break;
	    }
	    case 0:
	    default:
	    {
		from_server = primary_server;	/* Colten asked for it */
		if (bytesread > 0 && tmp[strlen(tmp) - 1] == '\n')
			tmp[strlen(tmp) - 1] = '\0';
		Client->bytes_read += bytesread;
		Client->locked++;
		if (do_hook(DCC_RAW_LIST, "%s %s D %s",
				Client->user, Client->description, tmp))
			say("Raw data on %s from %s: %s",
				Client->user, Client->description, tmp);
		Client->locked--;
	    }
	}
	return;
}


/*
 * When youre sending a file, and your peer sends an ACK, this handles
 * whether or not to send the next packet.
 */
static void		process_outgoing_file (DCC_list *Client)
{
	SS		remaddr;
	int		sra;
	char		tmp[DCC_BLOCK_SIZE+1];
	u_32int_t	bytesrecvd;
	int		bytesread;
	int		old_from_server = from_server;
	int		maxpackets;
	fd_set		fd;
	int		size;
	Timeval		to;
	int		new_fd;
	char		p_addr[256];
	char		p_port[24];
	SA *		addr;

	/*
	 * The remote user has accepted the file offer
	 */
	if (Client->flags & DCC_MY_OFFER)
	{
		/*
		 * Open up the network connection
		 */
		sra = sizeof(remaddr);
		new_fd = Accept(Client->socket, (SA *)&Client->peer_sockaddr, &sra);
		Client->socket = new_close(Client->socket);
		if ((Client->socket = new_fd) < 0)
		{
			Client->flags |= DCC_DELETE;
			yell("### DCC Error: accept() failed.  Punting.");
			return;
		}
		new_open(Client->socket);
		Client->flags &= ~DCC_MY_OFFER;
		Client->flags |= DCC_ACTIVE;
		get_time(&Client->starttime);

#ifdef HAVE_SO_SNDLOWAT
		/*
		 * Give a hint to the OS how many bytes we need to send
		 * for each write()
		 */
		size = DCC_BLOCK_SIZE;
		if (setsockopt(Client->socket, SOL_SOCKET, SO_SNDLOWAT, 
					&size, sizeof(size)) < 0)
			say("setsockopt failed: %s", strerror(errno));
#endif

		addr = (SA *)&Client->peer_sockaddr;
		inet_ntostr(addr, socklen(addr), p_addr, 256, 
				p_port, 24, NI_NUMERICHOST);

		/*
		 * Tell the user
		 */
		Client->locked++;
                if (do_hook(DCC_CONNECT_LIST, "%s SEND %s %s %s %ld",
				Client->user, p_addr, p_port,
				Client->description, Client->filesize))
		    say("DCC SEND connection to %s[%s:%s] established", 
				Client->user, p_addr, p_port);
		Client->locked--;

		/*
		 * Open up the file to be sent
		 */
		if ((Client->file = open(Client->description, O_RDONLY)) == -1)
		{
			Client->flags |= DCC_DELETE;
			say("Unable to open %s: %s", Client->description,
				errno ? strerror(errno) : "Unknown Host");
			return;
		}

		/*
		 * Set up any DCC RESUME as needed
		 */
		if (Client->bytes_sent)
		{
			if (x_debug & DEBUG_DCC_XMIT)
				yell("Resuming at address (%ld)", 
					(long)Client->bytes_sent);
			lseek(Client->file, Client->bytes_sent, SEEK_SET);
		}
	}

	/*
	 * The remote user has acknowledged a packet
	 */
	else 
	{ 
		if (x_debug & DEBUG_DCC_XMIT)
			yell("Reading a packet from [%s:%s(%ld)]", 
				Client->user, 
				Client->othername, 
				Client->packets_ack);

		/*
		 * It is important to note here that the ACK must always
		 * be exactly four bytes.  Never more, never less.
		 */
		if (read(Client->socket, (char *)&bytesrecvd, sizeof(u_32int_t)) < (int) sizeof(u_32int_t))
		{
			Client->flags |= DCC_DELETE;

			Client->locked++;
                        if (do_hook(DCC_LOST_LIST,"%s SEND %s CONNECTION LOST",
                                Client->user, Client->description))
	       		    say("DCC SEND:%s connection to %s lost",
				Client->description, Client->user);
			Client->locked--;
			return;
		}
		bytesrecvd = ntohl(bytesrecvd);

		/*
		 * Check to see if we need to move the sliding window up
		 */
		if (bytesrecvd >= (Client->packets_ack + 1) * DCC_BLOCK_SIZE)
		{
			if (x_debug & DEBUG_DCC_XMIT)
				yell("Packet #%ld ACKed", Client->packets_ack);
			Client->packets_ack++;
			Client->packets_outstanding--;
		}

		if (bytesrecvd > Client->bytes_sent)
		{
yell("### WARNING!  The other peer claims to have recieved more bytes than");
yell("### I have actually sent so far.  Please report this to ");
yell("### jnelson@acronet.net or #epic on EFNet right away.");
yell("### Ask the person who you sent the file to look for garbage at the");
yell("### end of the file you just sent them.  Please enclose that ");
yell("### information as well as the following:");
yell("###");
yell("###    bytesrecvd [%ld]        Client->bytes_sent [%ld]", 
				ntohl(bytesrecvd), (long)Client->bytes_sent);
yell("###    Client->filesize [%ld]  Client->packets_ack [%ld]",
				Client->filesize, Client->packets_ack);
yell("###    Client->packets_outstanding [%ld]",
				Client->packets_outstanding);

			/* And just cope with it to avoid whining */
			Client->bytes_sent = bytesrecvd;
		}
	
		/*
		 * If we've sent the whole file already...
		 */
		if (Client->bytes_sent >= Client->filesize)
		{
			/*
			 * If theyve ACKed the last packet, we close 
			 * the connection.
			 */
			if (bytesrecvd >= Client->filesize)
				DCC_close_filesend(Client, "SEND");

			/*
			 * Either way there's nothing more to do.
			 */
			return;
		}
	}

	/*
	 * In either case, we need to send some more data to the peer.
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

	while (Client->packets_outstanding < maxpackets)
	{
		/*
		 * Check to make sure the write won't block.
		 */
		FD_SET(Client->socket, &fd);
		to.tv_sec = 0;
		to.tv_usec = 0;
		if (select(Client->socket + 1, NULL, &fd, NULL, &to) <= 0)
			break;

		/*
		 * Grab some more file.  If this chokes, dont sweat it.
		 */
		if ((bytesread = read(Client->file, tmp, DCC_BLOCK_SIZE)) <= 0)
			break;


		/*
		 * Bug the user
		 */
		if (x_debug & DEBUG_DCC_XMIT)
		    yell("Sending packet [%s [%s] (packet %ld) (%d bytes)]",
			Client->user, 
			Client->othername, 
			Client->packets_transfer, 
			bytesread);

		/*
		 * Attempt to write the file.  If it chokes, whine.
		 */
		if (write(Client->socket, tmp, bytesread) < bytesread)
		{
			Client->flags |= DCC_DELETE;
			say("Outbound write() failed: %s", 
				errno ? strerror(errno) : 
					"I'm not sure why");

			message_from(NULL, LOG_CURRENT);
			from_server = old_from_server;
			return;
		}

		Client->packets_outstanding++;
		Client->packets_transfer++;
		Client->bytes_sent += bytesread;

		/*
		 * Update the status bar
		 */
		if (Client->filesize)
		{
		    update_transfer_buffer("(to %10s: %d of %d: %d%%)",
			Client->user, 
			Client->packets_transfer, 
			Client->packets_total, 
			(Client->bytes_sent * 100 / Client->filesize));
		}
		else
		{
		    update_transfer_buffer("(to %10s: %d kbytes     )",
			Client->user, 
			Client->bytes_sent / 1024);
		}
		update_all_status();
	}
}

/*
 * When youre recieving a DCC GET file, this is called when the sender
 * sends you a portion of the file. 
 */
static	void		process_incoming_file (DCC_list *Client)
{
	char		tmp[DCC_BLOCK_SIZE+1];
	u_32int_t	bytestemp;
	int		bytesread;

	if ((bytesread = read(Client->socket, tmp, DCC_BLOCK_SIZE)) <= 0)
	{
		if (Client->bytes_read < Client->filesize)
			say("DCC GET to %s lost -- Remote peer closed connection", Client->user);
		DCC_close_filesend(Client, "GET");
		return;
	}

	my_decrypt(tmp, bytesread, Client->encrypt);
	if ((write(Client->file, tmp, bytesread)) == -1)
	{
		Client->flags |= DCC_DELETE;
		say("Write to local file [%d] failed, giving up: [%s]", Client->file, strerror(errno));
		return;
	}

	Client->bytes_read += bytesread;
	bytestemp = htonl(Client->bytes_read);
	if (write(Client->socket, (char *)&bytestemp, sizeof(u_32int_t)) == -1)
	{
		Client->flags |= DCC_DELETE;
		yell("### Write to remote peer failed.  Giving up.");
		return;
	}

	Client->packets_transfer = Client->bytes_read / DCC_BLOCK_SIZE;

/* TAKE THIS OUT IF IT CAUSES PROBLEMS */
	if ((Client->filesize) && (Client->bytes_read > Client->filesize))
	{
		Client->flags |= DCC_DELETE;
		yell("### DCC GET WARNING: incoming file is larger then the handshake said");
		yell("### DCC GET: Closing connection");
		return;
	}

	if (((Client->flags & DCC_TYPES) == DCC_FILEOFFER) || 
	     ((Client->flags & DCC_TYPES) == DCC_FILEREAD))
	{
		if (Client->filesize)
			update_transfer_buffer("(%10s: %d of %d: %d%%)", Client->user, Client->packets_transfer, Client->packets_total, Client->bytes_read * 100 / Client->filesize);
		else
			update_transfer_buffer("(%10s %d packets: %dK)", Client->user, Client->packets_transfer, Client->bytes_read / 1024);
		update_all_status();
	}
}





/*
 * This is a callback.  When we want to do a CTCP DCC REJECT, we do
 * a WHOIS to make sure theyre still on irc, no sense sending it to
 * nobody in particular.  When this gets called back, that means the
 * peer is indeed on irc, so we send them the REJECT.
 *
 * -- Changed this to be quasi-reentrant. yuck.
 */
static	void 	output_reject_ctcp (char *original, char *received)
{
	char	*nickname_requested;
	char	*nickname_recieved;
	char	*type;
	char	*description;

	/*
	 * XXX This is, of course, a monsterous hack.
	 */
	nickname_requested 	= next_arg(original, &original);
	type 			= next_arg(original, &original);
	description 		= next_arg(original, &original);
	nickname_recieved 	= next_arg(received, &received);

	if (nickname_recieved && *nickname_recieved)
		send_ctcp(CTCP_NOTICE, nickname_recieved, CTCP_DCC,
				"REJECT %s %s", type, description);
}



/*
 * This is called when someone sends you a CTCP DCC REJECT.
 */
void 	dcc_reject (char *from, char *type, char *args)
{
	DCC_list *	Client;
	char *		description;
	int		CType;

	for (CType = 0; dcc_types[CType] != NULL; CType++)
		if (!my_stricmp(type, dcc_types[CType]))
			break;

	if (!dcc_types[CType])
		return;

	description = next_arg(args, &args);

	if ((Client = dcc_searchlist(description, from, CType, 0, description, -1)))
	{
		Client->flags |= DCC_REJECTED;
		Client->flags |= DCC_DELETE; 

		Client->locked++;
                if (do_hook(DCC_LOST_LIST,"%s %s %s REJECTED", from, type,
                        description ? description : "<any>"))
		    say("DCC %s:%s rejected by %s: closing", type,
			description ? description : "<any>", from);
		Client->locked--;
	}
}


static void DCC_close_filesend (DCC_list *Client, char *info)
{
	char	lame_ultrix[10];	/* should be plenty */
	char	lame_ultrix2[10];
	char	lame_ultrix3[10];
	double 	xtime, xfer;

	xtime = time_diff(Client->starttime, get_time(NULL));
	if (Client->bytes_sent)
		xfer = (double)(Client->bytes_sent - Client->resume_size);
	else
		xfer = (double)(Client->bytes_read - Client->resume_size);
	sprintf(lame_ultrix, "%2.4g", (xfer / 1024.0 / xtime));

	/* Cant pass %g to put_it (lame ultrix/dgux), fix suggested by sheik. */
	if (xfer <= 0)
		xfer = 1;
	sprintf(lame_ultrix2, "%2.4g", xfer / 1024.0);

	if (xtime <= 0)
		xtime = 1;
	sprintf(lame_ultrix3, "%2.6g", xtime);

	Client->flags |= DCC_DELETE;

	Client->locked++;
	if (do_hook(DCC_LOST_LIST,"%s %s %s %s TRANSFER COMPLETE",
		Client->user, info, Client->description, lame_ultrix))
	     say("DCC %s:%s [%skb] with %s completed in %s sec (%s kb/sec)",
		info, Client->description, lame_ultrix2, Client->user, 
		lame_ultrix3, lame_ultrix);
	Client->locked--;
}



/* 
 * Looks for the dcc transfer that is "current" (last recieved data)
 * and returns information for it
 */
char *	DCC_get_current_transfer (void)
{
	return DCC_current_transfer_buffer;
}


static void 	update_transfer_buffer (char *format, ...)
{
	if (format)
	{
		va_list args;
		va_start(args, format);
		vsnprintf(DCC_current_transfer_buffer, 255, format, args);
		va_end(args);
	}
	else
		*DCC_current_transfer_buffer = 0;

}


static	char *	dcc_urlencode (const char *s)
{
	const char *p1;
	char *str, *p2;

	str = m_strdup(s);

	for (p1 = s, p2 = str; *p1; p1++)
	{
		if (*p1 == '\\')
			continue;
		*p2++ = *p1;
	}

	*p2 = 0;
	return urlencode(str);
}

static	char *	dcc_urldecode (const char *s)
{
	char *str, *p1;

	str = m_strdup(s);
	urldecode(str, NULL);

	for (p1 = str; *p1; p1++)
	{
		if (*p1 != '.')
			break;
		*p1 = '_';
	}

	for (p1 = str; *p1; p1++)
	{
		if (*p1 == '/')
			*p1 = '_';
	}

	return str;
}

/*
 * This stuff doesnt conform to the protocol.
 * Thanks mirc for disregarding the protocol.
 */
#ifdef MIRC_BROKEN_DCC_RESUME

/*
 * Usage: /DCC RESUME <nick> [file] [-e passkey]
 */
static	void	dcc_getfile_resume (char *args)
{
	char		*user;
	char		*filename = NULL;
	DCC_list	*Client;
	char		*passwd = NULL;
	struct stat	sb;
	int		old_dp, old_dn, old_dc;

	if (!get_int_var(MIRC_BROKEN_DCC_RESUME_VAR))
		return;

	if (!(user = next_arg(args, &args)))
	{
		say("You must supply a nickname for DCC RESUME");
		return;
	}

	if (args && *args)
	{
		/* Leeme lone, Yoshi. :P */
		if (args[0] != '-' || args[1] != 'e')
			filename = next_arg(args, &args);

		if (args && args[0] == '-' && args[1] == 'e')
		{
			next_arg(args, &args);
			passwd = next_arg(args, &args);
		}
	}

	/*
	 * This has to be done by hand, we cant use send_ctcp,
	 * because this violates the protocol, and send_ctcp checks
	 * for that.  Ugh.
	 */
	if (stat(filename, &sb) == -1)
	{
		/* File doesnt exist.  Sheesh. */
		say("DCC RESUME: Cannot use DCC RESUME if the file doesnt exist. [%s|%s]", filename, strerror(errno));
		return;
	}


	if (!(Client = dcc_searchlist(filename, user, DCC_FILEREAD, 0, NULL, 0)))
	{
		if (filename)
			say("No file (%s) offered in SEND mode by %s", filename, user);
		else
			say("No file offered in SEND mode by %s", user);
		return;
	}

	if ((Client->flags & DCC_ACTIVE) || (Client->flags & DCC_MY_OFFER))
	{
		say("A previous DCC GET:%s to %s exists", filename?filename:"<any>", user);
		return;
	}

	if (passwd)
		Client->encrypt = m_strdup(passwd);
	Client->bytes_sent = 0L;
	Client->bytes_read = Client->resume_size = sb.st_size;

	if (((SA *)&Client->offer)->sa_family == AF_INET)
		malloc_strcpy(&Client->othername, 
				ltoa(ntohs(V4PORT(Client->offer))));

	if (x_debug & DEBUG_DCC_XMIT)
		yell("SENDING DCC RESUME to [%s] [%s|%s|%ld]", user, filename, Client->othername, (long)sb.st_size);

	/* Just in case we have to fool the protocol enforcement. */
	old_dp = doing_privmsg;
	old_dn = doing_notice;
	old_dc = in_ctcp_flag;

	doing_privmsg = doing_notice = in_ctcp_flag = 0;
	send_ctcp(CTCP_PRIVMSG, user, CTCP_DCC, "RESUME %s %s %ld", 
		filename, Client->othername, (long)sb.st_size);

	doing_privmsg = old_dp;
	doing_notice = old_dn;
	in_ctcp_flag = old_dc;

	/* Then we just sit back and wait for the reply. */
}

/*
 * When the peer demands DCC RESUME
 * We send out a DCC ACCEPT
 */
static void dcc_getfile_resume_demanded (char *user, char *filename, char *port, char *offset)
{
	DCC_list	*Client;
	int		old_dp, old_dn, old_dc;

	if (!get_int_var(MIRC_BROKEN_DCC_RESUME_VAR))
		return;

	if (x_debug & DEBUG_DCC_XMIT)
		yell("GOT DCC RESUME REQUEST from [%s] [%s|%s|%s]", user, filename, port, offset);

	if (!strcmp(filename, "file.ext"))
		filename = NULL;

	if (!(Client = dcc_searchlist(filename, user, DCC_FILEOFFER, 0, port, 0)))
	{
		if (x_debug & DEBUG_DCC_XMIT)
			yell("Resume request that doesnt exist.  Hmmm.");
		return;		/* Its a fake. */
	}

	if (!offset)
		return;		/* Its a fake */

	Client->bytes_sent = Client->resume_size = my_atol(offset);
	Client->bytes_read = 0L;

	/* Just in case we have to fool the protocol enforcement. */
	old_dp = doing_privmsg;
	old_dn = doing_notice;
	old_dc = in_ctcp_flag;

	doing_privmsg = doing_notice = in_ctcp_flag = 0;
	send_ctcp(CTCP_PRIVMSG, user, CTCP_DCC, "ACCEPT %s %s %s",
		filename, port, offset);

	doing_privmsg = old_dp;
	doing_notice = old_dn;
	in_ctcp_flag = old_dc;

	/* Wait for them to open the connection */
}


/*
 * When we get the DCC ACCEPT
 * We start the connection
 */
static	void	dcc_getfile_resume_start (char *nick, char *filename, char *port, char *offset)
{
	DCC_list	*Client;
	char		*fullname;

	if (!get_int_var(MIRC_BROKEN_DCC_RESUME_VAR))
		return;

	if (x_debug & DEBUG_DCC_XMIT)
		yell("GOT CONFIRMATION FOR DCC RESUME from [%s] [%s]", nick, filename);

	if (!strcmp(filename, "file.ext"))
		filename = NULL;

	if (!(Client = dcc_searchlist(filename, nick, DCC_FILEREAD, 0, port, 0)))
		return;		/* Its fake. */

	Client->flags |= DCC_TWOCLIENTS;
	if (dcc_open(Client))
		return;

	if (!(fullname = expand_twiddle(Client->description)))
		malloc_strcpy(&fullname, Client->description);

	if (!(Client->file = open(fullname, O_WRONLY | O_APPEND, 0644)))
	{
		Client->flags |= DCC_DELETE;
		say("Unable to open %s: %s", fullname, errno ? strerror(errno) : "<No Error>");
	}

	new_free(&fullname);
}

#endif
