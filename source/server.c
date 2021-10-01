/*
 * server.c:  Things dealing with that wacky program we call ircd.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1993, 2014 EPIC Software Labs.
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
#include "functions.h"
#include "alias.h"
#include "parse.h"
#include "ssl.h"
#include "server.h"
#include "ircaux.h"
#include "lastlog.h"
#include "exec.h"
#include "window.h"
#include "output.h"
#include "names.h"
#include "hook.h"
#include "notify.h"
#include "alist.h"
#include "screen.h"
#include "status.h"
#include "vars.h"
#include "newio.h"
#include "reg.h"

/************************ SERVERLIST STUFF ***************************/

/* 
 * The "server_list" is a global list of all of your servers.
 * This is a dynamically resiable array, and it may contain gaps.
 * A server refnum is an index into this array!  Ie, server refnum 0
 * literally means server_list[0].  Server refnums may _never change_
 * during their lifetime.  If you delete a server, it leaves a gap
 * (server_list[x] == NULL).
 */
	Server **server_list = (Server **) 0;

/*
 * "number of servers" is the size of server_list.  This is used to 
 * guard buffer overrun against server_list.
 */
	int	number_of_servers = 0;

/*
 * The "primary_server" is an old ircII concept when multi-servering
 * was unusual.  Your "Primary Server" is the server of last resort
 * when the client wants to do something in the context of a server,
 * but does not have an appropriate one to use.  
 */
	int	primary_server = NOSERV;

/*
 * The "from_server" represents the server on whose behalf we are 
 * currently working.  If you think of the client as always doing 
 * one of three things:
 *   1. Processing a keystrong
 *   2. Processing something from the server
 *   3. Resting
 * Context is commutative.  If you do an /exec in an /on msg, then 
 * that /exec runs in the context of the /on msg.  If you do a /dcc
 * in context of a keypress, then that dcc runs in the context of the
 * server of the window in which you pressed that key.
 *
 * In all cases where the language is active, there is a "from_server"
 * that represents the server from which the action initiated.
 */
	int	from_server = NOSERV;

/* 
 * The "parsing_server_index" is set to the refnum of a server while
 * we are processing data from it.  It is NOSERV at all other times.
 * The value is _NOT_ commutative.  If you do an /exec in an /on msg, 
 * then it will not be set during the /exec (as contrasted to from_server
 * which will be set during the /exec)
 *
 * This value is only used for:
 *	$S	(if it is set, $S is this value; otherwise it is the
 *		 server of your current window.  $S does not follow
 *		 from_server.  Use $serverctl(FROM_SERVER)
 *	By /TIMER to decide whether this is a server timer or a 
 *		window timer.
 */
	int	parsing_server_index = NOSERV;

/* 
 * The "last_server" is the last server we processed.  That is, it is
 * the most recent value of "parsing_server_index".  It never gets 
 * unset.  But it can be reset at any time to any value.
 *
 * It is only used for $serverctl(LAST_SERVER)
 */
	int	last_server = NOSERV;


/************************************************************************/
static void	reset_server_altnames (int refnum, char *new_altnames);
	int	clear_serverinfo (ServerInfo *s);
	int	str_to_serverinfo (char *str, ServerInfo *s);
static	void	free_serverinfo (ServerInfo *s);
static	int	serverinfo_to_servref (ServerInfo *s);
static	int	serverinfo_to_newserv (ServerInfo *s);
static 	void 	remove_from_server_list (int i);
static	char *	shortname (const char *oname);
static void	set_server_uh_addr (int refnum);
static void	discard_dns_results (int refnum);

static	void	set_server_vhost (int servref, const char * param );
static	void	set_server_itsname (int servref, const char * param );
static	void	set_server_state (int servref, int param );
static	void 	got_my_userhost (int refnum, UserhostItem *item, const char *nick, const char *stuff);
static	void	make_005 (int refnum);
static	void	destroy_005 (int refnum);
static 	int	server_addrs_left (int refnum);
static const char *	get_server_type (int servref);
static	int	get_server_accept_cert (int refnum);
static	void	set_server_accept_cert (int refnum, int val);

/*
 * clear_serverinfo: Initialize/Reset a ServerInfo object
 *
 * Parameters:
 *	s	- A ServerInfo that is brand new or has already
 *		  been cleansed (see below)
 *
 * Return value:
 *	0	- This function always returns 0.
 *
 * Notes:
 *	If you are resetting an existing serverinfo (ie, one that has
 *	already been in use), you _MUST_ new_free() freestr and fulldesc.
 *	Otherwise, this will leak memory.
 */
int	clear_serverinfo (ServerInfo *s)
{
        s->refnum = NOSERV;
        s->host = NULL;
        s->port = 0;
        s->password = NULL;
        s->nick = NULL;
        s->group = NULL;
        s->server_type = NULL;
	s->proto_type = NULL;
	s->vhost = NULL;
	s->freestr = NULL;		/* XXX ? */
	s->fulldesc = NULL;		/* XXX ? */
	s->clean = 1;
	return 0;
}

/*
 * --
 * A old-style "server description" looks like:
 *
 *   host:port:passwd:nick:group:type
 *
 * 'host'     is a hostname, or an ipv4 p-addr ("192.168.0.1")
 *            or an ipv6 p-addr in square brackets ("[01::01]")
 * 'port'     is the port number the server is listening on (usually 6667)
 * 'passwd'   is the protocol passwd to log onto the server (usually blank)
 * 'nick'     is the nickname you want to use on this server
 * 'group'    is the server group this server belongs to
 * 'type'     is the server protocol type, either "IRC" or "IRC-SSL"
 * 'proto'    is the socket protocol type, either 'tcp4' or 'tcp6' or neither.
 * 'vhost'    is the virtual hostname to use for this connection.
 * 'ssl-strict' is whether you require ssl server to have a bona fide ssl cert
 *
 * --
 * A new-style server description is a colon separated list of values:
 *
 *   host=HOST	   port=PORTNUM   pass=PASSWORD 
 *   nick=NICK     group=GROUP    type=PROTOCOL_TYPE
 *   proto=SOCKETYPE  vhost=HOST  
 *   ssl-strict=NO
 *
 * for example:
 *	host=irc.server.com:group=efnet:type=IRC-SSL:ssl-strict=NO
 * 
 * The command type ("host", "port") can be abbreviated as long as it's
 * not ambiguous:
 *	h=irc.server.com:po=6666:g=efnet:t=IRC-SSL
 *
 * --
 * You can mix and match (sort of) the types, but if the meaning of any
 * field that doesn't have a descriptor follows whatever was in the previous
 * field.  
 *	 irc.server.com:group=efnet:IRC-SSL
 */

enum serverinfo_fields { HOST, PORT, PASS, NICK, GROUP, TYPE, PROTO, VHOST, SSL_STRICT, LASTFIELD };

/*
 * str_to_serverinfo:  Create or Modify a temporary ServerInfo based on string.
 *
 * Arguments:
 *	str	A server description.  This string WILL BE POINTED TO 
 *		by the ServerInfo until you call preserve_serverinfo (below).
 *		This string WILL BE MODIFIED so it can't be a const.
 *	s	A ServerInfo object.  's' can either be a temporary ServerInfo
 *		that you have previously passed to 'clear_serverinfo' or a 
 *		permanent ServerInfo you have passed to 'preserve_serverinfo'.
 *		However, this function changes 's' into a temporary ServerInfo
 *		so you will need to call 'preserve_serverinfo' again.
 * Notes:
 *	A "temporary serverinfo" points to the 'str' you pass here, so you
 *	cannot use the temporary serverinfo after the source 'str' goes out
 *	of scope!  You MUST call preserve_serverinfo() if you want to keep
 *	the serverinfo!
 * Return value:	
 *	This function returns 0
 */
int	str_to_serverinfo (char *str, ServerInfo *s)
{
	char *	descstr;
	ssize_t span;
	char *	after;
	enum serverinfo_fields	fieldnum;

	if (!str)
		panic(1, "str_to_serverinfo: str == NULL");

	if (!s->clean)
		panic(1, "str_to_serverinfo: serverinfo is not clean!");

	/*
	 * As a shortcut, we allow the string to be a number,
	 * which refers to an existing server.
	 */
	descstr = str;
	if (str && is_number(str))
	{
		int	i;

		i = strtol(str, &after, 10);
		if ((!after || !*after) && get_server(i))
		{
			s->refnum = i;
			return 0;
		}
	}

	/*
	 * Otherwise, we convert it by breaking down each component in turn.
	 */
	for (fieldnum = HOST; fieldnum <= LASTFIELD; fieldnum++)
	{
		char *	first_colon;
		char *	first_equals;
		int	ignore_field = 0;

		first_equals = strchr(descstr, '=');
		if ((span = findchar_quoted(descstr, ':')) >= 0)
			first_colon = descstr + span;
		else
			first_colon = NULL;

		/*
		 * This field has a field-descryption-type.  Reset the field
		 * counter to the appropriate place.
		 */
		if ( (first_colon && first_equals && first_colon > first_equals)
		  || (first_colon == NULL && first_equals) )
		{
			char *p;

			p = first_equals;
			*p++ = 0;

			if (!my_strnicmp(descstr, "HOST", 1))
				fieldnum = HOST;
			else if (!my_strnicmp(descstr, "PORT", 2))
				fieldnum = PORT;
			else if (!my_strnicmp(descstr, "PASS", 2))
				fieldnum = PASS;
			else if (!my_strnicmp(descstr, "NICK", 1))
				fieldnum = NICK;
			else if (!my_strnicmp(descstr, "GROUP", 1))
				fieldnum = GROUP;
			else if (!my_strnicmp(descstr, "TYPE", 1))
				fieldnum = TYPE;
			else if (!my_strnicmp(descstr, "PROTO", 2))
				fieldnum = PROTO;
			else if (!my_strnicmp(descstr, "VHOST", 1))
				fieldnum = VHOST;
			else if (!my_strnicmp(descstr, "SSL_STRICT", 5))
				fieldnum = SSL_STRICT;
			else
			{
				say("Server desc field type [%s] not recognized.", 
					descstr);
				ignore_field = 1;
			}

			/*
			 * Now that we've set the correct field,
			 * set the value to the place after the =,
			 * and then treat it as it was not adorned.
			 */
			descstr = p;
		}

	    if (!ignore_field)
	    {
		/* Set the appropriate field */
		if (fieldnum == HOST)
		{
		  /* 
		   * A "clean" serverinfo has just been through 
		   * clear_serverinfo().  If it is not "clean" then that means
		   * it belongs to a server.  We don't want to modify the 
		   * host field of a server's serverinfo!
		   * -- This allows us to do things like:
		   * 		/server 0:group=efnet 
		   *    or      /server efnet:type=irc-ssl
		   */
		  if (s->clean)
		  {
		    if (*descstr == '[')
		    {
			s->host = descstr + 1;
			if ((span = MatchingBracket(descstr + 1, '[',']')) >= 0)
			{
				descstr = descstr + 1 + span;
				*descstr++ = 0;
			}
			else
				break;
		    }
		    else
			s->host = descstr;
		  }
		}
		else if (fieldnum == PORT)
			s->port = atol(descstr);
		else if (fieldnum == PASS)
			s->password = descstr;
		else if (fieldnum == NICK)
			s->nick = descstr;
		else if (fieldnum == GROUP)
			s->group = descstr;
		else if (fieldnum == TYPE)
			s->server_type = descstr;
		else if (fieldnum == PROTO)
			s->proto_type = descstr;
		else if (fieldnum == VHOST)
		{
		    if (*descstr == '[')
		    {
			s->vhost = descstr + 1;
			if ((span = MatchingBracket(descstr + 1, '[',']')) >= 0)
			{
				descstr = descstr + 1 + span;
				*descstr++ = 0;
			}
			else
				break;
		    }
		    else
			s->vhost = descstr;
		}

		/*
		 * We go one past "type" because we want to allow
		 *      /server -add type=irc-ssl:host=irc.foo.com
		 * and if we didn't do this, the above loop would
		 * terminate after the "type" and not read the "host".
		 */
		else if (fieldnum == LASTFIELD)
			break;
	    }

		/* I don't see how this is possible, but clang says it is */
		if (descstr == NULL)
			break;

		/* And advance to the next field */
		if ((span = findchar_quoted(descstr, ':')) >= 0)
		{
			descstr = descstr + span;
			*descstr++ = 0;
		}
		else
			break;
	}

	return 0;
}

/*
 * preserve_serverinfo - Convert a temporary ServerInfo into a permanent one.
 *
 * Arguments:
 *	si	Normally when you call str_to_serverinfo above, you get a 'si'
 *		that can only be used for as long as the 'str' you created it
 *		from.  If you pass that 'si' to this function, it unties it 
 *		from the original string and you can then use 'si' permanently.
 *		You MUST later call free_serverinfo() on 'si' to free it.
 */
static	int	preserve_serverinfo (ServerInfo *si)
{
	char *	resultstr = NULL;
	size_t	clue = 0;

	if (si->host && strchr(si->host, ':'))
	   malloc_strcat_c(&resultstr, "[", &clue);
	malloc_strcat_c(&resultstr, si->host, &clue);
	if (si->host && strchr(si->host, ':'))
	   malloc_strcat_c(&resultstr, "]", &clue);
	malloc_strcat_c(&resultstr, ":", &clue);
	malloc_strcat2_c(&resultstr, ltoa(si->port), ":", &clue);
	malloc_strcat2_c(&resultstr, si->password, ":", &clue);
	malloc_strcat2_c(&resultstr, si->nick, ":", &clue);
	malloc_strcat2_c(&resultstr, si->group, ":", &clue);
	malloc_strcat2_c(&resultstr, si->server_type, ":", &clue);
	malloc_strcat2_c(&resultstr, si->proto_type, ":", &clue);
	if (si->vhost && strchr(si->vhost, ':'))
	{
	   malloc_strcat_c(&resultstr, "[", &clue);
	   malloc_strcat_c(&resultstr, si->vhost, &clue);
	   malloc_strcat2_c(&resultstr, "]", ":", &clue);
	}
        else
	   malloc_strcat2_c(&resultstr, si->vhost, ":", &clue);

	new_free(&si->freestr);
	new_free(&si->fulldesc);
	clear_serverinfo(si);
	malloc_strcpy(&si->fulldesc, resultstr);
	si->freestr = resultstr;
	str_to_serverinfo(si->freestr, si);
	return 0;
}

/*
 * free_serverinfo - Destroy a permanent ServerInfo when you're done with it.
 *
 * Arguments:
 *	si	A permanent Serverinfo -- an 'si' that was previously 
 *		passed to preserve_serverinfo().
 */
static	void	free_serverinfo (ServerInfo *si)
{
	new_free(&si->freestr);
	new_free(&si->fulldesc);
	clear_serverinfo(si);
}

/*
 * update_serverinfo - Do a partial update of 'old_si' using 'new_si'
 *
 * Arguments:
 *	old_si	- A ServerInfo that should be updated.  I don't think it
 *		  matters if it is a temp or permanent ServerInfo. 
 *		  Old_si _will be updated_.
 *		  Old_si _will be turned into a permanent ServerInfo_.
 *	new_si	- A ServerInfo that contains fields that should be updated
 *		  in old_si.  It does not need to be a proper ServerInfo.
 *		  New_si will not be changed.
 *
 * Because the 'host' field is the PK for a ServerInfo, it will not be
 * changed, even if it is present in 'new_si'.
 * "New_si" BELONGS TO YOU -- YOU MUST CLEAN UP AFTER IT.
 * "Old_si" BECOMES A PERMANENT ServerInfo AND BELONGS TO YOU.
 */
static	void	update_serverinfo (ServerInfo *old_si, ServerInfo *new_si)
{
	/* You should never update 'host' because it contains the
	 * lookup key, and cannot ever be mutable. ever. */
#if 0
	if (new_si->host)
		old_si->host = new_si->host;
#endif
	if (new_si->port)
		old_si->port = new_si->port;
	if (new_si->password)
		old_si->password = new_si->password;
	if (new_si->nick)
		old_si->nick = new_si->nick;
	if (new_si->group)
		old_si->group = new_si->group;
	if (new_si->server_type)
		old_si->server_type = new_si->server_type;
	if (new_si->proto_type)
		old_si->proto_type = new_si->proto_type;
	if (new_si->vhost)
		old_si->vhost = new_si->vhost;

	preserve_serverinfo(old_si);
	return;
}


/*
 * serverinfo_matches_servref - See if a serverinfo could describe a server
 *
 * Arguments:
 *	si	A temporary ServerInfo previously passed to str_to_serverinfo.
 *	servref	A server that might be correctly described by 'si'.
 *
 * Return value:
 *	The server refnum of the server that matches 'si',
 *	or NOSERV if there is no server that matches.
 */
int	serverinfo_matches_servref (ServerInfo *si, int servref)
{
	Server *s;
	int	j;

	if (!si->host && si->refnum == NOSERV)
		return 0;

	/* If this server was deleted, ignore it. */
	if (!(s = get_server(servref)))
		return 0;

	/* If the server doesn't have a hostname, it's bogus. */
	if (!s->info || !s->info->host)
		return 0;

	/*
	 * The servref can match...
	 */
	if (si->refnum != NOSERV && si->refnum == servref)
		return 1;

	/*
	 * The "hostname" you request can actually be a refnum,
	 * and if it is this server's refnum, then we're done.
	 */
	if (si->host && is_number(si->host) && atol(si->host) == servref)
		return 1;

	/*
	 * If "hostname" is not a refnum, then we see if the
	 * vitals match this server (or not)
	 */

	/*
	 * IMPORTANT -- every server refnum is uniquely defined as
	 * a (hostname, port, password) tuple.  The only place
	 * this referential integrity is enforced is here.
	 * So if you're changing this code, don't screw that up!
	 * Unless you're here to screw it up, of course.
	 */

	/* If you requested a specific port, IT MUST MATCH.  */
	/* If you don't specify a port, then any port is fine */
	if (si->port != 0 && si->port != s->info->port)
		return 0;

	/* If you specified a password, IT MUST MATCH */
	/* If you don't specify a password, then any pass is fine */
	if (si->password && !s->info->password)
		return 0;
	if (si->password && !wild_match(si->password, s->info->password))
		return 0;

	/*
	 * At this point, we're looking to match your provided
	 * host against something reasonable.
	 *  1. The "ourname"  (the internet hostname)
	 *  2. The "itsname"  (the server's logical hostname on
	 *		       irc, which may or may not have anything
	 *		       to do with its internet hostname)
	 *  3. The Server Group
	 *  4. Any "altname"
	 *
	 * IMPORTANT! All of the above do WILDCARD MATCHING, so 
	 * that means hostname like "*.undernet.org" will match
	 * on an undernet server, even if you don't know the 
	 * exact name!
	 *
	 * IMPORTANT -- Please remember -- the lowest numbered 
	 * refnum that matches ANY of the four will be our winner!
	 * That means if server refnum 0 has an altname of 
	 * "booya" and server refnum 1 has a group of "booya",
	 * then server 0 wins!
	 */

	if (s->info->host && wild_match(si->host, s->info->host))
		return 1;

	if (s->itsname && wild_match(si->host, s->itsname))
		return 1;

	if (s->info->group && wild_match(si->host, s->info->group))
		return 1;

	if (get_server_005(servref, "NETWORK") && 
			wild_match(si->host, 
				   get_server_005(servref, "NETWORK")))
		return 1;

	for (j = 0; j < s->altnames->numitems; j++)
	{
		if (!s->altnames->list[j].name)
			continue;

		if (wild_match(si->host, s->altnames->list[j].name))
			return 1;
	}

	return 0;
}


/*
 * serverinfo_to_servref - Convert a temporary serverinfo into a server refnum
 *			   Returns the FIRST server that seems a match.
 *
 * Arguments:
 *	si	A temporary ServerInfo previously passed to str_to_serverinfo.
 * Return value:
 *	The first server refnum that matches 'si',
 *	or NOSERV if there is no server that matches.
 * Notes:
 *	If this function returns NOSERV, you can call serverinfo_to_newserv()
 *	to add the ServerInfo to the server list.
 */
static	int	serverinfo_to_servref (ServerInfo *si)
{
	int	i, j, opened;
	Server *s;

	if (si->refnum != NOSERV && get_server(si->refnum))
		return si->refnum;

	if (!si->host)
		return NOSERV;

	for (opened = 1; opened >= 0; opened--)
	{
	    for (i = 0; i < number_of_servers; i++)
	    {
		if (is_server_open(i) != opened)
			continue;

		if (serverinfo_matches_servref(si, i))
			return i;
	    }
	}

	return NOSERV;
}

static	void	update_refnum_serverinfo (int refnum, ServerInfo *new_si)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	update_serverinfo(s->info, new_si);

	/* If the user asked for a specific nick, use it as the default */
	if (!empty(new_si->nick))
		malloc_strcpy(&s->d_nickname, new_si->nick);
}

static	int	update_server_from_raw_desc (int refnum, char *str)
{
	ServerInfo si;

	if (!get_server(refnum))
		return NOSERV;

	clear_serverinfo(&si);
	str_to_serverinfo(str, &si);
	update_refnum_serverinfo(refnum, &si);
	return refnum;
}

/*
 * serverinfo_to_newserv - Add a server to the server list
 *
 * Arguments:
 *	si	A ServerInfo that should be added to the global server list.
 *		The new server makes a copy of this ServerInfo, so you are
 *		still responsible for cleaning up 'si' after calling.
 * Notes:
 *	You are responsible for ensuring that 'si' does not conflict with an
 *	already existing server.  You should not call this function unless
 *	serverinfo_to_servref(si) == NOSERV.  Adding duplicate servers to the 
 *	server list results in undefined behavior.
 */
static	int	serverinfo_to_newserv (ServerInfo *si)
{
	int	i;
	Server *s;

	for (i = 0; i < number_of_servers; i++)
		if (server_list[i] == NULL)
			break;

	if (i == number_of_servers)
	{
		number_of_servers++;
		RESIZE(server_list, Server *, number_of_servers);
	}

	s = server_list[i] = new_malloc(sizeof(Server));
	s->info = (ServerInfo *)new_malloc(sizeof(ServerInfo));
	clear_serverinfo(s->info);
	*(s->info) = *si;
	if (s->info->port == 0)
		s->info->port = irc_port;
	preserve_serverinfo(s->info);

	s->itsname = (char *) 0;
	s->altnames = new_bucket();
	add_to_bucket(s->altnames, shortname(s->info->host), NULL);
	s->away = (char *) 0;
	s->version_string = (char *) 0;
	s->des = -1;
	s->state = SERVER_CREATED;
	s->nickname = (char *) 0;
	s->s_nickname = (char *) 0;
	s->d_nickname = (char *) 0;
	s->unique_id = (char *) 0;
	s->userhost = (char *) 0;
	s->line_length = IRCD_BUFFER_SIZE;
	s->max_cached_chan_size = -1;
	s->who_queue = NULL;
	s->ison_len = 500;
	s->ison_max = 1;
	s->ison_queue = NULL;
	s->ison_wait = NULL;
	s->userhost_max = 1;
	s->userhost_queue = NULL;
	s->userhost_wait = NULL;
	s->uh_addr_set = 0;
	memset(&s->uh_addr, 0, sizeof(s->uh_addr));
	memset(&s->local_sockname, 0, sizeof(s->local_sockname));
	memset(&s->remote_sockname, 0, sizeof(s->remote_sockname));
	s->remote_paddr = NULL;
	s->redirect = NULL;
	s->cookie = NULL;
	s->quit_message = NULL;
	s->umode[0] = 0;
	s->addrs = NULL;
	s->next_addr = NULL;
	s->autoclose = 1;
	s->default_realname = NULL;
	s->realname = NULL;

	s->doing_privmsg = 0;
	s->doing_notice = 0;
	s->doing_ctcp = 0;
	s->waiting_in = 0;
	s->waiting_out = 0;
	s->start_wait_list = NULL;
	s->end_wait_list = NULL;

	s->invite_channel = NULL;
	s->last_notify_nick = NULL;
	s->joined_nick = NULL;
	s->public_nick = NULL;
	s->recv_nick = NULL;
	s->sent_nick = NULL;
	s->sent_body = NULL;

	s->stricmp_table = 1;		/* By default, use rfc1459 */
	s->funny_match = NULL;

	if (!empty(s->info->nick))
		malloc_strcpy(&s->d_nickname, s->info->nick);
	else
		malloc_strcpy(&s->d_nickname, nickname);

	make_notify_list(i);
	make_005(i);

	set_server_state(i, SERVER_RECONNECT);
	return i;
}

/***************************************************************************/
int	str_to_servref (const char *desc)
{
	char *	ptr;
	ServerInfo si;
	int	retval;

	ptr = LOCAL_COPY(desc);
	clear_serverinfo(&si);
	if (str_to_serverinfo(ptr, &si))
		return NOSERV;

	retval = serverinfo_to_servref(&si);
	return retval;
}

int	str_to_servref_with_update (const char *desc)
{
	char *	ptr;
	ServerInfo si;
	int	retval;

	ptr = LOCAL_COPY(desc);
	clear_serverinfo(&si);
	if (str_to_serverinfo(ptr, &si))
		return NOSERV;

	if ((retval = serverinfo_to_servref(&si)) != NOSERV)
		update_refnum_serverinfo(retval, &si);

	return retval;
}

int	str_to_newserv (const char *desc)
{
	char *	ptr;
	ServerInfo si;
	int	retval;

	ptr = LOCAL_COPY(desc);
	clear_serverinfo(&si);
	if (str_to_serverinfo(ptr, &si))
		return NOSERV;

	retval = serverinfo_to_newserv(&si);
	return retval;
}

void	destroy_server_list (void)
{
	int	i;

	for (i = 0; i < number_of_servers; i++)
		remove_from_server_list(i);
	new_free((char **)&server_list);
}

static 	void 	remove_from_server_list (int i)
{
	Server  *s;
	int	count, j;

	if (!(s = get_server(i)))
		return;

	/* Count up how many servers are left. */
	for (count = 0, j = 0; j < number_of_servers; j++)
		if (get_server(j))
			count++;

	if (count == 1 && !dead)
	{
		say("You can't delete the last server!");
		return;
	}

	say("Deleting server [%d]", i);
	set_server_state(i, SERVER_DELETED);

	clean_server_queues(i);
	new_free(&s->itsname);
	new_free(&s->away);
	new_free(&s->version_string);
	new_free(&s->nickname);
	new_free(&s->s_nickname);
	new_free(&s->d_nickname);
	new_free(&s->unique_id);
	new_free(&s->userhost);
	new_free(&s->cookie);
	new_free(&s->quit_message);
	new_free(&s->invite_channel);
	new_free(&s->last_notify_nick);
	new_free(&s->joined_nick);
	new_free(&s->public_nick);
	new_free(&s->recv_nick);
	new_free(&s->sent_nick);
	new_free(&s->sent_body);
	new_free(&s->funny_match);
	new_free(&s->default_realname);
	new_free(&s->remote_paddr);
	destroy_notify_list(i);
	destroy_005(i);
	reset_server_altnames(i, NULL);
	free_bucket(&s->altnames);
	free_serverinfo(s->info);
	new_free(&s->info);

	new_free(&server_list[i]);
	s = NULL;
}


/*****************************************************************************/
/*
 * add_servers: Add a space-separated list of server descs to the server list.
 *	If the server description does not set a port, use the default port.
 *	If the server description does not set a group, use the provided group.
 *  This function modifies "servers".
 */
void	add_servers (char *servers, const char *group)
{
	char	*host;
	ServerInfo si;
	int	refnum;

	if (!servers)
		return;

	while ((host = next_arg(servers, &servers)))
	{
		clear_serverinfo(&si);
		str_to_serverinfo(host, &si);
		if (group && si.group == NULL)
			si.group = group;

		refnum = serverinfo_to_servref(&si);
		if (refnum == NOSERV)
			serverinfo_to_newserv(&si);
		else
			update_refnum_serverinfo(refnum, &si);
	}
}

/*
 * read_server_file: Add servers from some file to the server list
 */
static int 	read_server_file (const char *file_path)
{
	Filename expanded;
	FILE 	*fp;
	char	buffer[BIG_BUFFER_SIZE + 1];
	char	*defaultgroup = NULL;

	if (normalize_filename(file_path, expanded))
		return -1;

	if (!(fp = fopen(expanded, "r")))
		return -1;

	while (fgets(buffer, BIG_BUFFER_SIZE, fp))
	{
		chop(buffer, 1);
		if (*buffer == '#')
			continue;
		else if (*buffer == '[')
		{
		    char *p;
		    if ((p = strrchr(buffer, ']')))
			*p++ = 0;
		    malloc_strcpy(&defaultgroup, buffer + 1);
		}
		else if (*buffer == 0)
			continue;
		else
			add_servers(buffer, defaultgroup);
	}

	fclose(fp);
	new_free(&defaultgroup);
	return 0;
}

/*
 * read_server_file: Add servers from "SERVERS FILE" to the server list.
 */
int 	read_default_server_file (void)
{
	char	file_path[PATH_MAX + 1];

	if (getenv("IRC_SERVERS_FILE"))
		strlcpy(file_path, getenv("IRC_SERVERS_FILE"), sizeof file_path);
	else
	{
#ifdef SERVERS_FILE
		*file_path = 0;
		if (SERVERS_FILE[0] != '/' && SERVERS_FILE[0] != '~')
			strlcpy(file_path, irc_lib, sizeof file_path);
		strlcat(file_path, SERVERS_FILE, sizeof file_path);
#else
		return -1;
#endif
	}

	return read_server_file(file_path);
}


/* display_server_list: just guess what this does */
void 	display_server_list (void)
{
	Server *s;
	int	i;

	if (!server_list)
	{
		say("The server list is empty");
		return;
	}

	if (from_server != NOSERV && (s = get_server(from_server)))
		say("Current server: %s %d", s->info->host, s->info->port);
	else
		say("Current server: <None>");

	if (primary_server != NOSERV && (s = get_server(primary_server)))
		say("Primary server: %s %d", s->info->host, s->info->port);
	else
		say("Primary server: <None>");

	say("Server list:");
	for (i = 0; i < number_of_servers; i++)
	{
		if (!(s = get_server(i)))
			continue;

		/*
		 * XXX Ugh.  I should build this up bit by bit.
		 */
		if (!s->nickname)
			say("\t%d) %s %d [%s] %s [%s] (vhost: %s)",
				i, s->info->host, s->info->port, 
				get_server_group(i), get_server_type(i),
				get_server_state_str(i),
				get_server_vhost(i));
		else if (is_server_open(i))
			say("\t%d) %s %d (%s) [%s] %s [%s] (vhost: %s)",
				i, s->info->host, s->info->port,
				s->nickname, get_server_group(i),
				get_server_type(i),
				get_server_state_str(i),
				get_server_vhost(i));
		else
			say("\t%d) %s %d (was %s) [%s] %s [%s] (vhost: %s)",
				i, s->info->host, 
				s->info->port, s->nickname, get_server_group(i),
				get_server_type(i),
				get_server_state_str(i),
				get_server_vhost(i));
	}
}

char *	create_server_list (void)
{
	Server	*s;
	int	i;
	char	*buffer = NULL;
	size_t	bufclue = 0;

	for (i = 0; i < number_of_servers; i++)
	{
		if (!(s = get_server(i)))
			continue;

		if (s->des != -1)
		    malloc_strcat_wordlist_c(&buffer, space, 
					get_server_itsname(i), &bufclue);
	}

	RETURN_MSTR(buffer);
}

/* server_list_size: returns the number of servers in the server list */
int 	server_list_size (void)
{
	return number_of_servers;
}

/* 
 * Look for another server in the same group as 'oldserv'
 * Direction is 1 to go forward, -1 to go backward. 
 * Other values will lose.
 */
static int	next_server_in_group (int oldserv, int direction)
{
	int	newserv;
	int	counter;

	for (counter = 1; counter <= number_of_servers; counter++)
	{
		/* Starting with 'oldserv', move in the given direction */
		newserv = oldserv + (counter * direction);

		/* Make sure the new server is always a valid servref */
		while (newserv < 0)
			newserv += number_of_servers;

		/* Make sure the new server is valid. */
		if (newserv >= number_of_servers)
			newserv %= number_of_servers;

		/* If there is no server at this refnum, skip it. */
		if (!get_server(newserv))
			continue;

		if (!my_stricmp(get_server_group(oldserv),
			        get_server_group(newserv)))
			return newserv;
	}
	return oldserv;		/* Couldn't find one. */
}

/*****************************************************************************/
/*****************************************************************************/
/*****************************************************************************/
static	void	server_is_unregistered (int refnum);
static void 	reset_nickname (int refnum);

	int	connected_to_server = 0;	/* How many active server 
						   connections are open */
static	char    lame_wait_nick[] = "***LW***";
static	char    wait_nick[] = "***W***";

/*
 * Each server goes through various stages/states where it builds up its capabilities
 * until it is fully enabled as a conduit for IRC commands by the user.
 * Each state *generally* automatically transitions to the next state when it completes.
 * 
 * There are three quiescent states (ie, states that do not automatically advance)
 *	RECONNECT	A server in RECONNECT will stay there until a window is associated with the server
 *			Once a window is associated with a server, it advances to DNS
 *	ACTIVE		A server in ACTIVE will stay there until the protocol session ends, because of
 *			1. The user requests disconnection,
 *			2. The server tells us we're being rejected,
 *			3. An EOF occurs on the underying network socket
 *	CLOSED		A server in CLOSED will stay there until the user moves it back to RECONNECT
 */
const char *server_states[13] = {
	"CREATED",		/* A server in CREATED is being built and can't be used yet. */
	"RECONNECT",		/* A server in RECONNECT can be connected to (but isn't).  Quiescent until a window points at it */
	"DNS",			/* A server in DNS is fetching IP addresses (but doesn't have them). */
	"CONNECTING",		/* A server in CONNECTING is doing a non-blocking connect. */
	"SSL_CONNECTING",	/* A server in SSL_CONNECTING is establishing SSL on top of the socket */
	"REGISTERING",		/* A server in REGISTERING is establishing an RFC1459 session with irc server */
	"SYNCING",		/* A server in SYNCING has a RFC1459 protocol layer and is doing things like setting AWAY */
	"ACTIVE",		/* A server in ACTIVE is quiescent and can be used by the user */
	"EOF",			/* A server in EOF has received an EOF on the socket (needs to be server_close()d */
	"ERROR",		/* A server in ERROR has received an error incompatable with the connection and needs to be reset */
	"CLOSING",		/* A server in CLOSING has already failed, and we are just cleaning up state */
	"CLOSED",		/* A server in CLOSED is "cleaned" and quiescent - you need to reset to RECONNECT */
	"DELETED"		/* A server in DELETED is being torn down and can't be used any more */
};



/*************************** SERVER STUFF *************************/
/*
 * server: the /SERVER command. Read the SERVER help page about 
 *
 * /SERVER
 *	Show the server list.
 * /SERVER -DELETE <refnum|desc>
 *	Remove server <refnum> (or <desc>) from server list.
 *	Fails if you do not give it a refnum or desc.
 *	Fails if server does not exist.
 * 	Fails if server is open.
 * /SERVER -ADD <desc>
 *	Add server <desc> to server list.
 *	Fails if you do not give it a <desc>
 * /SERVER +<refnum|desc>
 *	Allow server to reconnect if windows are pointed to it.
 *	Note: server reconnection is asynchronous
 * /SERVER -<refnum|desc>
 *	Unconditionally close a server connection
 *	Note: server disconnection is synchronous!
 * /SERVER +
 *	Switch windows from current server to next server in same group
 * /SERVER -
 *	Switch windows from current server to previous server in same group
 * /SERVER <refnum|desc>
 *	Switch windows from current server to another server.
 */
BUILT_IN_COMMAND(servercmd)
{
	char	*server = NULL;
	int	i;
	int	olds, news;
	char *	shadow;
	size_t	slen;

	/*
	 * This is a new trick I'm testing out to see if it works
	 * better than the hack I was using.
	 */
	shadow = LOCAL_COPY(args);
	if ((server = next_arg(shadow, &shadow)) == NULL)
	{
		display_server_list();
		return;
	}
	slen = strlen(server);

	/*
	 * /SERVER -DELETE <refnum>             Delete a server from list
	 */
	if (slen > 1 && !my_strnicmp(server, "-DELETE", slen))
	{
		next_arg(args, &args);		/* Skip -DELETE */
		if (!(server = new_next_arg(args, &args)))
		{
			say("Need argument to /SERVER -DELETE");
			return;
		}

		if ((i = str_to_servref(server)) == NOSERV)
		{
			say("No such server [%s] in list", server);
			return;
		}

		if (is_server_open(i))
		{
			say("Can not delete server %d because it is open", i);
			return;
		}

		remove_from_server_list(i);
		return;
	}

	/*
	 * SERVER -ADD <host>                   Add a server to list
	 */
	if (slen > 1 && !my_strnicmp(server, "-ADD", slen))
	{
		next_arg(args, &args);		/* Skip -ADD */
		if (!(server = new_next_arg(args, &args)))
		{
			say("Need argument to /SERVER -ADD");
			return;
		}

		if ((from_server = str_to_servref_with_update(server)) != NOSERV)
		{
			say("Server [%d] updated with [%s]",
					from_server, server);
		}
		else
		{
			from_server = str_to_newserv(server);
			say("Server [%s] added as server %d", server, from_server);
		}
		return;
	}

	/*
	 * SERVER -UPDATE <refnum> <desc>		Change a server
	 */
	if (slen > 1 && !my_strnicmp(server, "-UPDATE", slen))
	{
		int	servref;

		next_arg(args, &args);		/* Skip -UPDATE */
		if (!(server = new_next_arg(args, &args)))
		{
			say("Usage: /SERVER -UPDATE refnum descr");
			return;
		}
		if (!is_number(server))
		{
			say("Usage: /SERVER -UPDATE refnum descr");
			return;
		}

		servref = atol(server);
		if (!(server = new_next_arg(args, &args)))
		{
			say("Usage: /SERVER -UPDATE refnum descr");
			return;
		}

		if (is_server_open(servref))
		{
			say("Can't update a server that is open");
			return;
		}

		update_server_from_raw_desc(servref, server);
		say("Server %d description updated", servref);
		return;
	}


	/*
	 * /server +host.com                    Allow server to reconnect
	 */
	if (slen > 1 && *server == '+')
	{
		args++;			/* Skip the + */
		server = new_next_arg(args, &args);

		if ((i = str_to_servref(server)) == NOSERV)
		{
			say("No such server [%s] in list", server);
			return;
		}
		if (get_server(i) && get_server_state(i) == SERVER_CLOSED)
			set_server_state(i, SERVER_RECONNECT);
		return;
	}

	/*
	 * /server -host.com                    Force server to disconnect
	 */
	if (slen > 1 && *server == '-')
	{
		args++;			/* Skip the - */
		disconnectcmd("DISCONNECT", args, NULL);
		return;
	}


	/* * * * The rest of these actually move windows around * * * */
	olds = from_server;

	if (*server == '+')
		news = next_server_in_group(olds, 1);
	else if (*server == '-')
		news = next_server_in_group(olds, -1);
	else
	{
		if ((news = str_to_servref_with_update(server)) == NOSERV)
		{
		    if ((news = str_to_newserv(server)) == NOSERV)
		    {
			say("I can't parse server description [%s]", server);
			return;
		    }
		}
	}

	/* Always unconditionally allow new server to auto-reconnect */
	if (!is_server_registered(news))
	{
		say("Reconnecting to server %d", news);
		set_server_state(news, SERVER_RECONNECT);
	}

	/* If the user is not actually changing server, just reassure them */
	if (olds == news)
	{
		say("This window is associated with server %d (%s:%d)",
			olds, get_server_name(olds), get_server_port(olds));
	}
	else
	{
		/* Do the switch! */
		set_server_quit_message(olds, "Changing servers");
		change_window_server(olds, news);
	}
}


/* SERVER INPUT STUFF */
/*
 * do_server: A callback suitable for use with new_open() to handle servers
 *
 * When new_open() determines that new data is ready for a file descriptor,
 * it calls the callback function registered with that file descriptor.
 * This is the function that should be used for every server fd.
 *
 * It will:
 *  1) Determine what state the file descriptor is in 
 *  2) De-queue the next chunk of data from the server (which is state
 *     dependent -- ie, in DNS, it's a serialized IP address.  In CONNECTED, 
 *     it's a line of text terminated by \r\n).
 *  3) Take the appropriate action based on the state and data received.
 *
 * It might have been reasonable to have several different callbacks -- one for
 * each server state, and it might have been reasonable to implement each 
 * server state as a separate function that gets called from here; but I chose 
 * to implement it as one monolithic function.  There is nothing special about 
 * doing it one way or the other.
 */
void	do_server (int fd)
{
	Server *s;
	char	buffer[IO_BUFFER_SIZE + 1];
	int	des,
		i, l;
	char *extra = NULL;
	int	found = 0;

	for (i = 0; i < number_of_servers; i++)
	{
		ssize_t	junk;
		char 	*bufptr = buffer;
		int	retval = 0;

		if (!(s = get_server(i)))
			continue;

		if ((des = s->des) < 0)
			continue;		/* Nothing to see here, */

		if (des != fd)
			continue;		/* Move along. */

		found = 1;			/* We found it */

		from_server = i;
		l = message_from(NULL, LEVEL_OTHER);

		/* - - - - */
		/*
		 * Is the dns lookup finished?
		 * Handle DNS lookup responses from the dns helper.
		 * Remember that when we start up a server connection,
		 * s->des points to a socket connected to the dns helper
		 * which feeds us Getaddrinfo() responses.  We then use
		 * those reponses, to establish nonblocking connect()s
		 * [the call to connect_to_server() below], which replaces
		 * s->des with a new socket connecting to the server.
		 */
		if (s->state == SERVER_DNS)
		{
			int cnt = 0;
			ssize_t len;

			/*
			 * This is our handler for the first bit of data from 
			 * the dns helper, which is a length value.  This 
			 * length value tells us how much data we should expect
			 * to receive from the dns helper.  We use this value 
			 * to malloc() off some space and then read into that 
			 * buffer.
			 */
			if (s->addrs == NULL)
			{
				len = dgets(s->des, (char *)&s->addr_len, 
					sizeof(s->addr_len), -2);
				if (len < (ssize_t)sizeof(s->addr_len))
				{
					if (len < 0)
						yell("DNS lookup failed, possibly because of a "
							"bug in async_getaddrinfo!");
					else if (len == 0)
						yell("Got part of the dns response, waiting "
							"for the rest, stand by...");
					else
						yell("Got %ld, expected %ld bytes.  HELP!", 
							(long)len, (long)sizeof(s->addr_len));
					pop_message_from(l);
					continue;		/* Not ready yet */
				}

				if (s->addr_len < 0)
				{
					if (EAI_AGAIN > 0)
						s->addr_len = labs(s->addr_len);
					yell("Getaddrinfo(%s) for server %d failed: %s",
						s->info->host, i, gai_strerror(s->addr_len));
					s->des = new_close(s->des);
					set_server_state(i, SERVER_ERROR);
					set_server_state(i, SERVER_CLOSED);
				}
				else if (s->addr_len == 0) 
				{
					yell("Getaddrinfo(%s) for server (%d) did not "
						"resolve.", s->info->host, i);
					s->des = new_close(s->des);
					set_server_state(i, SERVER_ERROR);
					set_server_state(i, SERVER_CLOSED);
				}
				else
				{
					s->addrs = (AI *)new_malloc(s->addr_len + 1);
					s->addr_offset = 0;
				}
			}

			/*
			 * If we've already received the "reponse length" value
			 * [handled above] then s->addrs is not NULL, and we 
			 * need to write the nonblocking dns responses into the
			 * buffer.  Once we have all of the reponse, we can 
			 * "unmarshall" the response (converting a (char *) 
			 * buffer into a linked list of (struct addrinfo *)'s, 
			 * which we can then use to connect to the server 
			 * [via connect_to_server()].
			 */
			else
			{
				len = dgets(s->des, 
					(char *)s->addrs + s->addr_offset, 
					s->addr_len - s->addr_offset, -2);

				if (len < s->addr_len - s->addr_offset)
				{
				    if (len < 0)
					yell("DNS lookup failed, possibly "
					     "because of a bug in "
					     "async_getaddrinfo!");
				    else if (len == 0)
					yell("Got part of the dns response, "
					     "waiting for the rest, "
					     "stand by...");
				    else
				    {
					yell("Got %ld, expected %ld bytes", 
						(long)len, 
						(long)(s->addr_len - s->addr_offset));
					s->addr_offset += len;
				    }
				    pop_message_from(l);
				    continue;
				}
				else
				{
				    unmarshall_getaddrinfo(s->addrs);
				    s->des = new_close(s->des);

				    s->next_addr = s->addrs;
				    for (cnt = 0; s->next_addr; s->next_addr = 
						s->next_addr->ai_next)
					cnt++;
				    say("DNS lookup for server %d [%s] "
					"returned (%d) addresses", 
					i, s->info->host, cnt);

				    s->next_addr = s->addrs;
				    s->addr_counter = 0;
				    connect_to_server(i);	/* This function advances us to SERVER_CONNECTING */
				}
			}
		}

		/* - - - - */
		/*
		 * First look for nonblocking connects that are finished.
		 */
		else if (s->state == SERVER_CONNECTING)
		{
			ssize_t c;
			SS	 name;

			if (x_debug & DEBUG_SERVER_CONNECT)
				yell("do_server: server [%d] is now ready to write", i);

#define DGETS(x, y) dgets( x , (char *) & y , sizeof y , -1);

			/* * */
			/* This is the errno value from getsockopt() */
			c = DGETS(des, retval)
			if (c < (ssize_t)sizeof(retval) || retval)
				goto something_broke;

			/* This is the socket error returned by getsockopt() */
			c = DGETS(des, retval)
			if (c < (ssize_t)sizeof(retval) || retval)
				goto something_broke;

			/* * */
			/* This is the errno value from getsockname() */
			c = DGETS(des, retval)
			if (c < (ssize_t)sizeof(retval) || retval)
				goto something_broke;

			/* This is the address returned by getsockname() */
			c = DGETS(des, name)
			if (c < (ssize_t)sizeof(name))
				goto something_broke;

			/* * */
			/* This is the errno value from getpeername() */
			c = DGETS(des, retval)
			if (c < (ssize_t)sizeof(retval) || retval)
				goto something_broke;

			/* This is the address returned by getpeername() */
			c = DGETS(des, name)
			if (c < (ssize_t)sizeof(name))
				goto something_broke;

			/* XXX - I don't care if this is abusive.  */
			if (0)
			{
something_broke:
				if (retval)
				{
					syserr(i, "Could not connect to server [%d] "
						"address [%d] because of error: %s", 
						i, s->addr_counter, strerror(retval));
				}
				else
					syserr(i, "Could not connect to server [%d] "
						"address [%d]: (Internal error)", 
						i, s->addr_counter);

				/* 
				 * This results in trying the next IP addr 
				 * Servers that have windows pointing to them
				 * that land in CLOSED that still have IP addrs
				 * left to try, will auto-resurrect in 
				 * window_check_servers().
				 * 
				 * It is not necessary for us to take any 
				 * action here
				 */
				set_server_state(i, SERVER_ERROR);
				close_server(i, NULL);
				pop_message_from(l);
				continue;
			}

			/* Update this! */
			*(SA *)&s->remote_sockname = *(SA *)&name;
			s->remote_paddr = inet_sa_to_paddr((SA *)&name, 0);
			say("Connected to IP address %s", s->remote_paddr);

			/*
			 * For SSL server connections, we have to take a little
			 * detour.  First we start up the ssl connection, which
			 * always returns before it completes.  Then we tell 
			 * newio to call the ssl connector when the fd is 
			 * ready, and change our state to tell us what we're 
			 * doing.
			 */
			if (!my_stricmp(get_server_type(i), "IRC-SSL"))
			{
				/* XXX 'des' might not be both the vfd and channel! */
				/* (ie, on systems where vfd != channel) */
				int	ssl_err;

				ssl_err =  ssl_startup(des, des, get_server_name(i));

				/* SSL connection failed */
				if (ssl_err == -1)
				{
					/* XXX I don't care if this is abusive. */
					syserr(i, "Could not start SSL connection to server "
						"[%d] address [%d]", 
						i, s->addr_counter);
					goto something_broke;
				}

				/* 
				 * For us, this is asynchronous.  For nonblocking
				 * SSL connections, we have to wait until later.
				 * For blocking connections, we choose to wait until
				 * later, since the return code is posted to us via
				 * dgets().
				 */
				set_server_state(i, SERVER_SSL_CONNECTING);
				new_open(des, do_server, NEWIO_SSL_CONNECT, 0, i);
				pop_message_from(l);
				break;
			}

return_from_ssl_detour:
			/*
			 * Our IO callback depends on our medium
			 */
			if (is_fd_ssl_enabled(des))
				new_open(des, do_server, NEWIO_SSL_READ, 0, i);
			else
				new_open(des, do_server, NEWIO_RECV, 0, i);

			/* Always try to fall back to the nick from the server description */
			/* This was discussed and agreed to in April 2016 */
			if (s->info && s->info->nick && *(s->info->nick))
				register_server(i, s->info->nick);
			else
				register_server(i, s->d_nickname);
		}

		/* - - - - */
		/*
		 * Above, we did new_open(..., NEWIO_SSL_CONNECT, ...)
		 * which leads us here when the ssl stuff posts a result code.
		 * If it failed, we punt on this address and go to the next.
		 * If it succeeded, we "return" from out detour and go back
		 * to the place in SERVER_CONNECTING we left off.
		 */
		else if (s->state == SERVER_SSL_CONNECTING)
		{
			ssize_t c;
			int	strict_retval = 0;

			if (x_debug & DEBUG_SERVER_CONNECT)
				yell("do_server: server [%d] finished ssl setup", i);

			c = DGETS(des, retval)
			if (c < (ssize_t)sizeof(retval) || retval)
			{
				syserr(i, "SSL_connect returned [%d]", retval);
				goto something_broke;
			}

			/* By default, we don't accept SSL Cert */
			s->accept_cert = -1;

			/* 
			 * This throws the /ON SSL_SERVER_CERT_LIST and makes
			 * the socket blocking again.
			 */
			if (ssl_connected(des) < 0)
			{
				syserr(i, "ssl_connected() failed");
				goto something_broke;
			}

			/* 
			 * For backwards compatability, if the user has already set accept_cert
			 * by here (they hooked /on server_ssl_cert) we just accept that.
			 * Otherwise, we decide what WE think.
			 */
			if (s->accept_cert == -1)
			{
#ifdef HAVE_SSL
				int 	verify_error,
					checkhost_error,
					self_signed_error,
					other_error;

				verify_error = get_ssl_verify_error(des);
				checkhost_error = get_ssl_checkhost_error(des);
				self_signed_error = get_ssl_self_signed_error(des);
				other_error = get_ssl_other_error(des);

				/*
				 * Policy checks for invalid ssl certs.
				 * If an SSL cert is invalid (verify_error != 0)
				 * It is for one of the three reasons
				 *   (self-signed, bad host, or other)
				 */
				if (verify_error)
				{

					/*
					 * If "other_error" is 0, then all of the errors
					 * were either self-signed or checkhost errors.
					 * We forgive those if the user said to
					 */
					if (!other_error && get_int_var(ACCEPT_INVALID_SSL_CERT_VAR))
					{
						syserr(i, "The SSL certificate for server %d has problems, "
							  "but /SET ACCEPT_INVALID_SSL_CERT is ON", i);
						s->accept_cert = 1;
					}
					else
					{
						syserr(i, "The SSL certificate for server %d is not "
							  "acceptable", i);
						s->accept_cert = 0;
					}
				}
				else
					s->accept_cert = 1;

				/*
				 * Let a script have a chance to overrule us
				 */
				do_hook(SERVER_SSL_EVAL_LIST, "%d %s %d %d %d %d %d",
								i, get_server_name(i),
								verify_error,
								checkhost_error,
								self_signed_error,
								other_error,
								s->accept_cert);

				if (s->accept_cert == 0)
				{
					syserr(i, "SSL Certificate Verification for server %d failed: (verify error: %d, checkhost error: %d, self_signed error: %d, other error: %d)", i, verify_error, checkhost_error, self_signed_error, other_error);
					goto something_broke;
				}
			    }
#endif

			goto return_from_ssl_detour;	/* All is well! */
		}

		/* - - - - */
		/* Everything else is a normal read. */
		else
		{
			last_server = i;
			junk = dgets(des, bufptr, get_server_line_length(i), 1);

			/* 
			 * If we were to support encapsulating protocols, 
			 * we would do the extraction here.  In the end, 
			 * we want 'bufptr' to contain the rfc1459 message,
			 * and whatever metadata would go into other vars.
			 *
			 * XXX TODO - We need to de-couple the protocol
			 * state (to the server) from the state of the
			 * socket we use to talk to it.
			 */

			switch (junk)
			{
				case 0:		/* Sit on incomplete lines */
					break;

				case -1:	/* EOF or other error */
				{
					parsing_server_index = i;
					server_is_unregistered(i);
					do_hook(RECONNECT_REQUIRED_LIST, "%d", i);
					close_server(i, NULL);
					say("Connection closed from %s", s->info->host);
					parsing_server_index = NOSERV;

					i++;		/* NEVER DELETE THIS! */
					break;
				}

				default:	/* New inbound data */
				{
					char *end;
					int	l2;

					end = strlen(buffer) + buffer;
					if (*--end == '\n')
						*end-- = '\0';
					if (*end == '\r')
						*end-- = '\0';

					rfc1459_any_to_utf8(bufptr, sizeof(buffer), &extra);
					if (extra)
						bufptr = extra;

					if (x_debug & DEBUG_INBOUND)
						yell("[%d] <- [%s]", 
							s->des, bufptr);

					parsing_server_index = i;
					/* I added this for caf. :) */
					if (do_hook(RAW_IRC_BYTES_LIST, "%s", buffer))
					{
					    /* XXX What should 2nd arg be? */
					    parse_server(bufptr, sizeof buffer);
					}
					parsing_server_index = NOSERV;

					new_free(&extra);
					break;
				}
			}
		}

		pop_message_from(l);
		from_server = primary_server;
	}

	if (!found)
	{
		syserr(i, "FD [%d] says it is a server but no server claims it.  Closing it", fd);
		new_close(fd);
	}
}


/* SERVER OUTPUT STUFF */
static void 	vsend_to_aserver_with_payload (int, const char *extra, const char *format, va_list args);
void		send_to_aserver_raw (int, size_t len, const char *buffer);

/*
 * send_to_aserver - Send a message to a specific irc server
 *
 * Arguments:
 *	refnum	- The server to send message to
 *	format 	- The message to send. (in UTF8)
 *
 * Note: Message will be translated to server's encoding before sending.
 */
void	send_to_aserver (int refnum, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vsend_to_aserver_with_payload(refnum, NULL, format, args);
	va_end(args);
}

/*
 * send_to_aserver_with_payload
 */
void	send_to_aserver_with_payload (int refnum, const char *payload, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vsend_to_aserver_with_payload(refnum, payload, format, args);
	va_end(args);
}
/*
 * send_to_server - Send a message to current irc server
 *
 * Arguments:
 *	format 	- The message to send. (in UTF8)
 *
 * Note: Message will be translated to server's encoding before sending.
 */
void	send_to_server (const char *format, ...)
{
	va_list args;
	int	server;

	if ((server = from_server) == NOSERV)
		server = primary_server;

	va_start(args, format);
	vsend_to_aserver_with_payload(server, NULL, format, args);
	va_end(args);
}

/*
 * send_to_server_with_payload
 */
void	send_to_server_with_payload (const char *payload, const char *format, ...)
{
	va_list args;
	int	server;

	if ((server = from_server) == NOSERV)
		server = primary_server;

	va_start(args, format);
	vsend_to_aserver_with_payload(server, payload, format, args);
	va_end(args);
}


/* send_to_server: sends the given info the the server */
/*
 * vsend_to_aserver - Generalized message sending to irc.
 *
 * Arguments:
 *	refnum	- The server to send message to
 *	payload	- An *ALREADY RECODED* "payload" message to append
 *	format	- The base part of the message (in UTF8)
 *	args	- Args to 'format'.
 *
 * Note: "format" + "args" will be converted to the server's encoding.
 * The resulting message will be:
 *	[format+args after conversion] :[payload as-is]
 * This allows you to send a message to someone encoded in one encoding,
 * which refering to their channel or nick as the server wants.
 */
static void 	vsend_to_aserver_with_payload (int refnum, const char *payload, const char *format, va_list args)
{
	Server *s;
	char	buffer[BIG_BUFFER_SIZE * 11 + 1]; /* make this buffer *much*
						  * bigger than needed */
	int	server_part_len;
	int	len,
		des;
	int	ofs;
	char *	extra = NULL;

	if (!(s = get_server(refnum)))
		return;

	/* No server or server is closed */
	if (refnum == NOSERV || ((des = s->des) == -1))
        {
	    if (do_hook(DISCONNECT_LIST,"No Connection to %d", refnum))
		say("You are not connected to a server, "
			"use /SERVER to connect.");
	    return;
        }

	/* No message to send */
	if (!format)
		return;


	/****************************************/
	/*
	 * 1. Press and translate the server part
	 */
	/* Keep the results short, and within reason. */
	server_part_len = vsnprintf(buffer, BIG_BUFFER_SIZE, format, args);

	/* XXX To be honest, this is so unlikely i'm not sure what to do here */
	if (server_part_len == -1)
		buffer[IRCD_BUFFER_SIZE - 200] = 0;

	if (outbound_line_mangler)
	{
	    char *s2;
	    s2 = new_normalize_string(buffer, 1, outbound_line_mangler);
	    strlcpy(buffer, s2, sizeof(buffer));
	    new_free(&s2);
	}

	outbound_recode(zero, refnum, buffer, &extra);
	if (extra)
	{
		strlcpy(buffer, extra, sizeof(buffer));
		new_free(&extra);
	}

	/****************************************/
	/*
	 * 2. Append the (already translated) payload part if necessary
	 */
	if (payload)
	{
		strlcat(buffer, " :", sizeof(buffer));
		if (outbound_line_mangler)
		{
		    char *s2;
		    s2 = new_normalize_string(payload, 1, outbound_line_mangler);
		    strlcat(buffer, s2, sizeof(buffer));
		    new_free(&s2);
		}
		else
		    strlcat(buffer, payload, sizeof(buffer));
	}

	/****************************************/
	/*
	 * Send the resulting message out
	 */
	len = strlen(buffer);
	s->sent = 1;
	if (len > (IRCD_BUFFER_SIZE - 2))
		buffer[IRCD_BUFFER_SIZE - 2] = 0;
	if (x_debug & DEBUG_OUTBOUND)
		yell("[%d] -> [%s]", des, buffer);
	strlcat(buffer, "\r\n", sizeof buffer);

	/* This "from_server" hack is for the benefit of do_hook. */
	ofs = from_server;
	from_server = refnum;

	/* XXX TODO - I don't like that this is ``encoded'' rather than utf8. */
	if (do_hook(SEND_TO_SERVER_LIST, "%d %d %s", from_server, des, buffer))
		send_to_aserver_raw(refnum, strlen(buffer), buffer);
	from_server = ofs;
}

void	send_to_aserver_raw (int refnum, size_t len, const char *buffer)
{
	Server *s;
	int des;
	int err = 0;

	if (!(s = get_server(refnum)))
		return;

	if ((des = s->des) != -1 && buffer)
	{
	    if (is_fd_ssl_enabled(des) == TRUE)
		err = ssl_write(des, buffer, len);
	    else
		err = write(des, buffer, len);

	    if (err == -1 && (!get_int_var(NO_FAIL_DISCONNECT_VAR)))
	    {
		if (is_server_registered(refnum))
		{
			say("Write to server failed.  Resetting connection.");
			set_server_state(refnum, SERVER_ERROR);
			do_hook(RECONNECT_REQUIRED_LIST, "%d", refnum);
			close_server(refnum, NULL);
		}
	    }
	}
}

void	flush_server (int servnum)
{
	if (!is_server_registered(servnum))
		return;
	set_server_redirect(servnum, "0");
	send_to_aserver(servnum, "%s", "***0");
}


/* CONNECTION/RECONNECTION STRATEGIES */
/*
 * Grab_server_address -- look up all of the addresses for a hostname and
 *	save them in the Server data for later use.  Someone must free
 * 	the results when they're done using the data (usually when we 
 *	successfully connect, or when we run out of addrs)
 *
 * XXX IMPORTANT XXX
 * 	This function technically is the entry point for a server connection "from scratch".
 *	It is only called by window_check_servers() when a window observes it
 * 	is associated with a server in the RECONNECT state.
 *
 *	Thus, to reconnect to a server, you put a server in a RECONNECT state.
 *	When a window notices this, it will kick off the reconnect (here)
 *
 *	After the server addresses are grabbed, do_server() will automatically
 * 	kick off connect_to_server(), the next function in the chain.
 */
int	grab_server_address (int server)
{
	Server *s;
	AI	hints;
	int	xvfd[2];

	if (x_debug & DEBUG_SERVER_CONNECT)
		yell("Grabbing server addresses for server [%d]", server);

	if (!(s = get_server(server)))
	{
		say("Server [%d] does not exist -- "
			"cannot do hostname lookup", server);
		return -1;		/* XXXX */
	}

	if (s->addrs)
	{
		if (x_debug & DEBUG_SERVER_CONNECT)
		    yell("This server still has addresses left over from "
			 "last time.  Starting over anyways...");
		new_free(&s->addrs);
		/* Freeaddrinfo(s->addrs); */
		s->addrs = NULL;
		s->next_addr = NULL;
	}

	set_server_state(server, SERVER_DNS);

	say("Performing DNS lookup for [%s] (server %d)", s->info->host, server);
	xvfd[0] = xvfd[1] = -1;
	if (socketpair(PF_UNIX, SOCK_STREAM, 0, xvfd))
		yell("socketpair: %s", strerror(errno));
	new_open(xvfd[1], do_server, NEWIO_READ, 1, server);

	memset(&hints, 0, sizeof(hints));
	if (empty(s->info->proto_type))
		hints.ai_family = AF_UNSPEC;
	else if (!my_stricmp(s->info->proto_type, "0")
	      || !my_stricmp(s->info->proto_type, "any") 
	      || !my_stricmp(s->info->proto_type, "ip") 
	      || !my_stricmp(s->info->proto_type, "tcp") )
		hints.ai_family = AF_UNSPEC;
	else if (!my_stricmp(s->info->proto_type, "4")
	      || !my_stricmp(s->info->proto_type, "tcp4") 
	      || !my_stricmp(s->info->proto_type, "ipv4") 
	      || !my_stricmp(s->info->proto_type, "v4") 
	      || !my_stricmp(s->info->proto_type, "ip4") )
		hints.ai_family = AF_INET;
#ifdef INET6
	else if (!my_stricmp(s->info->proto_type, "6")
	      || !my_stricmp(s->info->proto_type, "tcp6") 
	      || !my_stricmp(s->info->proto_type, "ipv6") 
	      || !my_stricmp(s->info->proto_type, "v6") 
	      || !my_stricmp(s->info->proto_type, "ip6") )
		hints.ai_family = AF_INET6;
#endif
	else
		hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_ADDRCONFIG;
	async_getaddrinfo(s->info->host, ltoa(s->info->port), &hints, xvfd[0]);
	close(xvfd[0]);
	s->des = xvfd[1];
	return 0;
}

/*
 * Make an attempt to connect to the next server address that will 
 * receive us.  This is the guts of "connectory", but "connectory" is
 * completely self-contained, and we have to be able to support looping
 * through getaddrinfo() results, possibly on multiple asynchronous
 * occasions.  So what we do is restart from where we left off before.
 */
static int	connect_next_server_address (int server)
{
	Server *s;
	int	fd = -1;
	SS	localaddr;
	socklen_t locallen;
	const AI *	ai;
	char	p_addr[256];
	char	p_port[24];

	if (!(s = get_server(server)))
	{
		syserr(-1, "connect_next_server_address: Server %d doesn't exist", 
						server);
		return -1;
	}

	if (!s->addrs)
	{
		syserr(server, "connect_next_server_address: There are no more "
			"addresses available for server %d", server);
		return -1;
	}

	s->addr_counter++;
	for (ai = s->next_addr; ai; ai = ai->ai_next, s->addr_counter++)
	{
	    if (x_debug & DEBUG_SERVER_CONNECT)
		yell("Trying to connect to server %d using address [%d] and protocol [%d]",
					server, s->addr_counter, ai->ai_family);

	    if ((inet_vhostsockaddr(ai->ai_family, -1, s->info->vhost,
						&localaddr, &locallen)) < 0)
	    {
#if 0
		/* 
		 * If using the server-specific vhost failed (possibly because 
		 * it does not resolve in the ai_family you're trying to 
		 * connect to), we will stake another stab without a vhost.
		 * This would fall back to your /hostname, if you have one.
		 */
		syserr(server, "connect_next_server_address: Your vhost for "
				"[%d] (%s) did not resolve - using your normal "
				"vhost (if you have one)", 
					server, s->info->vhost);
	        if ((inet_vhostsockaddr(ai->ai_family, -1, NULL,
						&localaddr, &locallen)) < 0)
#endif
		{
		    syserr(server, "connect_next_server_address: Can't use address [%d] "
				" because I can't get vhost for protocol [%d]",
					 s->addr_counter, ai->ai_family);
		    continue;
		}
	    }

	    if ((fd = client_connect((SA *)&localaddr, locallen, 
					ai->ai_addr, ai->ai_addrlen)) < 0)
	    {
		syserr(server, "connect_next_server_address: "
			"client_connect() failed for server %d address [%d].", 
					server, s->addr_counter);
		continue;
	    }

	    if (inet_ntostr(ai->ai_addr, p_addr, 256, p_port, 24, NI_NUMERICHOST))
		say("Connecting to server refnum %d (%s), using address %d",
					server, s->info->host, s->addr_counter);
	    else
		say("Connecting to server refnum %d (%s), "
				"using address %d (%s:%s)",
					server, s->info->host, 
					s->addr_counter, p_addr, p_port);

	    s->next_addr = ai->ai_next;
	    return fd;
	}

	say("I'm out of addresses for server %d so I have to stop.", 
			server);
	/* Freeaddrinfo(s->addrs); */
	/* s->addrs = NULL; */
	new_free(&s->addrs);
	s->next_addr = NULL;
	return -1;
}

/*
 * connect_to_server:  Supervision of a new network connection to server
 * 
 * Arguments:
 * 	new_server - A server refnum which has previously successfully completed a DNS lookup
 *			(ie, grab_server_addresses())
 *
 * This function is used by exactly two places:
 *	1) do_server() after a DNS lookup has completed
 *	2) /RECONNECT (disconnectcmd()) when a user wants to give up on a connection
 * The results of those two cases is "please try the next IP address"
 *
 * This function catches these errant initial conditions:
 *	1) You are trying to connect to an invalid server refnum
 *	2) The server is already open [this should be impossible, so we tell the user to /RECONNECT]
 * 
 * The function sets the server to CONNECTING and kicks off a nonblocking connect
 * (with connect_next_server_address(), which should only be called here!
 *  (and so maybe should actually be included in this function))
 * Then it handles some administrative book-keeping and returns.
 *
 * If for any reason this function cannot succeed to produce a nonblocking connection,
 * we recommend the user do a /RECONNECT,
 */
int 	connect_to_server (int new_server)
{
	int 		des;
	socklen_t	len;
	Server *	s;

	/*
	 * Can't connect to refnum -1, this is definitely an error.
	 */
	if (!(s = get_server(new_server)))
	{
		say("Connecting to server %d.  That makes no sense.", 
			new_server);
		return -1;		/* XXXX */
	}

	/*
	 * If we are already connected to the new server, go with that.
	 */
	if (s->des != -1)
	{
		say("Network connection to server %d at %s:%d is already open (state [%s])",
				new_server, s->info->host, s->info->port,
				server_states[s->state]);
		say("Use /RECONNECT if this connection is stuck");
		from_server = new_server;
		return -1;		/* Server is already connected */
	}

	/*
	 * Make an attempt to connect to the new server.
	 * XXX I am not sure all these should be done _here_.
	 */
	set_server_state(new_server, SERVER_CONNECTING);
	errno = 0;				/* XXX And why do we need to reset errno? */
	memset(&s->local_sockname, 0, sizeof(s->local_sockname));
	memset(&s->remote_sockname, 0, sizeof(s->remote_sockname));

	/*
	 * Get a nonblocking connect going
	 */
	if ((des = connect_next_server_address(new_server)) < 0)
	{
		if (x_debug & DEBUG_SERVER_CONNECT)
			say("new_des is %d", des);

		if ((s = get_server(new_server)))
		{
		    say("Unable to connect to server %d at %s:%d",
				new_server, s->info->host, s->info->port);
		}
		else
			say("Unable to connect to server %d: not a valid server refnum", new_server);

		say("Use /RECONNECT to reconnect to this server");
		set_server_state(new_server, SERVER_CLOSED);
		return -1;		/* Connect failed */
	}

	/*
	 * Now we have a valid nonblocking connect going
	 * Register the nonblocking connect with newio.
	 */
	if (x_debug & DEBUG_SERVER_CONNECT)
		say("connect_next_server_address returned [%d]", des);
	from_server = new_server;	/* XXX sigh */
	new_open(des, do_server, NEWIO_CONNECT, 0, from_server);

	/*
	 * Get the local IP socket address
	 * 
	 * (This test will appear odd to some:
	 *  "When would a hostname start with a slash?"
	 *  Unix Domain Sockets are "files" in your filesystem
	 *  which you can bind() and connect() to.   
	 *  I don't know if any ircd's still support them, but
	 *  i was using UDS for servers for local development
	 *  even as late as 2006.
 	 *  I've never seen any reason to remove support)
	 *
	 * Anyways, you can't call getsockname() on a UDS.
	 */
	if (*s->info->host != '/')
	{
		len = sizeof(s->local_sockname);
		getsockname(des, (SA *)&s->local_sockname, &len);
	}

	/*
	 * Initialize all of the server_list data items
	 * XXX I am not sure all these should be done _here_.
	 */
	s->des = des;


	clean_server_queues(new_server);	/* XXX Protocol level - should be somewhere else */

	/* So we set the default nickname for a server only when we use it */
	if (!s->d_nickname)			/* XXX Protocol level - should be somewhere else */
		malloc_strcpy(&s->d_nickname, nickname);

	/*
	 * Reset everything and go on our way.
	 */
	update_all_status();
	return 0;			/* New nonblocking connection established */
}

int 	close_all_servers (const char *message)
{
	int i;

	for (i = 0; i < number_of_servers; i++)
	{
		if (!get_server(i))
			continue;
		if (message)
			set_server_quit_message(i, message);
		close_server(i, NULL);
	}

	return 0;
}

/*
 * discard_dns_results:  Connecting to a server results in a DNS lookup,
 * which returns a set of IP addresses to try.  When we successfully connect
 * to a server, then we do not need the rest of the IP addresses we have.
 * That is to say, a reconnect should prompt a new DNS lookup.
 *
 * This is only to be done when the server state switches to ACTIVE.
 * If a connection fails at any previous state, we would want to try the
 * next IP address.
 */
static void	discard_dns_results (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	new_free(&s->addrs);
	s->next_addr = NULL;
}


/*
 * close_server: Given an index into the server list, this closes the
 * connection to the corresponding server.  If 'message' is anything other
 * than the NULL or the empty_string, it will send a protocol QUIT message
 * to the server before closing the connection.
 */
void	close_server (int refnum, const char *message)
{
	Server *s;
	int	was_registered;
	char *  sub_format;
	char 	final_message[IRCD_BUFFER_SIZE];

	/* Make sure server refnum is valid */
	if (!(s = get_server(refnum)))
	{
		yell("Closing server [%d] makes no sense!", refnum);
		return;
	}

	*final_message = 0;
	was_registered = is_server_registered(refnum);
	set_server_state(refnum, SERVER_CLOSING);
	if (s->waiting_out > s->waiting_in)		/* XXX - hack! */
		s->waiting_out = s->waiting_in = 0;

	destroy_waiting_channels(refnum);
	destroy_server_channels(refnum);

	new_free(&s->nickname);
	new_free(&s->s_nickname);
	new_free(&s->realname);

	/* 
	 * XXX Previously here, we discarded the extra IP addresses.
	 * but it was decided to do that only when we switched to
	 * ACTIVE rather than when we switched to CLOSED.  This permits 
	 * you to call close_server() and then connect_to_server()
	 */

	s->uh_addr_set = 0;

	if (s->des == -1)
		return;		/* Nothing to do here */

	if (was_registered)
	{
		if (!message)
		    if (!(message = get_server_quit_message(refnum)))
			message = "Leaving";
		sub_format = convert_sub_format(message, 's');
		snprintf(final_message, sizeof(final_message), sub_format, irc_version);
		new_free(&sub_format);

		if (x_debug & DEBUG_OUTBOUND)
			yell("Closing server %d because [%s]", refnum, final_message);
		if (*final_message)
			send_to_aserver(refnum, "QUIT :%s\n", final_message);

		server_is_unregistered(refnum);
	}

	do_hook(SERVER_LOST_LIST, "%d %s %s", 
			refnum, s->info->host, final_message);
	s->des = new_close(s->des);
	set_server_state(refnum, SERVER_CLOSED);
}

/********************* OTHER STUFF ************************************/

/* AWAY STATUS */
/*
 * Encapsulates everything we need to change our AWAY status.
 * This improves greatly on having everyone peek into that member.
 * Also, we can deal centrally with someone changing their AWAY
 * message for a server when we're not registered to that server
 * (when we do connect, then we send out the AWAY command.)
 * All this saves a lot of headaches and crashes.
 */
void	set_server_away (int refnum, const char *message)
{
	Server *s;

	if (!(s = get_server(refnum)))
	{
		say("You are not connected to a server.");
		return;
	}

	if (message && *message)
	{
		if (!s->away || strcmp(s->away, message))
			malloc_strcpy(&s->away, message);
		if (is_server_registered(refnum))
			send_to_aserver(refnum, "AWAY :%s", message);
	}
	else
	{
		new_free(&s->away);
		if (is_server_registered(refnum))
			send_to_aserver(refnum, "AWAY :");
	}
}

const char *	get_server_away (int refnum)
{
	Server *s;

	if (refnum == NOSERV)
	{
		int	i;

		for (i = 0; i < number_of_servers; i++)
		{
			if (!(s = get_server(i)))
				continue;

			if (is_server_registered(i) && s->away)
				return s->away;
		}

		return NULL;
	}

	if (!(s = get_server(refnum)))
		return NULL;
	
	return s->away;
}


/* USER MODES */
const char *	get_umode (int refnum)
{
	Server *s;
	char *	retval;

	if (!(s = get_server(refnum)))
		return empty_string;

	retval = s->umode;
	return retval;		/* Eliminates a specious warning from gcc. */
}

static void	add_user_mode (int refnum, int mode)
{
	Server *s;
	char c, *p, *o;
	char new_umodes[1024];		/* Too huge for words */
	int	i;

	if (!(s = get_server(refnum)))
		return;

	/* 
	 * 'c' is the mode that is being added
	 * 'o' is the umodes that are already set
	 * 'p' is the string that we are building that adds 'c' to 'o'.
	 */
	c = (char)mode;
	o = s->umode;
	p = new_umodes;

	/* Copy the modes in 'o' that are alphabetically less than 'c' */
	for (i = 0; o && o[i]; i++)
	{
		if (o[i] >= c)
			break;
		*p++ = o[i];
	}

	/* If 'c' is already set, copy it, otherwise add it. */
	if (o && o[i] == c)
		*p++ = o[i++];
	else
		*p++ = c;

	/* Copy all the rest of the modes */
	for (; o && o[i]; i++)
		*p++ = o[i];

	/* Nul terminate the new string and reset the server's info */
	*p++ = 0;
	strlcpy(s->umode, new_umodes, 54);
}

static void	remove_user_mode (int refnum, int mode)
{
	Server *s;
	char c, *o, *p;
	char new_umodes[1024];		/* Too huge for words */
	int	i;

	if (!(s = get_server(refnum)))
		return;

	/* 
	 * 'c' is the mode that is being deleted
	 * 'o' is the umodes that are already set
	 * 'p' is the string that we are building that adds 'c' to 'o'.
	 */
	c = (char)mode;
	o = s->umode;
	p = new_umodes;

	/*
	 * Copy the whole of 'o' to 'p', except for any instances of 'c'.
	 */
	for (i = 0; o && o[i]; i++)
	{
		if (o[i] != c)
			*p++ = o[i];
	}

	/* Nul terminate the new string and reset the server's info */
	*p++ = 0;
	strlcpy(s->umode, new_umodes, 54);
}

static void 	clear_user_modes (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	*s->umode = 0;
}

void	update_user_mode (int refnum, const char *modes)
{
	int		onoff = 1;

	for (; *modes; modes++)
	{
		if (*modes == '-')
			onoff = 0;
		else if (*modes == '+')
			onoff = 1;
		else if (onoff == 1)
			add_user_mode(refnum, *modes);
		else if (onoff == 0)
			remove_user_mode(refnum, *modes);
	}
	update_all_status();
}

static void	reinstate_user_modes (void)
{
	const char *modes = get_umode(from_server);

	if (!modes || !*modes)
		modes = send_umode;

	if (modes && *modes)
	{
		if (x_debug & DEBUG_OUTBOUND)
			yell("Reinstating your user modes on server [%d] to [%s]", from_server, modes);
		send_to_server("MODE %s +%s", get_server_nickname(from_server), modes);
		clear_user_modes(from_server);
	}
}

int	get_server_operator (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return 0;

	if (strchr(s->umode, 'O') || strchr(s->umode, 'o'))
		return 1;
	else
		return 0;
}


/* get_server_ssl_enabled: returns 1 if the server is using SSL connection */
int	get_server_ssl_enabled (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return 0;

	return (is_fd_ssl_enabled(s->des) == TRUE ? 1 : 0);
}

const char	*get_server_ssl_cipher (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return empty_string;
	if (!is_fd_ssl_enabled(s->des))
		return empty_string;
	return get_ssl_cipher(s->des);
}


/* CONNECTION/REGISTRATION STATUS */
void	register_server (int refnum, const char *nick)
{
	Server *	s;
	int		ofs = from_server;
	const char *	usehost;

	if (!(s = get_server(refnum)))
		return;

	if (get_server_state(refnum) != SERVER_CONNECTING &&
	    get_server_state(refnum) != SERVER_SSL_CONNECTING)
	{
		if (x_debug & DEBUG_SERVER_CONNECT)
			say("Server [%d] state should be [%d] but it is [%d]", 
				refnum, SERVER_CONNECTING, 
				get_server_state(refnum));
		return;		/* Whatever */
	}

	if (is_server_registered(refnum))
	{
		if (x_debug & DEBUG_SERVER_CONNECT)
			say("Server [%d] is already registered", refnum);
		return;		/* Whatever */
	}

	set_server_state(refnum, SERVER_REGISTERING);

	from_server = refnum;
	do_hook(SERVER_ESTABLISHED_LIST, "%s %d",
		get_server_name(refnum), get_server_port(refnum));
	from_server = ofs;

	if (!empty(s->info->password))
	{
		char *dequoted = NULL;
		malloc_strcat_ues(&dequoted, s->info->password, "\\:");
		send_to_aserver(refnum, "PASS %s", dequoted);
		new_free(&dequoted);
	}

	malloc_strcpy (&s->realname, 
		(s->default_realname == NULL) ?
			(get_string_var(DEFAULT_REALNAME_VAR) ?
		 		get_string_var(DEFAULT_REALNAME_VAR) : 
				space) :
		s->default_realname);

	/* 
	 * Use the correct vhost for the socket type
	 * (ie, if we connected via ipv6, use the ipv6 vhost)
	 * If we're not using a vhost, then just fallback to hostname.
	 */
	if (((SA *)&s->remote_sockname)->sa_family == AF_INET)
		usehost = LocalIPv4HostName;
	else if (((SA *)&s->remote_sockname)->sa_family == AF_INET6)
		usehost = LocalIPv6HostName;
	else
		usehost = hostname;
	if (usehost == NULL)
		usehost = hostname;

	send_to_aserver(refnum, "USER %s %s %s :%s", 
			get_string_var(DEFAULT_USERNAME_VAR),
			(send_umode && *send_umode) ? send_umode : 
			usehost,
			get_string_var(DEFAULT_USERNAME_VAR),
			s->realname);
	change_server_nickname(refnum, nick);
	if (x_debug & DEBUG_SERVER_CONNECT)
		yell("Registered with server [%d]", refnum);
}

static const char *	get_server_password (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return NULL;

	return s->info->password;
}

/*
 * set_server_password: this sets the password for the server with the given
 * index. If 'password' is NULL, the password is cleared
 */
static void	set_server_password (int refnum, const char *password)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	if (password)
		s->info->password = password;
	else
		s->info->password = empty_string;

	preserve_serverinfo(s->info);
}

/*
 * password_sendline: called by send_line() in get_password() to handle
 * hitting of the return key, etc 
 * -- Callback function
 */
void 	password_sendline (char *data, const char *line)
{
	int	new_server;

	if (!line || !*line)
		return;

	new_server = str_to_servref(data);
	set_server_password(new_server, line);
	close_server(new_server, NULL);
	set_server_state(new_server, SERVER_RECONNECT);
}


/*
 * is_server_open: Returns true if the given server index represents a server
 * with a live connection, returns false otherwise 
 */
int	is_server_open (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return 0;

	return (s->des != -1);
}

int	is_server_registered (int refnum)
{
	int	state;

	if (!get_server(refnum))
		return 0;

	state = get_server_state(refnum);
	if (state == SERVER_SYNCING  || state == SERVER_ACTIVE)
		return 1;
	else
		return 0;
}


/*
 * Informs the client that the user is now officially registered or not
 * registered on the specified server.
 */
void  server_is_registered (int refnum, const char *itsname, const char *ourname)
{
	Server *s;
	int	winref;

	if (!(s = get_server(refnum)))
		return;

	/* Throw away the rest of addresses to stop reconnections */
	if (x_debug & DEBUG_SERVER_CONNECT)
	    yell("We're connected! Throwing away the rest of the addrs");
	/* Freeaddrinfo(s->addrs); */
	/* s->addrs = NULL; */
	new_free(&s->addrs);
	s->next_addr = NULL;

	set_server_state(refnum, SERVER_SYNCING);

	accept_server_nickname(refnum, ourname);
	set_server_itsname(refnum, itsname);

	if ((winref = get_winref_by_servref(refnum)) != -1)
	    if (new_server_lastlog_mask)
		renormalize_window_levels(winref, *new_server_lastlog_mask);

	/*
	 * This hack is required by a race condition with freebsd that 
	 * I'm seeing on epicsol.  For reasons that I have never been able
	 * to adequately explain, if I write out data to the socket (ie,
	 * from reinstate_user_modes) at the same time as the kernel is
	 * reassembling a fractured packet (ie, the initial packet from 
	 * the server with the 001 and stuff), the kernel will refuse to 
	 * flag the socket as read()able ever again.  I've confirmed this
	 * with select(), poll(), kqueue(), and even a blocking read().
	 * Wireshark shows that the packet(s) do come in, bu the kernel
	 * refuses to give them to me.  This tiny sleep eliminates the 
	 * race condition that consistently causes this problem.
	 *
	 * P.S. -- Yes, I tried different nic cards using different drvivers.
	 * Yes, I've tried multiple versions of freebsd.
	 */
	my_sleep(0.005);

	reinstate_user_modes();
	userhostbase(from_server, NULL, NULL, got_my_userhost, 1);

	if (default_channel)
	{
		e_channel("JOIN", default_channel, empty_string);
		new_free(&default_channel);
	}

	if (get_server_away(refnum))
		set_server_away(from_server, get_server_away(from_server));

	update_all_status();
	do_hook(CONNECT_LIST, "%s %d %s", get_server_name(refnum), 
					get_server_port(refnum), 
					get_server_itsname(from_server));
	window_check_channels();
	set_server_state(refnum, SERVER_ACTIVE);

	/* 
	 * When we hit ACTIVE, we discard other DNS results so that any 
	 * reconnect results in a new DNS lookup.  If a failure occurs 
	 * before we hit this point, we will move to the next saved DNS 
	 * address on failure.
	 * (This is especially relevant for failed nonblocking connects)
	 */
	discard_dns_results(refnum);

	isonbase(from_server, NULL, NULL);
}

void	server_is_unregistered (int refnum)
{
	if (!get_server(refnum))
		return;

	destroy_005(refnum);
	set_server_state(refnum, SERVER_EOF);
}

int	is_server_active (int refnum)
{
	if (get_server(refnum))
		return 0;

	if (get_server_state(refnum) == SERVER_ACTIVE)
		return 1;
	return 0;
}

int	is_server_valid (int refnum)
{
	if (get_server(refnum))
		return 1;
	return 0;
}


/*
 * Despite its name, this is the handler for both /DISCONNECT and /RECONNECT.
 * The "recon" variable controls which one we're doing.
 */
BUILT_IN_COMMAND(disconnectcmd)
{
	char	*server;
	const char *message;
	int	i;
	int	discon = !strcmp(command, "DISCONNECT");
	int	recon = strcmp(command, "DISCONNECT");

	if (!(server = next_arg(args, &args)))
	{
		if ((i = from_server) == NOSERV)
			i = get_window_server(0);
	}
	else
	{
		if ((i = str_to_servref(server)) == NOSERV)
		{
			say("No such server!");
			return;
		}
	}

	if (recon && is_server_registered(i))
	{
		say("You cannot /RECONNECT to a server you are actively on (%s)", get_server_itsname(i));
		say("Use /DISCONNECT first.  This is a safety valve");
		return;
	}

	if (get_server(i))
	{
		if (is_server_open(i))
		{
		    if (args && *args)
			message = args;
		    else if (recon)
			message = "Reconnecting";
		    else
			message = "Disconnecting";

		    say("Disconnecting from server %s", get_server_itsname(i));
		    close_server(i, message);
		    update_all_status();
		}
		else if (discon)
		    say("You are already disconnected from server %s",
						get_server_itsname(i));
	}

	if (discon && !connected_to_server)
                if (do_hook(DISCONNECT_LIST, "Disconnected by user request"))
			say("You are not connected to a server, use /SERVER to connect.");

	if (recon && connect_to_server(i) < 0)
	{
		set_server_state(i, SERVER_RECONNECT);
		say("Reconnecting to server %s", get_server_itsname(i));
	}
} 

BUILT_IN_COMMAND(reconnectcmd)
{
	disconnectcmd(command, args, subargs);
}

/**************************************************************************/
/* Getters and setters and stuff, oh my! */

/* PORTS */
static void    set_server_port (int refnum, int port)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	s->info->port = port;
	preserve_serverinfo(s->info);
}

/* get_server_port: Returns the connection port for the given server index */
int	get_server_port (int refnum)
{
	Server *s;
	char	p_port[12];

	if (!(s = get_server(refnum)))
		return 0;

	if (is_server_open(refnum))
	   if (!inet_ntostr((SA *)&s->remote_sockname, NULL, 0, p_port, 12, 0))
		return atol(p_port);

	return s->info->port;
}

int	get_server_local_port (int refnum)
{
	Server *s;
	char	p_port[12];

	if (!(s = get_server(refnum)))
		return 0;

	if (is_server_open(refnum))
	    if (!inet_ntostr((SA *)&s->local_sockname, NULL, 0, p_port, 12, 0))
		return atol(p_port);

	return 0;
}

static const char *	get_server_remote_paddr (int refnum)
{
	Server *	s;

	if (!(s = get_server(refnum)))
		return 0;

	if (is_server_open(refnum) && s->remote_paddr)
		return s->remote_paddr;

	return empty_string;
}


static SS	get_server_remote_addr (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
	    panic(1, "Refnum %d isn't valid in get_server_remote_addr", refnum);

	return s->remote_sockname;
}

SS	get_server_local_addr (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		panic(1, "Refnum %d isn't valid in get_server_local_addr", refnum);

	return s->local_sockname;
}

SS	get_server_uh_addr (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		panic(1, "Refnum %d isn't valid in get_server_uh_addr", refnum);

	if (s->uh_addr_set == 0)
	{
		set_server_uh_addr(refnum);
		s->uh_addr_set = 1;
	}

	return s->uh_addr;
}

/* USERHOST */
static void	set_server_userhost (int refnum, const char *uh)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	malloc_strcpy(&s->userhost, uh);
}

static void	set_server_uh_addr (int refnum)
{
	Server *s;
	char *host;
	const char *uh;

	if (!(s = get_server(refnum)))
		return;

	uh = s->userhost;

	if (!(host = strchr(uh, '@')))
	{
		yell("Cannot set your userhost to [%s] because it does not"
		      "contain a @ character!", uh);
		return;
	}

	/* Ack!  Oh well, it's for DCC. */
	FAMILY(s->uh_addr) = AF_INET;
	if (inet_strton(host + 1, zero, (SA *)&s->uh_addr, AI_ADDRCONFIG))
        {
		/* 
		 * Once upon a time this warning was relevant to people
		 * who put their machines in the DMZ of their router and
		 * who needed the irc server to tell them what their
		 * hostname was for DCC purposes.  But the message is 
		 * annoying and the people who need to be told this won't
		 * work is vanishingly small.
		 *
		 * An error message will only be output when a fake hostname
		 * causes a /DCC to actually fail.
		 */
		yell("Ack.  The server says your userhost is [%s] and "
		     "I can't figure out the IPv4 address of that host! "
		     "If you use /SET DCC_USE_GATEWAY_ADDR ON (because "
                     "you're in the DMZ behind a NAT firewall), DCC won't "
                     "work with this server connection!", host + 1);
	}
}

/*
 * get_server_userhost: return the userhost for this connection to server
 */
const char	*get_server_userhost (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)) || !s->userhost)
		return get_userhost();

	return s->userhost;
}


/* COOKIES */
void	use_server_cookie (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	if (s->cookie)
		send_to_aserver(refnum, "COOKIE %s", s->cookie);
}


/* NICKNAMES */
/*
 * get_server_nickname: returns the current nickname for the given server
 * index 
 */
const char	*get_server_nickname (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return "<invalid server>";

	if (s->nickname)
		return s->nickname;

	return "<not registered yet>";
}

int	is_me (int refnum, const char *nick)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return 0;

	if (s->nickname && nick)
		return !my_stricmp(nick, s->nickname);

	return 0;
}

/*
 * This is the function to attempt to make a nickname change.  You
 * cannot send the NICK command directly to the server: you must call
 * this function.  This function makes sure that the neccesary variables
 * are set so that if the NICK command fails, a sane action can be taken.
 *
 * If ``nick'' is NULL, then this function just tells the server what
 * we're trying to change our nickname to.  If we're not trying to change
 * our nickname, then this function does nothing.
 */
void	change_server_nickname (int refnum, const char *nick)
{
	Server *s;
	const char *id;

	if (!(s = get_server(refnum)))
		return;			/* Uh, no. */

	if (nick)
	{
		/* If changing to our Unique ID, the default nickname is 0 */
		id = get_server_unique_id(refnum);
		if (id && !my_stricmp(nick, id))
			malloc_strcpy(&s->d_nickname, zero);
		else
			malloc_strcpy(&s->d_nickname, nick);

		/* Make a note that we are changing our nickname */
		malloc_strcpy(&s->s_nickname, nick);
	}

	if (s->s_nickname && is_server_open(refnum))
		send_to_aserver(refnum, "NICK %s", s->s_nickname);
}

const char *	get_pending_nickname (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return NULL;

	return s->s_nickname;
}

void	accept_server_nickname (int refnum, const char *nick)
{
	Server *s;
	const char *id;

	if (!(s = get_server(refnum)))
		return;

	/* We always accept whatever the server says our new nick is. */
	malloc_strcpy(&s->nickname, nick);
	new_free(&s->s_nickname);

	/* Change our default nickname to our new nick, or 0 for unique id's */
	id = get_server_unique_id(refnum);
	if (id && !my_stricmp(nick, id))
		malloc_strcpy(&s->d_nickname, zero);
	else
		malloc_strcpy(&s->d_nickname, nick);

	/* Change the global nickname for primary server (die, die!) */
	if (refnum == primary_server)
		strlcpy(nickname, nick, sizeof nickname);

	update_all_status();
}

void	nickname_change_rejected (int refnum, const char *mynick)
{
	if (is_server_registered(refnum))
	{
		accept_server_nickname(refnum, mynick);
		return;
	}

	reset_nickname(refnum);
}

/*
 * reset_nickname: when the server reports that the selected nickname is not
 * a good one, it gets reset here. 
 * -- Called by more than one place
 */
static void 	reset_nickname (int refnum)
{
	Server *s;
	char *	old_pending = NULL;

	if (!(s = get_server(refnum)))
		return; 		/* Don't repeat the reset */

	if (s->s_nickname)
		old_pending = LOCAL_COPY(s->s_nickname);

	do_hook(NEW_NICKNAME_LIST, "%d %s %s", refnum, 
			s->nickname ? s->nickname : "*", 
			s->s_nickname ? s->s_nickname : "*");

	if (!(s = get_server(refnum)))
		return;			/* Just in case the user punted */

	/* Did the user do a /NICK in the /ON NEW_NICKNAME ? */
	if (s->s_nickname == NULL || 
		(old_pending && !strcmp(old_pending, s->s_nickname)))
	{
	    say("Use the /NICK command to set a new nick to continue "
			"connecting.");
	    say("If you get disconnected, you will also need to do "
			"/server +%d to reconnect.", refnum);
	}
	update_all_status();
}


/* REDIRECT STUFF */
int	check_server_redirect (int refnum, const char *who)
{
	Server *s;

	if (!who || !(s = get_server(refnum)) || !s->redirect)
		return 0;

	if (!strncmp(who, "***", 3) && !strcmp(who + 3, s->redirect))
	{
		set_server_redirect(refnum, NULL);
		if (!strcmp(who + 3, "0"))
			say("Server flush done.");
		return 1;
	}

	return 0;
}

/*****************************************************************************/
/* GETTERS AND SETTERS FOR MORE MUNDANE THINGS */

/* A setter for a mundane integer field */
#define SET_IATTRIBUTE(param, member) \
void	set_server_ ## member (int servref, int param )	\
{							\
	Server *s;					\
							\
	if (!(s = get_server(servref)))			\
		return;					\
							\
	s-> member = param;				\
}

/* A getter for a mundane integer field */
#define GET_IATTRIBUTE(member) \
int	get_server_ ## member (int servref)		\
{							\
	Server *s;					\
							\
	if (!(s = get_server(servref)))			\
		return -1;				\
							\
	return s-> member ;				\
}

/* A setter for a mundane string field */
#define SET_SATTRIBUTE(param, member) \
void	set_server_ ## member (int servref, const char * param )	\
{							\
	Server *s;					\
							\
	if (!(s = get_server(servref)))			\
		return;					\
							\
	malloc_strcpy(&s-> member , param);		\
}

/* A getter for a mundane string field */
#define GET_SATTRIBUTE(member, default)			\
const char *	get_server_ ## member (int servref ) \
{							\
	Server *s;					\
							\
	if (!(s = get_server(servref)))			\
		return default ;			\
							\
	if (s-> member )				\
		return s-> member ;			\
	else						\
		return default ;			\
}

/* A getter and a setter for a mundane integer field */
#define IACCESSOR(param, member)		\
SET_IATTRIBUTE(param, member)			\
GET_IATTRIBUTE(member)

/* A getter and a setter for a mundane string field */
#define SACCESSOR(param, member, default)	\
SET_SATTRIBUTE(param, member)			\
GET_SATTRIBUTE(member, default)

/* A getter and a static setter for a mundane string field */
#define SSACCESSOR(param, member, default)	\
static SET_SATTRIBUTE(param, member)			\
GET_SATTRIBUTE(member, default)

/* Various mundane getters and setters */
IACCESSOR(v, doing_privmsg)
IACCESSOR(v, doing_notice)
IACCESSOR(v, doing_ctcp)
IACCESSOR(v, sent)
IACCESSOR(v, line_length)
IACCESSOR(v, max_cached_chan_size)
IACCESSOR(v, ison_len)
IACCESSOR(v, ison_max)
IACCESSOR(v, userhost_max)
IACCESSOR(v, stricmp_table)
IACCESSOR(v, autoclose)
IACCESSOR(v, accept_cert)
SACCESSOR(chan, invite_channel, NULL)
SACCESSOR(nick, last_notify_nick, NULL)
SACCESSOR(nick, joined_nick, NULL)
SACCESSOR(nick, public_nick, NULL)
SACCESSOR(nick, recv_nick, NULL)
SACCESSOR(nick, sent_nick, NULL)
SACCESSOR(text, sent_body, NULL)
SACCESSOR(nick, redirect, NULL)
SACCESSOR(message, quit_message, get_string_var(QUIT_MESSAGE_VAR))
SACCESSOR(cookie, cookie, NULL)
SACCESSOR(ver, version_string, NULL)
SACCESSOR(name, default_realname, get_string_var(DEFAULT_REALNAME_VAR))
GET_SATTRIBUTE(realname, NULL)

/* * * * */
/* Getters and setters that require special handling somehow. */
 
/*
 * Getter and setter for (IRCNet) "Unique ID"
 */
GET_SATTRIBUTE(unique_id, NULL)
void	set_server_unique_id (int servref, const char * id)
{
	Server *s;

	if (!(s = get_server(servref)))
		return;

	malloc_strcpy(&s->unique_id , id);
	if (id && s->d_nickname && !my_stricmp(id, s->d_nickname))
		malloc_strcpy(&s->d_nickname, zero);
}


/*
 * Getter and setter for Server Status ("server state")
 */
GET_IATTRIBUTE(state)
static void	set_server_state (int refnum, int new_state)
{
	Server *s;
	int	old_state;
	const char *oldstr, *newstr;

	if (!(s = get_server(refnum)))
		return;

	if (new_state < 0 || new_state > SERVER_DELETED)
		return;			/* Not acceptable */

	old_state = s->state;
	if (old_state < 0 || old_state > SERVER_DELETED)
		oldstr = "UNKNOWN";
	else
		oldstr = server_states[old_state];

	s->state = new_state;
	newstr = server_states[new_state];
	do_hook(SERVER_STATE_LIST, "%d %s %s", refnum, oldstr, newstr);
	do_hook(SERVER_STATUS_LIST, "%d %s %s", refnum, oldstr, newstr);
	update_all_status();
}

const char *	get_server_state_str (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return empty_string;

	return server_states[s->state];
}


/*
 * Getter for the full server description string (irc.host.com:port:::...)
 */
static const char *	get_server_fulldesc (int servref)
{
	Server *s;

	if (!(s = get_server(servref)))
		return NULL;

	if (s->info->fulldesc)
		return s->info->fulldesc;
	else
		return NULL;
}

/*
 * Getter and setter for "name" (ie, "ourname")
 */
static void	set_server_name (int servref, const char * param )
{
	Server *s;

	if (!(s = get_server(servref)))
		return;

	s->info->host = param;
	preserve_serverinfo(s->info);
}

const char *	get_server_name (int servref )
{
	Server *s;

	if (!(s = get_server(servref)))
		return "<none>";

	if (s->info->host)
		return s->info->host;
	else
		return "<none>";
}

/*
 * Getter and setter for "server_group"
 */
void	set_server_group (int servref, const char * param )
{
	Server *s;

	if (!(s = get_server(servref)))
		return;

	s->info->group = param;
	preserve_serverinfo(s->info);
}

const char *	get_server_group (int servref)
{
	Server *s;

	if (!(s = get_server(servref)))
		return "<default>";

	if (!empty(s->info->group))
		return s->info->group;
	else
		return "<default>";
}

/*
 * Getter and setter for "server_type"
 */
void	set_server_server_type (int servref, const char * param )
{
	Server *s;

	if (!(s = get_server(servref)))
		return;

	s->info->server_type = param;
	preserve_serverinfo(s->info);
}

/*
 * This returne either "IRC" or "IRC-SSL" for now.
 * XXX - I really regret calling this field "type".
 */
static const char *	get_server_type (int servref )
{
	Server *s;

	if (!(s = get_server(servref)))
		return "IRC";

	if (!empty(s->info->server_type))
		return s->info->server_type;
	else
		return "IRC";
}

/*
 * Getter and setter for "vhost"
 */
void	set_server_vhost (int servref, const char * param )
{
	Server *s;

	if (!(s = get_server(servref)))
		return;

	s->info->vhost = param;
	preserve_serverinfo(s->info);
}

const char *	get_server_vhost (int servref )
{
	Server *s;

	if (!(s = get_server(servref)))
		return "<none>";

	if (s->info->vhost && *s->info->vhost)
		return s->info->vhost;
	else
		return "<none>";
}


/* 
 * Getter and setter for "itsname"
 */
static SET_SATTRIBUTE(name, itsname)
const char	*get_server_itsname (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return "<none>";

	if (s->itsname)
		return s->itsname;
	else
		return s->info->host;
}

/*
 * Getter and setter for "protocol_state"
 */
int	get_server_protocol_state (int refnum)
{
	int	retval = 0;

	retval = (get_server_doing_ctcp(refnum) & 0xFF);
	retval = retval << 8;

	retval += (get_server_doing_notice(refnum) & 0xFF);
	retval = retval << 8;

	retval += (get_server_doing_privmsg(refnum) & 0xFF);

	return retval;
}

void	set_server_protocol_state (int refnum, int state)
{
	int	val;

	val = state & 0xFF;
	set_server_doing_privmsg(refnum, val);
	state = state >> 8;

	val = state & 0xFF;
	set_server_doing_notice(refnum, val);
	state = state >> 8;

	val = state & 0xFF;
	set_server_doing_ctcp(refnum, val);
	/* state = state >> 8; */
}

/***********************************************************************/
/* WAIT STUFF */
/*
 * server_hard_wait -- Do not return until one round trip to the server 
 *			is completed.
 *
 * Arguments:
 *	i	- A server refnum
 *
 * Notes:
 * 	- This is the /WAIT command.
 *	- This function does not return until this WAIT _and all 
 *	  subsequent WAITs launched while this one is pending_ 
 *	  have completed.  This is an unspecified amount of time.
 *	- See the comments for check_server_wait() for more info.
 */
void 	server_hard_wait (int i)
{
	Server *s;
	int	proto, old_from_server;
	char	reason[1024];

	if (!(s = get_server(i)))
		return;

	if (!is_server_registered(i))
		return;

	snprintf(reason, 1024, "WAIT on server %d", i);
	proto = get_server_protocol_state(i);
	old_from_server = from_server;

	s->waiting_out++;
	lock_stack_frame();
	send_to_aserver(i, "%s", lame_wait_nick);
	while ((s = get_server(i)) && (s->waiting_in < s->waiting_out))
		io(reason);

	set_server_protocol_state(i, proto);
	from_server = old_from_server;
}

/*
 * server_passive_wait - Register a non-recursive callback promise
 *
 * Arguments:
 *	i	- A server refnum
 *	stuff	- A block of code to run at a later time
 *
 * Notes:
 *	This is the /WAIT -CMD command.
 * 	'stuff' will be run after one round trip to the server 'i'.
 *	See the comments for check_server_wait() for more info.
 */
void	server_passive_wait (int i, const char *stuff)
{
	Server *s;
	WaitCmd	*new_wait;

	if (!(s = get_server(i)))
		return;

	new_wait = (WaitCmd *)new_malloc(sizeof(WaitCmd));
	new_wait->stuff = malloc_strdup(stuff);
	new_wait->next = NULL;

	if (s->end_wait_list)
		s->end_wait_list->next = new_wait;
	s->end_wait_list = new_wait;
	if (!s->start_wait_list)
		s->start_wait_list = new_wait;

	send_to_aserver(i, "%s", wait_nick);
}

/*
 * check_server_wait - A callback to see if a WAIT has completed
 *
 * Arguments:
 *	refnum	- A server refnum that has sent us a token (see below)
 *	nick	- The token sent to us
 *
 * Return value:
 *	1	- This token represents a valid WAIT request
 *	0	- This token does not represent a valid WAIT request.
 *
 * - Backstory about hard waits:
 * The /WAIT command performs a (to your script) blocking synchronization
 * with the server.  This means it does not return until all of the 
 * commands you have previously sent to the server have been completed
 * (and any /ONs have been run).  This is done by recursively calling 
 * the main loop until one round trip to the server is completed.
 *
 * How it does this is in server_hard_wait() [see above].  It will send
 * an invalid command to the server  (lame_wait_nick) and wait for the
 * server to send a 421 NOSUCHCOMMAND numeric back to us.  
 *
 * Ordinarily, this would be non-controversial, except that you might do
 * a /WAIT while another /WAIT is already pending.  This can get ugly, 
 * so how we choose to manage that is, _No WAIT shall return until ALL 
 * pending WAITs have completed_.  This means a WAIT does not return at 
 * the first possible convenience; but only when it is guaranteed to be
 * safe.  This means anything you do after a WAIT (such as cleanup) might
 * have occurred after several consecutive WAITs have happened.  That's just
 * a risk you have to take.
 *
 * - Backstory about soft waits:
 * The /WAIT -CMD command implements a "promise" feature, where it will
 * record a block of code to be run at a later time, after one round trip
 * to the server has occurred.  Because the client does not implement
 * closures, this is not as flexible, since you can only converse between
 * the calling scope and the /WAIT -CMD scope through global variables, and
 * that means you don't have re-entrancy.  However, you do get the promise
 * that your commands will be run at the first possible convenience.
 *
 * - /WAITs and /WAIT -CMDs play nicely with each other.
 *
 * This function should only be called by the 421 Numeric Handler.
 * If the "invalid command" is a hard wait, we record that
 * If the "invalid command" is a soft wait, we run the appropriate callback.
 */
int	check_server_wait (int refnum, const char *nick)
{
	Server	*s;

	if (!(s = get_server(refnum)))
		return 0;

	/* Hard waits */
	if ((s->waiting_out > s->waiting_in) && !strcmp(nick, lame_wait_nick))
	{
		s->waiting_in++;
		unlock_stack_frame();
	        return 1;
	}

	/* Soft waits */
	if (s->start_wait_list && !strcmp(nick, wait_nick))
	{
		WaitCmd *old = s->start_wait_list;

		s->start_wait_list = old->next;
		if (old->stuff)
		{
			call_lambda_command("WAIT", old->stuff, empty_string);
			new_free(&old->stuff);
		}
		if (s->end_wait_list == old)
			s->end_wait_list = NULL;
		new_free((char **)&old);
		return 1;
	}

	/* This invalid command is not a wait */
	return 0;
}

/****** FUNNY STUFF ******/
/*
 * "Funny stuff" is a vestige of ircII that had a file called "funny.c"
 * which handled LIST and NAMES, (and MODE replies).
 *
 * These represent parameters passed to the most recent LIST and NAMES
 * request.  You can do /LIST -MIN to only show channels with a certain
 * number of users, and well, i mean, nobody really uses this stuff any
 * more, but it still works, so it's still here.
 */

/*
 * These macro functions allow people to get the funny flags for
 * a specific server.  They should only be called by the appropriate
 * numeric reply in numbers.c
 */
IACCESSOR(v, funny_min)
IACCESSOR(v, funny_max)
IACCESSOR(v, funny_flags)
SACCESSOR(match, funny_match, NULL)

/*
 * set_server_funny_stuff -- Record params passed to /LIST and /NAMES
 *
 * Parameters:
 *	refnum	- The server upon which /LIST or /NAMES was run
 *	min	- Only show channels with at least <min> users
 *	max	- Only show channels with at most <max> users
 *	flags	- FUNNY_PUBLIC/FUNNY_PRIVATE/FUNNY_TOPIC
 *		  Only show public/private/channels with a topic
 *	stuff	- Only show channels that match this wildcard pattern
 *
 * Naturally, if you do multiple /LIST and/or /NAMES at the same time,
 * you'll clobber the flags from the previous run. oh rats.
 *
 * This should only be called from funny_stuff() in commands.c.
 */
void	set_server_funny_stuff (int refnum, int min, int max, int flags, const char *stuff)
{
	set_server_funny_min(refnum, min);
	set_server_funny_max(refnum, max);
	set_server_funny_flags(refnum, flags);
	set_server_funny_match(refnum, stuff);
}

/*****************************************************************************/
/***** ALTNAME STUFF *****/

/*
 * add_server_altname - Add an alternate name for a refnum
 * 
 * Parameters:
 *	refnum	- A server refnum
 *	altname	- A new alternate name for 'refnum'
 *
 * The purpose of altnames is to give you something to refer to them
 * by.  Traditionally, you've had to refer to a server either by 
 * its name, or its refnum.  But then later people asked to be able
 * to refer to a server by its group, and then eventually by any 
 * random string. 
 *
 * For any value <x>, doing add_server_altname(refnum, <x>)
 * You may then later use <x> to refer to the server:
 *	/server <x>
 *	$serverctl(GET <x> ...stuff...)
 *	/window server <x>
 *	(etc)
 * The first altname (numbered 0) is the "shortname" and is 
 * auto-populated and used as %S on the status bar.
 */
static void	add_server_altname (int refnum, char *altname)
{
	Server *s;
	char *v;

	if (!(s = get_server(refnum)))
		return;

	v = malloc_strdup(altname);
	add_to_bucket(s->altnames, v, NULL);
}

/*
 * reset_server_altnames - Replace the altnames list with something new
 *
 * Parmeters:
 *	refnum		- A server refnum
 *	new_altnames 	- A space-separated list of dwords (altnames that
 *			  contain spaces must be double-quoted) that 
 *			  will replace the server's current altnames.
 *
 * The first word will be "altname 0" and will  be used by the %S 
 * status bar expando.
 *
 * You cannot modify the altnames in place; you can either append a new
 * altname (add_server_altname() above), or replace the entire list at once.
 * The user's script usually does this with:
 *	@ y = serverctl(GET $ref ALTNAMES)
 *	[do something to $y]
 * 	@serverctl(SET $ref ALTNAMES $y)
 */
static void	reset_server_altnames (int refnum, char *new_altnames)
{
	Server *s;
	int	i;
	char *	value;

	if (!(s = get_server(refnum)))
		return;

	for (i = 0; i < s->altnames->numitems; i++)
		/* XXX Free()ing this (const char *) is ok */
		new_free((char **)(intptr_t)&s->altnames->list[i].name);	

	s->altnames->numitems = 0;

	while ((value = new_next_arg(new_altnames, &new_altnames)))
		add_server_altname(refnum, value);
}

/*
 * get_server_altnames - Return all altnames for a server
 *
 * Parameter:
 *	refnum	- A server refnum
 *
 * Return value:
 *	NULL	- "refnum" is not a valid server refnum
 *	<other>	- A space-separated list of dwords (altnames that contain 
 *		  spaces are double-quoted, so you have to call new_next_word()
 *		  to iterate the words)
 *  THE RETURN VALUE IS YOUR STRING.  YOU MUST NEW_FREE() IT.
 */
static char *	get_server_altnames (int refnum)
{
	Server *s;
	char *	retval = NULL;
	size_t	clue = 0;
	int	i;

	if (!(s = get_server(refnum)))
		return NULL;

	for (i = 0; i < s->altnames->numitems; i++)
		malloc_strcat_word_c(&retval, space, s->altnames->list[i].name, DWORD_DWORDS, &clue);

	return retval;
}

/*
 * get_server_altname: Return the 'which'th altname for 'refnum'
 *
 * Parameters:
 *	refnum	- A server refnum
 *	which	- An integer >= 0, index into the altname list
 *
 * Returns:
 *	NULL	- "refnum" is not a valid server refnum
 *		  -or- "which" is not a valid altname index for "refnum"
 *	<other>	- The "which"th altname for "refnum"
 *		  THIS IS NOT YOUR STRING.  YOU MUST NOT MODIFY IT.
 *
 * Since 'altnames' is a bucket, it could technically have NULLs in it,
 * but it seems there's no way to delete an altname; you have to complete
 * reset the entire set.  So although it's _possible_, I don't believe it
 * happens in practice.  
 *
 * Thus, you could enumerate all altnames by starting at which=0 and 
 * stopping when you get a NULL back.
 */
const char *	get_server_altname (int refnum, int which)
{
	Server	*s;

	if (!(s = get_server(refnum)))
		return NULL;

	if (which < 0 || which > s->altnames->numitems - 1)
		return NULL;

	return s->altnames->list[which].name;
}

#if 0
/*
 * which_server_altname: Check if 'name' is an altname for 'refnum'
 *
 * Parameters:
 *	refnum 	-
 *	name	-
 *
 * Returns:
 *	-2	- "refnum" is not a valid server refnum
 *	-1	- "name" is not a valid altname for "refnum"
 * 	>= 0	- An index suitable for passing to get_server_altname().
 *		  *** Note *** altnames may be changed at any time by
 *		  the user, so it's not clear how long this index would
 *		  be valid for.
 *
 * XXX This function appears to be unused.
 */
int	which_server_altname (int refnum, const char *name)
{
	Server *s;
	int	i;

	if (!(s = get_server(refnum)))
		return -2;

	for (i = 0; i < s->altnames->numitems; i++)
		if (!my_stricmp(s->altnames->list[i].name, name))
			return i;

	return -1;
}
#endif


/*
 * shortname: Convert a server "ourname" into a server "shortname".
 *
 * Parameters:
 *	oname	- The "ourname" -- what you typed to /server or put 
 *		  in a server description (eg, "irc.efnet.com")
 *
 * Return value:
 *	The Shortname for 'oname':
 *	  1. If 'oname' starts with "irc", the first segment is removed
 *	     (this catches "irc.*" and "ircserver.*" and "irc-2.*")
 *	  2. If after this it is > 60 bytes, it is truncated
 *	THE RETURN VALUE IS YOUR STRING -- YOU MUST NEW_FREE() IT.
 * 
 * The intention is the "shortname" should be a server's default 
 * zeroth altname.  The zeroth altname is what appears as %S on your
 * status bar.
 *
 * This is used by serverinfo_to_newserver(), the function that creates
 * new server refnums.
 */
static char *	shortname (const char *oname)
{
	char *name, *next, *rest;
	ssize_t	len;

	name = malloc_strdup(oname);

	/* If it's an IP address, just use it. */
	if (strtoul(name, &next, 10) && *next == '.')
		return name;

	/* If it doesn't have a dot, just use it. */
	if (!(rest = strchr(name, '.')))
		return name;

	/* If it starts with 'irc', skip that. */
	if (!strncmp(name, "irc", 3))
	{
		ov_strcpy(name, rest + 1);
		if (!(rest = strchr(name + 1, '.')))
			rest = name + strlen(name);
	}

	/* Truncate at 60 chars */
	if ((len = rest - name) > 60)
		len = 60;

	name[len] = 0;
	return name;
}


/*****************************************************************************/
/* 005 STUFF */

/*
 * make_005 - Initialize the Bucket that will hold a server's 005 settings.
 *
 * Parameter:
 *	refnum	- A server whose 005 Bucket needs initializing
 *
 * This function must be called once and only one per refnum,
 *    and that call must be from servref_to_newserv().
 * If you call it elsewhere, it will leak memory (s->a005.list).
 * If you want to clean up/reset an 005 bucket, use destroy_005(refnum)
 */
static void	make_005 (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	s->a005.list = NULL;
	s->a005.max = 0;
	s->a005.total_max = 0;
	s->a005.func = (alist_func)strncmp;
	s->a005.hash = HASH_SENSITIVE; /* One way to deal with rfc2812 */
}

/*
 * destroy_a_005 - Clean up after an unwanted 005 setting.
 *
 * Parameter:
 *	item	- An 005 item that has been previously removed from the server.
 *
 * Once you have called this function, you must not make any reference to the
 * 'item' parameter you passed in.
 */
static void	destroy_a_005 (A005_item *item)
{
	if (item) {
		new_free(&((*item).name));
		new_free(&((*item).value));
		new_free(&item);
	}
}

/*
 * destroy_005 - Remove all 005 settings for a server
 *
 * Parameters:
 *	refnum	- A server refnum
 *
 * This function releases all of the malloc()ed memory associated with 005
 * management for a server.  It should therefore be called when the server
 * gets disconnected or deleted.
 *
 * Technically, this may only be called for a server that has previously 
 * been called make_005(refnum) to set up the server's 005 Bucket.  This
 * would always be the case, but I'm just saying...
 */
static void	destroy_005 (int refnum)
{
	Server *s;
	A005_item *new_i;

	if (!(s = get_server(refnum)))
		return;

	while ((new_i = (A005_item*)array_pop((array*)(&s->a005), 0)))
		destroy_a_005(new_i);
	s->a005.max = 0;
	s->a005.total_max = 0;
	new_free(&s->a005.list);
}

/*
 * get_server_005s - return all settings passed to set_server_005()
 *
 * Parameters:
 *	refnum	- A server refnum 
 *	str	- A wildcard pattern.  Use "*" to get everything.
 *
 * Return value:
 *	A space-separated list of zero or more words containing the settings
 * 	that were previously passed to set_server_005(refnum, setting, ...);
 *	THIS IS YOUR STRING.  YOU MUST NEW_FREE() IT.
 *
 * This was previously a macro, but clang complained there was a NULL deref,
 * so I de-macrofied it so i could figure it out.
 */
static char *	get_server_005s (int refnum, const char *str)
{
	int	i;
	char *	ret = NULL;
	size_t	rclue = 0;
	Server *s;

	if (!(s = get_server(refnum)))
		return malloc_strdup(empty_string);

	for (i = 0; i < s->a005.max; i++)
	{
		if (s->a005.list[i]->name == NULL)
			continue;	/* Ignore nulls */

		if (!str || !*str || wild_match(str, s->a005.list[i]->name))
			malloc_strcat_wordlist_c(&ret, space, 
					s->a005.list[i]->name, &rclue);
	}
	return ret ? ret : malloc_strdup(empty_string);
}


/*
 * get_server_005 - Retrieve an 005 variable for a server
 *
 * Parameters:
 *	refnum	- The server that previously sent us an 005 numeric
 *	setting	- The server setting we want to get the value of
 *
 * Return Value:
 *	NULL	- "refnum" is not a valid server
 *		  -or- The server did not provide an 005 containing 'setting'
 *	<other>	- The 'value' value most recently passed to
 *			set_server_005(refnum, setting, value);
 *		  THIS IS NOT YOUR STRING.  You must not modify it.
 */
const char *	get_server_005 (int refnum, const char *setting)
{
	Server *s;
	A005_item *item;
	int cnt, loc;

	if (!(s = get_server(refnum)))
		return NULL;
	item = (A005_item*)find_array_item((array*)(&s->a005), setting, &cnt, &loc);
	if (0 > cnt)
		return ((*item).value);
	else
		return NULL;
}

/*
 * set_server_005 - Associate an 005 numeric variable with the server
 *
 * Parameters:
 *	refnum 	- The server that sent us an 005 numeric
 *	setting - The variable name (the part before =)
 *	value 	- The value (the part after =)
 *	          If "value" is NULL or the empty string, it deletes "setting".
 *
 * It is a practice in modern IRC for the server to provide the client
 * some information about its configuration so that the client can 
 * parse things correctly.  This information is provided via the 005
 * numeric reply, which usually accompanies the VERSION reply.
 *
 * Example:
 *	:server.com 005 mynick mynick CHANTYPES=&# PREFIX=(ov)@+
 * Each of these parameters is divided into a "setting" (CHANTYPES, PREFIX) and
 * a "value".  The exact interpretation of the values is set by convention, and 
 * there isn't a form RFC that describes or requires them.  The client uses 
 * these values when they are provided to make better decisions.
 *
 * XXX - As a side effect, this function acts as a gatekeeper for flags that 
 * get set.  For now, we capture the CASEMAPPING setting to determine how we 
 * should treat lower and uppercase leters (RFC1459 says that {|} and [\] 
 * are equivalent, but ASCII says they're not.)   In theory, we should be 
 * handling this someplace else, but for now, we do it here.
 *
 * XXX - It is probably a hack to call update_all_status() here, but I'm not 
 * sure if i care that badly.
 */
void	set_server_005 (int refnum, char *setting, const char *value)
{
	Server *s;
	A005_item *new_005;
	int	destroy = (!value || !*value);

	if (!(s = get_server(refnum)))
		return;

	new_005 = (A005_item*)array_lookup((array*)(&s->a005), setting, 0, destroy);

	if (destroy) {
		if (new_005 && !strcmp(setting, (*new_005).name))
			destroy_a_005(new_005);
	} else if (new_005 && !strcmp(setting, (*new_005).name)) {
		malloc_strcpy(&((*new_005).value), value);
	} else {
		new_005 = (A005_item *)new_malloc(sizeof(A005_item));
		(*new_005).name = malloc_strdup(setting);
		(*new_005).value = malloc_strdup(value);
		add_to_array((array*)(&s->a005), (array_item*)new_005);
	}

	/* XXX I hate this, i hate this, i hate this.  This is a hack XXX */
	/* We need to set up a table to handle 005 callbacks. */
	if (!my_stricmp(setting, "CASEMAPPING"))
	{
	    if (destroy)
		set_server_stricmp_table(refnum, 1);
	    else if (!my_stricmp(value, "rfc1459"))
		set_server_stricmp_table(refnum, 1);
	    else if (!my_stricmp(value, "ascii"))
		set_server_stricmp_table(refnum, 0);
	    else
		set_server_stricmp_table(refnum, 1);
	}

	update_all_status();
}

/*
 * get_all_server_groups - Return a list of all "group" fiends used by servers
 *
 * Return value:
 * 	A string containing a space separated list of all values of the "group"
 *	field in all servers, open or closed.
 *
 * THE RETURN VALUE IS YOUR STRING -- YOU MUST EVENTUALLY NEW_FREE() IT.
 * The order of the groups in the return value is unspecified and may change.
 * This implementation orders the groups by server refnum.
 */
static char *	get_all_server_groups (void)
{
	Server *s;
	int	i, j;
	char *	retval = NULL;
	size_t	clue = 0;

	for (i = 0; i < number_of_servers; i++)
	{
	    if (!get_server(i))
		continue;

	    /* 
	     * A group is added to the return value if and only if
	     * there is no lower server refnum of the same group.
	     */
	    for (j = 0; j < i; j++)
	    {
		if (!get_server(j))
			continue;
		if (!my_stricmp(get_server_group(i), get_server_group(j)))
			goto sorry_wrong_number;
	    }

	    malloc_strcat_word_c(&retval, space, get_server_group(i), 
					DWORD_DWORDS, &clue);
sorry_wrong_number:
	    ;
	}
	return retval;
}

/* Used by function_serverctl */
/*
 * $serverctl(REFNUM server-desc)
 * $serverctl(LAST_SERVER)
 * $serverctl(FROM_SERVER)
 * $serverctl(ALLGROUPS)
 * $serverctl(MAX)
 * $serverctl(READ_FILE filename)
 * $serverctl(GET 0 [LIST])
 * $serverctl(SET 0 [ITEM] [VALUE])
 * $serverctl(MATCH [pattern])
 * $serverctl(PMATCH [pattern])
 * $serverctl(GMATCH [group])
 *
 * [LIST] and [ITEM] are one of the following:
 *	NAME		"ourname" for the server connection
 * 	ITSNAME		"itsname" for the server connection
 *	PASSWORD	The password we will use on connect
 *	PORT		The port we will use on connect
 *	GROUP		The group that this server belongs to
 *	NICKNAME	The nickname we will use on connect
 *	USERHOST	What the server thinks our userhost is.
 *	AWAY		The away message
 *	VERSION		The server's claimed version
 *	UMODE		Our user mode
 *	CONNECTED	Whether or not we are connected
 *	COOKIE		Our TS/4 cookie
 *	QUIT_MESSAGE	The quit message we will use next.
 *	SSL		Whether this server is SSL-enabled or not.
 *      005             Individual PROTOCTL elements.
 *      005s            The full list of PROTOCTL elements.
 *	ALTNAME		An alternate server designation
 *			(SETting ALTNAME adds a new alternate designation) 
 *	ALTNAMES	All of the alternate server designations
 *			(SETting ALTNAMES replaces all alternate designations)
 *			(This is the only way to delete a designation)
 *	DEFAULT_REALNAME Default realname, used at next connect.
 *	REALNAME	Realname. Read-only.
 */
char 	*serverctl 	(char *input)
{
	int	refnum, num, len;
	char	*listc, *listc1;
	const char *ret;
	char *	retval = NULL;
	size_t	clue = 0;

	GET_FUNC_ARG(listc, input);
	len = strlen(listc);
	if (!my_strnicmp(listc, "ADD", len)) {
	} else if (!my_strnicmp(listc, "DELETE", len)) {
	} else if (!my_strnicmp(listc, "LAST_SERVER", len)) {
		RETURN_INT(last_server);
	} else if (!my_strnicmp(listc, "FROM_SERVER", len)) {
		RETURN_INT(from_server);
	} else if (!my_strnicmp(listc, "REFNUM", len)) {
		char *server;

		GET_FUNC_ARG(server, input);
		refnum = str_to_servref(server);
		if (refnum != NOSERV)
			RETURN_INT(refnum);
		RETURN_EMPTY;
	} else if (!my_strnicmp(listc, "UPDATE", len)) {
		int   servref;

		GET_INT_ARG(servref, input);
		refnum = update_server_from_raw_desc(servref, input);
		if (refnum != NOSERV)
			RETURN_INT(refnum);
		RETURN_EMPTY;
	} else if (!my_strnicmp(listc, "ALLGROUPS", len)) {
		retval = get_all_server_groups();
		RETURN_MSTR(retval);
	} else if (!my_strnicmp(listc, "GET", len)) {
		GET_INT_ARG(refnum, input);
		if (!get_server(refnum))
			RETURN_EMPTY;

		GET_FUNC_ARG(listc, input);
		len = strlen(listc);
		if (!my_strnicmp(listc, "AWAY", len)) {
			ret = get_server_away(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "MAXCACHESIZE", len)) {
			num = get_server_max_cached_chan_size(refnum);
			RETURN_INT(num);
		} else if (!my_strnicmp(listc, "MAXISON", len)) {
			num = get_server_ison_max(refnum);
			RETURN_INT(num);
		} else if (!my_strnicmp(listc, "MAXUSERHOST", len)) {
			num = get_server_userhost_max(refnum);
			RETURN_INT(num);
		} else if (!my_strnicmp(listc, "ISONLEN", len)) {
			num = get_server_ison_len(refnum);
			RETURN_INT(num);
		} else if (!my_strnicmp(listc, "CONNECTED", len)) {
			num = is_server_registered(refnum);
			RETURN_INT(num);
		} else if (!my_strnicmp(listc, "COOKIE", len)) {
			ret = get_server_cookie(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "GROUP", len)) {
			ret = get_server_group(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "ITSNAME", len)) {
			ret = get_server_itsname(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "NAME", len)) {
			ret = get_server_name(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "NICKNAME", len)) {
			ret = get_server_nickname(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "PASSWORD", len)) {
			ret = get_server_password(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "PORT", len)) {
			num = get_server_port(refnum);
			RETURN_INT(num);
		} else if (!my_strnicmp(listc, "PADDR", len)) {
			ret = get_server_remote_paddr(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "LOCALPORT", len)) {
			num = get_server_local_port(refnum);
			RETURN_INT(num);
		} else if (!my_strnicmp(listc, "QUIT_MESSAGE", len)) {
			if (!(ret = get_server_quit_message(refnum)))
				ret = empty_string;
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "SSL", len)) {
			ret = get_server_type(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "UMODE", len)) {
			ret = get_umode(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "UNIQUE_ID", len)) {
			ret = get_server_unique_id(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "USERHOST", len)) {
			ret = get_server_userhost(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "VERSION", len)) {
			ret = get_server_version_string(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "005", len)) {
			GET_FUNC_ARG(listc1, input);
			ret = get_server_005(refnum, listc1);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "005s", len)) {
			retval = get_server_005s(refnum, input);
			RETURN_MSTR(retval);
		} else if (!my_strnicmp(listc, "STATE", len)) {
			RETURN_STR(server_states[get_server_state(refnum)]);
		} else if (!my_strnicmp(listc, "STATUS", len)) {
			RETURN_STR(server_states[get_server_state(refnum)]);
		} else if (!my_strnicmp(listc, "ALTNAME", len)) {
			retval = get_server_altnames(refnum);
			RETURN_MSTR(retval);
		} else if (!my_strnicmp(listc, "ALTNAMES", len)) {
			retval = get_server_altnames(refnum);
			RETURN_MSTR(retval);
		} else if (!my_strnicmp(listc, "ADDRFAMILY", len)) {
			SS a;
			SA *x;

			a = get_server_remote_addr(refnum);
			x = (SA *)&a;
			if (x->sa_family == AF_INET)
				RETURN_STR("ipv4");
#ifdef INET6
			else if (x->sa_family == AF_INET6)
				RETURN_STR("ipv6");
#endif
			else if (x->sa_family == AF_UNIX)
				RETURN_STR("unix");
			else
				RETURN_STR("unknown");
		} else if (!my_strnicmp(listc, "PROTOCOL", len)) {
			RETURN_STR(get_server_type(refnum));
		} else if (!my_strnicmp(listc, "VHOST", len)) {
			RETURN_STR(get_server_vhost(refnum));
		} else if (!my_strnicmp(listc, "ADDRSLEFT", len)) {
			RETURN_INT(server_addrs_left(refnum));
		} else if (!my_strnicmp(listc, "AUTOCLOSE", len)) {
			RETURN_INT(get_server_autoclose(refnum));
		} else if (!my_strnicmp(listc, "FULLDESC", len)) {
			RETURN_STR(get_server_fulldesc(refnum));
		} else if (!my_strnicmp(listc, "REALNAME", len)) {
			RETURN_STR(get_server_realname(refnum));
		} else if (!my_strnicmp(listc, "DEFAULT_REALNAME", len)) {
			RETURN_STR(get_server_default_realname(refnum));
		} else if (!my_strnicmp(listc, "OPEN", len)) {
			RETURN_INT(is_server_open(refnum));
		} else if (!my_strnicmp(listc, "SSL_", 4)) {
			Server *s;
			int	des;

			if (!(s = get_server(refnum)) || !get_server_ssl_enabled(refnum))
				RETURN_EMPTY;

			if (!my_strnicmp(listc, "SSL_CIPHER", len)) {
				RETURN_STR(get_ssl_cipher(s->des));
			} else if (!my_strnicmp(listc, "SSL_VERIFY_RESULT", len)) {
				RETURN_EMPTY;	/* XXX :( */
			} else if (!my_strnicmp(listc, "SSL_VERIFY_ERROR", len)) {
				RETURN_INT(get_ssl_verify_error(s->des));
			} else if (!my_strnicmp(listc, "SSL_PEM", len)) {
				RETURN_STR(get_ssl_pem(s->des));
			} else if (!my_strnicmp(listc, "SSL_CERT_HASH", len)) {
				RETURN_STR(get_ssl_cert_hash(s->des));
			} else if (!my_strnicmp(listc, "SSL_PKEY_BITS", len)) {
				RETURN_INT(get_ssl_pkey_bits(s->des));
			} else if (!my_strnicmp(listc, "SSL_SUBJECT", len)) {
				RETURN_STR(get_ssl_subject(s->des));
			} else if (!my_strnicmp(listc, "SSL_SUBJECT_URL", len)) {
				RETURN_STR(get_ssl_u_cert_subject(s->des));
			} else if (!my_strnicmp(listc, "SSL_ISSUER", len)) {
				RETURN_STR(get_ssl_issuer(s->des));
			} else if (!my_strnicmp(listc, "SSL_ISSUER_URL", len)) {
				RETURN_STR(get_ssl_u_cert_issuer(s->des));
			} else if (!my_strnicmp(listc, "SSL_VERSION", len)) {
				RETURN_STR(get_ssl_ssl_version(s->des));
			} else if (!my_strnicmp(listc, "SSL_CHECKHOST_RESULT", len)) {
				RETURN_EMPTY;	/* XXX :( */
			} else if (!my_strnicmp(listc, "SSL_CHECKHOST_ERROR", len)) {
				RETURN_INT(get_ssl_checkhost_error(s->des));
			} else if (!my_strnicmp(listc, "SSL_SELF_SIGNED", len)) {
				RETURN_EMPTY;	/* XXX :( */
			} else if (!my_strnicmp(listc, "SSL_SELF_SIGNED_ERROR", len)) {
				RETURN_INT(get_ssl_self_signed_error(s->des));
			} else if (!my_strnicmp(listc, "SSL_OTHER_ERROR", len)) {
				RETURN_INT(get_ssl_other_error(s->des));
			} else if (!my_strnicmp(listc, "SSL_MOST_SERIOUS_ERROR", len)) {
				RETURN_INT(get_ssl_most_serious_error(s->des));
			} else if (!my_strnicmp(listc, "SSL_SANS", len)) {
				RETURN_STR(get_ssl_sans(s->des));
			} else if (!my_strnicmp(listc, "SSL_ACCEPT_CERT", len)) {
				RETURN_INT(get_server_accept_cert(refnum));
			}
		}
	} else if (!my_strnicmp(listc, "SET", len)) {
		GET_INT_ARG(refnum, input);
		if (!get_server(refnum))
			RETURN_EMPTY;

		GET_FUNC_ARG(listc, input);
		len = strlen(listc);
		if (!my_strnicmp(listc, "AWAY", len)) {
			set_server_away(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "MAXCACHESIZE", len)) {
			int	size;
			GET_INT_ARG(size, input);
			set_server_max_cached_chan_size(refnum, size);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "MAXISON", len)) {
			int	size;
			GET_INT_ARG(size, input);
			set_server_ison_max(refnum, size);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "MAXUSERHOST", len)) {
			int	size;
			GET_INT_ARG(size, input);
			set_server_userhost_max(refnum, size);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "ISONLEN", len)) {
			int	size;
			GET_INT_ARG(size, input);
			set_server_ison_len(refnum, size);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "CONNECTED", len)) {
			RETURN_EMPTY;		/* Read only. */
		} else if (!my_strnicmp(listc, "COOKIE", len)) {
			set_server_cookie(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "GROUP", len)) {
			set_server_group(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "ITSNAME", len)) {
			set_server_itsname(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "NAME", len)) {
			set_server_name(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "NICKNAME", len)) {
			change_server_nickname(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "PASSWORD", len)) {
			set_server_password(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "PORT", len)) {
			int port;

			GET_INT_ARG(port, input);
			set_server_port(refnum, port);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "PRIMARY", len)) {
			primary_server = refnum;
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "QUIT_MESSAGE", len)) {
			set_server_quit_message(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "SSL", len)) {
			set_server_server_type(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "UMODE", len)) {
			if (is_server_open(refnum) == 0) {
				clear_user_modes(refnum);
				update_user_mode(refnum, input);
				RETURN_INT(1);
			}
			RETURN_EMPTY;		/* Read only for now */
		} else if (!my_strnicmp(listc, "UNIQUE_ID", len)) {
			set_server_unique_id(refnum, input);
		} else if (!my_strnicmp(listc, "USERHOST", len)) {
			set_server_userhost(refnum, input);
		} else if (!my_strnicmp(listc, "VERSION", len)) {
			set_server_version_string(refnum, input);
		} else if (!my_strnicmp(listc, "VHOST", len)) {
			set_server_vhost(refnum, input);
		} else if (!my_strnicmp(listc, "005", len)) {
			GET_FUNC_ARG(listc1, input);
			set_server_005(refnum, listc1, input);
			RETURN_INT(!!*input);
		} else if (!my_strnicmp(listc, "ALTNAME", len)) {
			add_server_altname(refnum, input);
		} else if (!my_strnicmp(listc, "ALTNAMES", len)) {
			reset_server_altnames(refnum, input);
		} else if (!my_strnicmp(listc, "AUTOCLOSE", len)) {
			int newval;

			GET_INT_ARG(newval, input);
			set_server_autoclose(refnum, newval);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "REALNAME", len)) {
			set_server_default_realname(refnum, input);
		} else if (!my_strnicmp(listc, "DEFAULT_REALNAME", len)) {
			set_server_default_realname(refnum, input);
		} else if (!my_strnicmp(listc, "SSL_", 4)) {
			Server *s;
			int	des;

			if (!get_server(refnum) || !get_server_ssl_enabled(refnum))
				RETURN_EMPTY;

			if (!my_strnicmp(listc, "SSL_ACCEPT_CERT", len)) {
				int	val = 0;
				val = my_atol(input);
				set_server_accept_cert(refnum, val);
			}
			RETURN_INT(1);
		}
	} else if (!my_strnicmp(listc, "OMATCH", len)) {
		int	i;

		for (i = 0; i < number_of_servers; i++)
			if (get_server(i) && wild_match(input, get_server_name(i)))
				malloc_strcat_wordlist_c(&retval, space, ltoa(i), &clue);
		RETURN_MSTR(retval);
	} else if (!my_strnicmp(listc, "IMATCH", len)) {
		int	i;

		for (i = 0; i < number_of_servers; i++)
			if (get_server(i) && wild_match(input, get_server_itsname(i)))
				malloc_strcat_wordlist_c(&retval, space, ltoa(i), &clue);
		RETURN_MSTR(retval);
	} else if (!my_strnicmp(listc, "GMATCH", len)) {
		int	i;

		for (i = 0; i < number_of_servers; i++)
			if (get_server(i) && wild_match(input, get_server_group(i)))
				malloc_strcat_wordlist_c(&retval, space, ltoa(i), &clue);
		RETURN_MSTR(retval);
	} else if (!my_strnicmp(listc, "MAX", len)) {
		RETURN_INT(number_of_servers);
	} else if (!my_strnicmp(listc, "READ_FILE", len)) {
		read_server_file(input);
	} else
		RETURN_EMPTY;

	RETURN_EMPTY;
}

/*
 * got_my_userhost: A callback function to userhostbase()
 *
 * Parameters:
 *	refnum	- A server refnum
 *	item	- The result of a USERHOST request
 *	nick	- The value passed to 'args' (for here, it is NULL)
 *	stuff	- The value passed to 'subargs' (for here, it is NULL)
 *
 * When you connect to a server, we ask do a USERHOST for ourselves, so we
 * know what our public IP address is.  This is needed for 
 * for /SET DCC_USE_GATEWAY_ADDR ON, CTCP FINGER, the $X expando, and 
 * (in the future) for determining how long protocol messages can be.
 * 
 * XXX I suppose this doesn't belong here.  But where else shall it go?
 */
static void 	got_my_userhost (int refnum, UserhostItem *item, const char *nick, const char *stuff)
{
	char *freeme;

	freeme = malloc_strdup3(item->user, "@", item->host);
	set_server_userhost(refnum, freeme);
	new_free(&freeme);
}


/*
 * server_more_addrs: Are there IPs for this server we haven't tried yet?
 *
 * Parameters:
 *	refnum	- A server refnum
 *
 * Return value:
 *	0	- "refnum" is not a valid server refnum
 *		  -or- There are no untried IPs for "refnum"
 *	1	- There are untried IPs for "refnum"
 *
 * This is used by window_check_servers() which is a failsafe function.
 * If you're trying to connect to a server and a connection gets 
 * "stuck" or you lose patience, you might do a /reconnect or /disconnect.
 * This results in a window connected to a server that is CLOSED; however
 * window_check_servers() will call here to find out if there are other
 * IP addresses that can be tried, and if so, it resets the server to 
 * READY.  This is what allows /reconnect to do the right thing if you 
 * get hung up during a connection to a firewalled IP address.
 */
int	server_more_addrs (int refnum)
{
	Server	*s;

	if (!(s = get_server(refnum)))
		return 0;

	if (s->next_addr)
		return 1;
	else
		return 0;
}

/*
 * server_addrs_left: How many DNS entries do we have left to try?
 *
 * Parameters:
 *	refnum	- A server refnum
 *
 * Return value:
 *	0	- "refnum" is not a valid server refnum
 *	>0	- The number of dns entries still untried.
 *
 * When you connect to a server, the client will do a dns lookup
 * on the "ourname", which will return 0 or more IP addresses.
 * Each of them are tried in sequence until one of them results 
 * in a server that accepts us.  During the period of time between
 * our DNS lookup and our receiving a 001 numeric, we keep the 
 * addresses around.  This will tell you how many are left to go
 * before we give up.
 *
 * This is used by $serverctl(GET x ADDRSLEFT).
 */
static int	server_addrs_left (int refnum)
{
	Server *s;
	const AI *ai;
	int	count = 0;

	if (!(s = get_server(refnum)))
		return 0;

	for (ai = s->next_addr; ai; ai = ai->ai_next)
		count++;

	return count;
}

#if 0
/*
 * This calculates how long a privmsg/notice can be on 'server' that we send
 * to 'target'.
 *
 * Each privmsg/notice looks like this:
 *      1        2             345678901        23         4 5
 *	:<mynick>!<myuser@host> PRIVMSG <target> :<message>\r\n
 * so each line has 15 bytes of overhead
 *   + length of my nick
 *   + length of my user@host
 *   + length of channel/target (including #)
 * The maximum length of a message is 512 - all of the above
 */
size_t	get_server_message_limit (int server, const char *target)
{
	size_t	overhead;

	overhead = 15;
	overhead += strlen(get_server_nickname(server));
	overhead += strlen(get_server_userhost(server));
	overhead += strlen(target);
	return 512 - overhead;
}
#endif


Server *      get_server (int server)
{
        if (server == -1 && from_server >= 0)
                server = from_server;
        if (server < 0 || server >= number_of_servers)
                return NULL;
        return server_list[server];
}

