/* $EPIC: server.c,v 1.89 2003/01/31 23:50:18 jnelson Exp $ */
/*
 * server.c:  Things dealing with that wacky program we call ircd.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1993, 2002 EPIC Software Labs.
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
#include "translat.h"

/*
 * Note to future maintainers -- we do a bit of chicanery here.  The 'flags'
 * member in the server structure is assuemd to be at least N bits wide,
 * where N is the number of possible user modes.  Since the server stores
 * our user modes in a similar fashion, this shouldnt ever be "broken",
 * and if it is, we'll just change to do it however the server does.
 * The easiest way to handle that would be just to use an fd_set.
 *
 * 'umodes' here is a bogus default.  When we recieve the 004 numeric, it
 * overrides 'umodes'.
 *
 * The 'o' user mode is a special case, in that we want to know when its on.
 * This is mirrored in the "operator" member, for historical reasons.  This is
 * a kludge and should be changed.
 */
const 	char *	umodes = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
static	char *	do_umode (int du_index);
	void 	reset_nickname (int);
	void	clear_reconnect_counts (void);
	const char *get_server_group (int refnum);
	const char *get_server_type (int refnum);

	int	connected_to_server = 0;	/* true when connection is
						 * confirmed */
	int	reconnects_to_hint = 0;		/* XXX Hack.  Don't remind me */

static	char    lame_wait_nick[] = "***LW***";
static	char    wait_nick[] = "***W***";

/************************ SERVERLIST STUFF ***************************/

	Server **server_list = (Server **) 0;
	int	number_of_servers = 0;

	int	primary_server = NOSERV;
	int	from_server = NOSERV;
	int	parsing_server_index = NOSERV;
	int	last_server = NOSERV;


/*
 * add_to_server_list: adds the given server to the server_list.  If the
 * server is already in the server list it is not re-added... however, if the
 * overwrite flag is true, the port and passwords are updated to the values
 * passes.  If the server is not on the list, it is added to the end. In
 * either case, the server is made the current server. 
 */
void 	add_to_server_list (const char *server, int port, const char *password, const char *nick, const char *group, const char *server_type, int overwrite)
{
	Server *s;
	int	i;

	if ((from_server = find_in_server_list(server, port)) == NOSERV)
	{
		for (i = 0; i < number_of_servers; i++)
			if (server_list[i] == NULL)
				break;

		if (i == number_of_servers)
		{
			from_server = number_of_servers++;
			RESIZE(server_list, Server *, number_of_servers);
		}
		else
			from_server = i;

		s = server_list[from_server] = new_malloc(sizeof(Server));
		s->name = m_strdup(server);
		s->itsname = (char *) 0;
		s->password = (char *) 0;
		s->group = NULL;
		s->away = (char *) 0;
		s->version_string = (char *) 0;
		s->server2_8 = 0;
		s->operator = 0;
		s->des = -1;
		s->version = 0;
		s->flags = 0;
		s->flags2 = 0;
		s->nickname = (char *) 0;
		s->s_nickname = (char *) 0;
		s->d_nickname = (char *) 0;
		s->userhost = (char *) 0;
		s->registered = 0;
		s->eof = 0;
		s->port = port;
		s->who_queue = NULL;
		s->ison_queue = NULL;
		s->userhost_queue = NULL;
		memset(&s->uh_addr, 0, sizeof(s->uh_addr));
		memset(&s->local_sockname, 0, sizeof(s->local_sockname));
		memset(&s->remote_sockname, 0, sizeof(s->remote_sockname));
		s->umodes = NULL;
		s->redirect = NULL;
		s->cookie = NULL;
		s->closing = 0;
		s->nickname_pending = 0;
		s->fudge_factor = 0;
		s->registration_pending = 0;
		s->resetting_nickname = 0;
		s->reconnects = 0;
		s->quit_message = NULL;
		s->save_channels = -1;

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

		s->ssl_enabled = FALSE;
		s->ssl_fd = NULL;

		if (password && *password)
			malloc_strcpy(&s->password, password);
		if (nick && *nick)
			malloc_strcpy(&s->d_nickname, nick);
		else if (!s->d_nickname)
			malloc_strcpy(&s->d_nickname, nickname);
		if (group && *group)
			malloc_strcpy(&s->group, group);
		if (server_type && *server_type)
		{
		    if (my_stricmp(server_type, "IRC-SSL") == 0)
			set_server_try_ssl(from_server, TRUE);
		    else
			set_server_try_ssl(from_server, FALSE);
		}
		malloc_strcpy(&s->umodes, umodes);

		make_notify_list(from_server);
		do_umode(from_server);
		make_005(from_server);
	}
	else
	{
		s = server_list[from_server];

		if (overwrite)
		{
			s->port = port;
			if (password || !s->password)
			{
				if (password && *password)
					malloc_strcpy(&s->password, password);
				else
					new_free(&s->password);
			}
			if (nick || !s->d_nickname)
			{
				if (nick && *nick)
					malloc_strcpy(&s->d_nickname, nick);
				else
					new_free(&s->d_nickname);
			}
		}

		if (strlen(server) > strlen(s->name))
			malloc_strcpy(&s->name, server);
	}
}

static 	void 	remove_from_server_list (int i)
{
	Server  *s;

	if (!(s = get_server(i)))
		return;

	if (number_of_servers == 1)
	{
		say("You can't delete the last server!");
		return;
	}

	say("Deleting server [%d]", i);
	clean_server_queues(i);
	new_free(&s->name);
	new_free(&s->itsname);
	new_free(&s->password);
	new_free(&s->group);
	new_free(&s->away);
	new_free(&s->version_string);
	new_free(&s->nickname);
	new_free(&s->s_nickname);
	new_free(&s->d_nickname);
	new_free(&s->userhost);
	new_free(&s->cookie);
	new_free(&s->umodes);
	new_free(&s->ison_queue);		/* XXX Aren't these free? */
	new_free(&s->who_queue);
	new_free(&s->invite_channel);
	new_free(&s->last_notify_nick);
	new_free(&s->joined_nick);
	new_free(&s->public_nick);
	new_free(&s->recv_nick);
	new_free(&s->sent_nick);
	new_free(&s->sent_body);
	destroy_notify_list(i);
	destroy_005(i);

	if (get_server_ssl_enabled(i) == TRUE)
	{
#ifndef HAVE_SSL
		panic("Deleting server %d which claims to be using SSL on"
			"a non-ssl client", i);
#else
		SSL_free((SSL *)s->ssl_fd);
		SSL_CTX_free((SSL_CTX *)s->ctx);
#endif
	}
	new_free(&server_list[i]);
	s = NULL;
}



/*
 * Given a hostname and a port number, this function returns the server_list
 * index for that server.  If the specified server is not in the server_list,
 * then NOSERV is returned.  This function makes an attempt to do smart name
 * completion so that if you ask for "irc", it returns the first server that
 * has the leading prefix of "irc" in it.
 */
int	find_in_server_list (const char *server, int port)
{
	Server *s;
	int	i;
	int	first_idx = NOSERV;

	for (i = 0; i < number_of_servers; i++)
	{
		if (!(s = get_server(i)))
			continue;

		/* Check the port first.  This is a cheap, easy check */
		if (port && s->port && port != s->port && port != -1)
			continue;

#define MATCH_WITH_COMPLETION(n1, n2) 				\
{								\
	/* Length of the user's input */			\
	size_t l1 = strlen(n1);					\
								\
	/* Length of the server's real name */			\
	size_t l2 = strlen(n2);					\
								\
	/* 							\
	 * Compare what the user wants to what this server 	\
	 * name is.  If the server's name is shorter than what	\
	 * the user specified, then don't bother doing the	\
	 * compare.  If the two strings match exactly, then 	\
	 * this is the server we're looking for.		\
	 */							\
	if (l2 >= l1 && !my_strnicmp(n1, n2, l1))		\
	{							\
		if (l2 == l1)					\
			return i;				\
		if (first_idx == NOSERV)			\
			first_idx = i;				\
	}							\
}

		MATCH_WITH_COMPLETION(server, s->name)
		if (!s->itsname)
			continue;
		MATCH_WITH_COMPLETION(server, s->itsname)
	}

	return first_idx;	/* NOSERV if server was not found. */
}

/*
 * Given a string that (in all likelihood) contains a server description
 * of the form:
 *
 *		refnum	 (where refnum is an integer for an existing server)
 * or
 *		hostname:port:password:nickname:group:type
 * or
 *		hostname port password nickname group type
 *
 * This extracts the salient information from the string and returns the
 * server_list index for that server.  If the information describes a server
 * that is not in the server_list, that information is *automatically added*
 * to the server_list and a new index is returned.  Double-quoted words are
 * supported as long as you use them reasonably.  Double-quoted words protect
 * any colons on the inside from inadvertant mis-parsing.
 *
 * This function always succeeds.
 */
int 	find_server_refnum (char *server, char **rest)
{
	int 	refnum;
	int	port = irc_port;
	char 	*cport = NULL, 
		*password = NULL,
		*nick = NULL,
		*group = NULL,
		*server_type = NULL;

	/*
	 * First of all, check for an existing server refnum
	 */
	if ((refnum = parse_server_index(server, 0)) != NOSERV)
		return refnum;

	/*
	 * Next check to see if its a "server:port:password:nick"
	 */
	else if (strchr(server, ':'))
		parse_server_info(&server, &cport, &password, &nick, &group, &server_type);

	/*
	 * Next check to see if its "server port password nick"
	 */
	else if (rest && *rest)
	{
		cport = new_next_arg(*rest, rest);
		password = new_next_arg(*rest, rest);
		nick = new_next_arg(*rest, rest);
		group = new_next_arg(*rest, rest);
		server_type = new_next_arg(*rest, rest);
	}

	if (cport && *cport)
		port = my_atol(cport);

	/*
	 * Add to the server list (this will update the port
	 * and password fields).
	 */
	add_to_server_list(server, port, password, nick, group, server_type, 1);
	return from_server;
}


/*
 * Parse_server_index:  "Canonicalize" a string that should contain a server
 * refnum into a server refnum integer that represents said refnum.  
 *
 * So if 'str' contains a valid server refnum, return that refnum integer
 * Otherwise, 'str' is invalid, and return NOSERV, the invalid server refnum.
 *
 * However, if 'wantprim' is 1 and 'str' is empty, then return -1, which 
 * always refers to the from server.  'Wantprim' should be 0 except for
 * a few special cases.
 *
 * You should always and only use this function to convert a string into 
 * a server refnum.  Fail to do so at your own peril.
 */
int	parse_server_index (const char *str, int wantprim)
{
	int	i;
	char	*after = NULL;

	if (wantprim && (str == NULL || *str == 0))
		return -1;

	if (str && is_number(str))
	{
		i = strtol(str, &after, 10);
		if (after && *after)
			return NOSERV;		/* Not a number, sorry. */
		if (get_server(i))
			return i;
	}
	return NOSERV;
}




/*
 * parse_server_info:  This parses a single string of the form
 * "server:portnum:password:nickname".  It the points port to the portnum
 * portion and password to the password portion.  This chews up the original
 * string, so * upon return, name will only point the the name.  If portnum
 * or password are missing or empty,  their respective returned value will
 * point to null. 
 *
 * "*group" must be set to something (even if it is NULL) before calling this!
 * Colons can be backslashed.
 */
void	parse_server_info (char **host, char **port, char **password, char **nick, char **group, char **server_type)
{
	char *ptr;
	char *name = *host;

	*host = *port = *password = *nick = *server_type = NULL;

	do
	{
		ptr = name;
		if (*ptr == '"')
			*host = new_next_arg(ptr, &ptr);
		else if (*ptr == '[')
		{
		    *host = ptr + 1;
		    if ((ptr = MatchingBracket(ptr + 1, '[', ']')))
			*ptr++ = 0;
		    else
			break;
		}
		else
		    *host = ptr;

		ptr = strchr(ptr, ':');
		if (!ptr)
			break;
		*ptr++ = 0;
		if (!*ptr)
			break;
		*port = ptr;

		if (*ptr == '"')
			*port = new_next_arg(ptr, &ptr);
		ptr = strchr(ptr, ':');
		if (!ptr)
			break;
		*ptr++ = 0;
		if (!*ptr)
			break;
		*password = ptr;

		if (*ptr == '"')
			*password = new_next_arg(ptr, &ptr);
		ptr = strchr(ptr, ':');
		if (!ptr)
			break;
		*ptr++ = 0;
		if (!*ptr)
			break;
		*nick = ptr;

		if (*ptr == '"')
			*nick = new_next_arg(ptr, &ptr);
		ptr = strchr(ptr, ':');
		if (!ptr)
			break;
		*ptr++ = 0;
		if (!*ptr)
			break;
		*group = ptr;

		if (*ptr == '"')
			*group = new_next_arg(ptr, &ptr);
		ptr = strchr(ptr, ':');
		if (!ptr)
			break;
		*ptr++ = 0;
		if (!*ptr)
			break;
		*server_type = ptr;

		if (*ptr == '"')
			*server_type = new_next_arg(ptr, &ptr);
		ptr = strchr(ptr, ':');
		if (!ptr)
			break;
		else
			*ptr++ = 0;
	}
	while (0);
}

/*
 * build_server_list: given a whitespace separated list of server names this
 * builds a list of those servers using add_to_server_list().  Since
 * add_to_server_list() is used to added each server specification, this can
 * be called many many times to add more servers to the server list.  Each
 * element in the server list case have one of the following forms: 
 *
 * servername 
 *
 * servername:port 
 *
 * servername:port:password 
 *
 * servername::password 
 *
 * servername::password:servergroup
 *
 *
 * Note also that this routine mucks around with the server string passed to it,
 * so make sure this is ok 
 */
void	build_server_list (char *servers, char *group)
{
	char	*host,
		*rest,
		*password = (char *) 0,
		*port = (char *) 0,
		*nick = (char *) 0,
		*server_type = (char *) 0;
	int	port_num;

	if (!servers)
		return;

	while (servers)
	{
		if ((rest = strchr(servers, '\n')))
			*rest++ = 0;

		while ((host = next_arg(servers, &servers)))
		{
			parse_server_info(&host, &port, &password, &nick, &group, &server_type);
                        if (port && *port && (port_num = my_atol(port)))
				;
			else
				port_num = irc_port;

			add_to_server_list(host, port_num, password, nick, group, server_type, 0);
		}
		servers = rest;
	}
}

/*
 * read_server_file: reads hostname:portnum:password server information from
 * a file and adds this stuff to the server list.  See build_server_list()/ 
 */
int 	read_server_file (void)
{
	FILE 	*fp;
	char	file_path[MAXPATHLEN + 1];
	char	buffer[BIG_BUFFER_SIZE + 1];
	Filename expanded;
	char	*defaultgroup = NULL;

	if (getenv("IRC_SERVERS_FILE"))
		strmcpy(file_path, getenv("IRC_SERVERS_FILE"), MAXPATHLEN);
	else
	{
#ifdef SERVERS_FILE
		*file_path = 0;
		if (SERVERS_FILE[0] != '/' && SERVERS_FILE[0] != '~')
			strmcpy(file_path, irc_lib, MAXPATHLEN);
		strmcat(file_path, SERVERS_FILE, MAXPATHLEN);
#else
		return -1;
#endif
	}

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
			build_server_list(buffer, defaultgroup);
	}

	fclose(fp);
	new_free(&defaultgroup);
	return 0;
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
		say("Current server: %s %d", s->name, s->port);
	else
		say("Current server: <None>");

	if (primary_server != NOSERV && (s = get_server(primary_server)))
		say("Primary server: %s %d", s->name, s->port);
	else
		say("Primary server: <None>");

	say("Server list:");
	for (i = 0; i < number_of_servers; i++)
	{
		if (!(s = get_server(i)))
			continue;

		if (!s->nickname)
			say("\t%d) %s %d [%s] %s", i, s->name, s->port, 
				get_server_group(i), get_server_type(i));
		else if (is_server_open(i))
			say("\t%d) %s %d (%s) [%s] %s", i, s->name, s->port,
				s->nickname, get_server_group(i),
				get_server_type(i));
		else
			say("\t%d) %s %d (was %s) [%s] %s", i, s->name, 
				s->port, s->nickname, get_server_group(i),
				get_server_type(i));
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
		    m_sc3cat(&buffer, space, get_server_itsname(i), &bufclue);
	}

	return buffer ? buffer : m_strdup(empty_string);
}

/* server_list_size: returns the number of servers in the server list */
int 	server_list_size (void)
{
	return (number_of_servers);
}



/*************************** SERVER STUFF *************************/
/*
 * server: the /SERVER command. Read the SERVER help page about 
 */
BUILT_IN_COMMAND(servercmd)
{
	char	*server = NULL;
	int	i;

	if ((server = next_arg(args, &args)) == NULL)
	{
		display_server_list();
		return;
	}

	/*
	 * Delete an existing server
	 */
	if (strlen(server) > 1 && 
		!my_strnicmp(server, "-DELETE", strlen(server)))
	{
		if ((server = next_arg(args, &args)) == NULL)
		{
			say("Need server number for -DELETE");
			return;
		}

		if ((i = parse_server_index(server, 0)) == NOSERV &&
		    (i = find_in_server_list(server, 0)) == NOSERV)
		{
			say("No such server in list");
			return;
		}

		if (is_server_open(i))
		{
			say("Can not delete server that is open");
			return;
		}

		remove_from_server_list(i);
	}

	/*
	 * Add a server, but dont connect
	 */
	else if (strlen(server) > 1 && 
			!my_strnicmp(server, "-ADD", strlen(server)))
	{
		if (!(server = new_next_arg(args, &args)))
		{
			say("Need server info for -ADD");
			return;
		}

		find_server_refnum(server, &args);
	}

	/*
	 * The difference between /server +foo.bar.com  and
	 * /window server foo.bar.com is now moot.
	 */
	else if (*server == '+')
	{
		clear_reconnect_counts();

		/* /SERVER +foo.bar.com is an alias for /window server */
		if (*++server)
			window_server(current_window, &server);

		/* /SERVER + means go to the next server */
		else
		{
			set_server_quit_message(from_server, 
					"Changing servers");
			server_reconnects_to(from_server, from_server + 1);
			reconnect(from_server, 1);
		}

		window_check_servers();
	}

	/*
	 * You can only detach a server using its refnum here.
	 */
	else if (*server == '-')
	{
		clear_reconnect_counts();

		if (*++server)
		{
			i = find_server_refnum(server, &args);
			if (i == primary_server)
			{
			    say("You can't close your primary server!");
			    return;
			}

			set_server_quit_message(from_server, 
					"Disconnected at user request");
			server_reconnects_to(i, NOSERV);
			reconnect(i, 1);
		}
		else
		{
			set_server_quit_message(from_server, 
					"Changing servers");
			if (from_server == 0)
				server_reconnects_to(from_server, 
							number_of_servers - 1);
			else
				server_reconnects_to(from_server, 
							from_server - 1);
			reconnect(from_server, 1);
		}

		window_check_servers();
	}

	/*
	 * Just a naked /server with no flags
	 */
	else
	{
		int	j = from_server;

		i = find_server_refnum(server, &args);
		if (i != j)
		{
			clear_reconnect_counts();
			if (my_stricmp(get_server_type(i), "IRC-SSL") == 0)
				set_server_ssl_enabled(i, TRUE);

			server_reconnects_to(j, i);
			reconnect(j, 0);
			window_check_servers();
		}
		else
			say("Connected to port %d of server %s",
				get_server_port(j), get_server_name(j));
	}
}


/* SERVER INPUT STUFF */
/*
 * do_server: check the given fd_set against the currently open servers in
 * the server list.  If one have information available to be read, it is read
 * and and parsed appropriately.  If an EOF is detected from an open server,
 * we call reconnect() to try to keep that server connection alive.
 */
void	do_server (fd_set *rd, fd_set *wd)
{
	Server *s;
	char	buffer[IO_BUFFER_SIZE + 1];
	int	des,
		i;

	for (i = 0; i < number_of_servers; i++)
	{
		int	junk;
		char 	*bufptr = buffer;

		if (!(s = get_server(i)))
			continue;

		des = s->des;
		if (des == -1 || !FD_ISSET(des, rd))
		{
			if (get_server_ssl_enabled(i) == TRUE)
			{
#ifndef HAVE_SSL
				panic("do_server on server %d claims to be"
					"using SSL on non-ssl client", i);
#else
				if (!SSL_pending((SSL *)s->ssl_fd))
					continue;
#endif
			}
			else
				continue;
		}
		FD_CLR(des, rd);	/* Make sure it never comes up again */

		last_server = from_server = i;
		junk = dgets(bufptr, des, 1, s->ssl_fd);

		switch (junk)
 		{
			case 0:		/* Sit on incomplete lines */
				break;

			case -1:	/* EOF or other error */
			{
				if (s->save_channels == -1)
					set_server_save_channels(i, 1);

				server_is_registered(i, 0);
				close_server(i, NULL);
				say("Connection closed from %s: %s", 
					s->name,
					(dgets_errno == -1) ? 
					     "Remote end closed connection" : 
					     strerror(dgets_errno));
				reconnect(i, 1);
				i++;		/* NEVER DELETE THIS! */
				break;
			}

			default:	/* New inbound data */
			{
				char *end;

				end = strlen(buffer) + buffer;
				if (*--end == '\n')
					*end-- = '\0';
				if (*end == '\r')
					*end-- = '\0';

				if (x_debug & DEBUG_INBOUND)
					yell("[%d] <- [%s]", 
						s->des, buffer);

				if (translation)
					translate_from_server(buffer);
				parsing_server_index = i;
				parse_server(buffer);
				parsing_server_index = NOSERV;
				message_from(NULL, LOG_CRAP);
				break;
			}
		}
		from_server = primary_server;
	}

	/* Make sure primary_server is legit before we leave */
	if (primary_server == NOSERV || !is_server_registered(primary_server))
		window_check_servers();
}


/* SERVER OUTPUT STUFF */
static void 	vsend_to_aserver (int, const char *format, va_list args);
void		send_to_aserver_raw (int, size_t len, const char *buffer);

void	send_to_aserver (int refnum, const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vsend_to_aserver(refnum, format, args);
	va_end(args);
}

void	send_to_server (const char *format, ...)
{
	va_list args;
	int	server;

	if ((server = from_server) == NOSERV)
		server = primary_server;

	va_start(args, format);
	vsend_to_aserver(server, format, args);
	va_end(args);
}

/* send_to_server: sends the given info the the server */
static void 	vsend_to_aserver (int refnum, const char *format, va_list args)
{
	Server *s;
	char	buffer[BIG_BUFFER_SIZE * 11 + 1]; /* make this buffer *much*
						  * bigger than needed */
	size_t	size = BIG_BUFFER_SIZE * 11;
	int	len,
		des;
	int	ofs;

	if (!(s = get_server(refnum)))
		return;

	if (refnum != NOSERV && (des = s->des) != -1 && format)
	{
		/* Keep the results short, and within reason. */
		len = vsnprintf(buffer, BIG_BUFFER_SIZE, format, args);

		if (translation)
			translate_to_server(buffer);

		if (outbound_line_mangler)
		{
			if (mangle_line(buffer, outbound_line_mangler, size) 
					> size)
				yell("mangle_line truncated results!  Ick.");
		}

		s->sent = 1;
		if (len > (IRCD_BUFFER_SIZE - 2) || len == -1)
			buffer[IRCD_BUFFER_SIZE - 2] = 0;
		if (x_debug & DEBUG_OUTBOUND)
			yell("[%d] -> [%s]", des, buffer);
		strmcat(buffer, "\r\n", IRCD_BUFFER_SIZE);

		/* This "from_server" hack is for the benefit of do_hook. */
		ofs = from_server;
		from_server = refnum;
		if (do_hook(SEND_TO_SERVER_LIST, "%d %d %s", from_server, des, buffer))
			send_to_aserver_raw(refnum, strlen(buffer), buffer);
		from_server = ofs;
	}
	else if (from_server == NOSERV)
        {
	    if (do_hook(DISCONNECT_LIST,"No Connection to %d", refnum))
		say("You are not connected to a server, "
			"use /SERVER to connect.");
        }
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
	    if (get_server_ssl_enabled(refnum) == TRUE)
	    {
#ifndef HAVE_SSL
		panic("send_to_aserver_raw: Server %d claims to "
			"be using SSL on a non-ssl client", refnum);
#else
		if (s->ssl_fd == NULL)
		{
			say("SSL write error - ssl socket = 0");
			return;
		}
		err = SSL_write((SSL *)s->ssl_fd, buffer, strlen(buffer));
		BIO_flush(SSL_get_wbio((SSL *)s->ssl_fd));
#endif
	    }
	    else
		err = write(des, buffer, strlen(buffer));

	    if (err == -1 && (!get_int_var(NO_FAIL_DISCONNECT_VAR)))
	    {
		if (is_server_registered(refnum))
		{
			/* s->save_channels == 1; */
			/* close_server(server, strerror(errno));  */
			server_is_registered(des, 0);
			say("Write to server failed.  Closing connection.");
			if (get_server_ssl_enabled(refnum) == TRUE)
#ifndef HAVE_SSL
			    panic("send_to_aserver_raw: Closing server %d "
				  "which claims to be using SSL on non-ssl "
				  "client", refnum);
#else
				SSL_shutdown((SSL *)s->ssl_fd);
#endif
			reconnect(refnum, 1);
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
 * This establishes a new connection to 'new_server'.  This function does
 * not worry about why or where it is doing this.  It is only concerned
 * with getting a connection up and running.
 *
 * NOTICE! THIS MUST ONLY EVER BE CALLED BY connect_to_new_server()!
 * IF YOU CALL THIS ELSEWHERE, THINGS WILL BREAK AND ITS NOT MY FAULT!
 */
static int 	connect_to_server (int new_server)
{
	int 		des;
	int		len;
	Server *	s;

	/*
	 * Can't connect to refnum -1, this is definitely an error.
	 */
	if (!(s = get_server(new_server)))
	{
		say("Connecting to refnum %d.  That makes no sense.", 
			new_server);
		return -1;		/* XXXX */
	}

	/*
	 * If we are already connected to the new server, go with that.
	 */
	if (s->des != -1)
	{
		say("Connected to port %d of server %s", s->port, s->name);
		from_server = new_server;
		return -3;		/* Server is already connected */
	}

	/*
	 * Make an attempt to connect to the new server.
	 */
	say("Connecting to port %d of server %s [refnum %d]", 
			s->port, s->name, new_server);

	s->closing = 0;
	oper_command = 0;
	errno = 0;
	memset(&s->local_sockname, 0, sizeof(s->local_sockname));
	memset(&s->remote_sockname, 0, sizeof(s->remote_sockname));

	if ((des = connectory(AF_UNSPEC, s->name, ltoa(s->port))) < 0)
	{
		if (x_debug & DEBUG_SERVER_CONNECT)
			say("new_des is %d", des);

		if ((s = get_server(new_server)))
		{
		    say("Unable to connect to port %d of server %s: [%d] %s", 
				s->port, s->name, des, my_strerror(errno));

		    /* Would cause client to crash, if not wiped out */
		    set_server_ssl_enabled(new_server, FALSE);
		}
		else
			say("Unable to connect to server.");

		return -1;		/* Connect failed */
	}

	from_server = new_server;	/* XXX sigh */
	new_open(des);

	if (*s->name != '/')
	{
		len = sizeof(s->local_sockname);
		getsockname(des, (SA *)&s->local_sockname, &len);

		len = sizeof(s->remote_sockname);
		getpeername(des, (SA *)&s->remote_sockname, &len);
	}


	/*
	 * Initialize all of the server_list data items
	 * XXX - Calling add_to_server_list is a hack.
	 */
	add_to_server_list(s->name, s->port, NULL, NULL, NULL, NULL, 1);
	s->des = des;
	s->operator = 0;
	if (!s->d_nickname)
		malloc_strcpy(&s->d_nickname, nickname);

	/*
	 * Reset everything and go on our way.
	 */
	message_from(NULL, LOG_CRAP);
	update_all_status();
	server_reconnects_to(new_server, new_server + 1);
	return 0;			/* New connection established */
}


/*
 * This attempts to substitute a new connection to server 'new_server'
 * for the server connection 'old_server'.
 * If 'old_server' is NOSERV, then this is the first connection for the entire
 * 	client.  We have to handle this differently because no windows exist
 *	at this point and we have to be careful not to call 
 *	window_check_servers.
 * If 'new_conn' is 1, then this is a new server connection on an existing
 * 	window.  This is used when a window "splits off" from an existing
 *	server connection to a new server.
 */
int 	connect_to_new_server (int new_server, int old_server, int new_conn)
{
	Server *s;
	int	x;
	int	old;

	/*
	 * First of all, if we can't connect to the new server, we don't
	 * do anything here.  Note that this might succeed because we are
	 * already connected to the new server, in which case 
	 * 'is_server_registered' should be true.
	 */
	if ((x = connect_to_server(new_server)) == -1)
	{
		if (old_server != NOSERV && (s = get_server(old_server)) && s->des != -1)
			say("Connection to server %s resumed...", s->name);

		return -1;
	}

	/*
	 * Now at this point, we have successfully opened a connection
	 * to the new server.  What to do, what to do?
	 */
	from_server = new_server;	/* XXX sigh */

	/*
	 * If connect_to_server() resulted in a new server connection,
	 * go ahead and register it.
	 */
	if (x == 0)
	{
		if (!(s = get_server(new_server)))
			return -1;
		register_server(new_server, s->d_nickname);
	}

	/*
	 * If we're not actually switching servers...
	 */
	if (old_server == new_server)
	{
		/* 
		 * If we were asked to connect to a server we were 
		 * already connected to, then there's nothing more
		 * to be done, eh?
		 */
		if (x == -3)
			return 0;

		/*
		 * We must be reconnecting to a server.  Try to find
		 * and grab all of its old windows that are just lying
		 * around.  This should be harmless at worst.
		 */
		change_window_server(old_server, new_server);
		return 0;
	}

	/*
	 * At this point, we know that new_server != old_server, so we are
	 * actually trying to change servers, rather than just try to re-
	 * establish an old, dropped one.  We do some bookkeeping here to 
	 * try to keep everything in order, as far as windows and channels go.
	 * This is not done for /window server, though.
	 */
	if (new_conn == 0)
	{
		/* 
		 * By default, all server channels are saved; however,
		 * we save them just in case.
		 */
		if (is_server_open(old_server))
		{
			set_server_save_channels(old_server, 1);
			close_server(old_server, "changing servers");
		}

		/*
		 * Situation #1:  If we are /server'ing to a server
		 * that was already connected, then we do *not* transfer
		 * over the channels that are on the "old" server.  Instead
		 * we trashcan them so they don't get in the way and become
		 * phantom channels in the future.  Otherwise, if we are
		 * establishing a brand new connection, we 
		 */
		if (x == -3)
		{
			destroy_waiting_channels(old_server);
			destroy_server_channels(old_server);
		}
	
		/*
		 * If we are /server'ing to a server that was not already 
		 * connected, then we *do* want to transfer over the 
		 * channels that are on the "old" server.
		 */
		else
			change_server_channels(old_server, new_server);

		/*
		 * If 'new_conn' is 0, and 'old_server' is NOSERV, then we're
		 * doing something like a /server in a disconnected window.
		 * That's no sweat.  We will just try to figure out what
		 * server this window was last connected to, and move all
		 * of those windows over to this new server.
		 */
		if (old_server == NOSERV)
		{
			if (!(old = get_window_oldserver(0)) != NOSERV)
				old_server = old;
		}

		/*
		 * In all situations, we always want to move all of the
		 * windows over from the last server to this new server
		 * as a logical group.  Grab any old windows that seem 
		 * to want to be part of this fun as well.  Also, do a
		 * window_check_servers() to make sure all is coherent.
		 */
		change_window_server(old_server, new_server);
	}

	/*
	 * And it never hurts, in any situation, to set the AWAY message
	 * if there is one.
	 */
	if (!get_server_away(new_server) && get_server_away(old_server))
		set_server_away(new_server, get_server_away(old_server));

	return 0;
}

/*
 * This function supplants the "get_connected" function; it started out life
 * as a front end to get_connected(), but it ended up being more flexible
 * than get_connected() and everyone was calling reconnect() instead, so I 
 * just rolled the two of them together and here we are.
 *
 * The purpose of this function is to make it easier to handle dropped
 * server connections.  If we recieve a Notice Of Termination (such as an
 * 010 or 465 numerics, or a KILL or such), we may not want to actually 
 * terminate the connection before the server does as it may send us more
 * important information.  Instead of closing the connection, we instead
 * drop a "hint" to the server what it should do when it sees an EOF.  For
 * most circumstances, the "hint" is "reconnect to yourself again".  However,
 * sometimes we want to reconnect to a different server.  For all of these
 * circumstances, this function is called to figure out what to do.
 *
 * Use the function "server_reconnects_to" to control how this function
 * behaves.  Setting the reconnecting server to NOSERV inhibits any connection
 * from occuring upon EOF.  Setting the reconnecting server to itself causes
 * a normal reconnect act.  Setting the reconnecting server to another refnum
 * causes the server connection to be migrated to that refnum.  The original
 * connection is closed only if the new refnum is successfully opened.
 *
 * The "samegroup" argument is new and i think every call to reconnect
 * uses the value '1' which means 'only try to connect to the same group
 * as whatever we're reconnecting to'; the code for value 0 may not be
 * tested very well, and i can't think of any uses for it, but eh, you 
 * never know what the future will hold.
 */
int	reconnect (int oldserv, int samegroup)
{
	Server *s;
	int	newserv;
	int	i, j;
	int	was_registered = 0;
	int	registered;
	int	max_reconnects = get_int_var(MAX_RECONNECTS_VAR);

	if (!server_list)
	{
                if (do_hook(DISCONNECT_LIST, "No Server List"))
		    say("No server list. Use /SERVER to connect to a server");
		return -1;
	}

	if (oldserv == NOSERV)
	{
		newserv = reconnects_to_hint;
		registered = 0;
	}
	else
	{
		if (!(s = get_server(oldserv)))
			return -1;
		was_registered = is_server_registered(oldserv);
		newserv = s->reconnect_to;

		/*
		 * Inhibit automatic reconnections.
		 */
		if (!was_registered && newserv == oldserv &&
				get_int_var(AUTO_RECONNECT_VAR) == 0)
			newserv = NOSERV;

		if (newserv != NOSERV)
			set_server_save_channels(oldserv, 1);
		else
			set_server_save_channels(oldserv, 0);

		if (newserv == oldserv || newserv == NOSERV)
			close_server(oldserv, get_server_quit_message(oldserv));
		registered = is_server_registered(oldserv);
	}

	if (newserv == NOSERV)
	{
		window_check_servers();
		return -1;		/* User wants to disconnect */
	}

	/* Try all of the other servers, stop when one of them works. */
	for (i = 0; i < number_of_servers; i++)
	{
		j = (i + newserv) % number_of_servers;
		if (!(s = get_server(j)))
			continue;
		if (samegroup && oldserv != NOSERV &&
			my_stricmp(get_server_group(oldserv), 
				   get_server_group(j)))
			continue;
		if (newserv != oldserv && j == oldserv)
			continue;
		if (s->reconnects++ > max_reconnects) {
			say("Auto-reconnect has been throttled because too many unsuccessfull attempts to connect to server %d have been performed.", j);
			break;
		}
		if (connect_to_new_server(j, oldserv, 0) == 0)
			return j;

		/*
		 * Because of the new way we handle things, if the old
		 * server was connected, then this was because the user 
		 * was trying to change to a new server; if the connection
		 * to the new server failed, then we want to resume the old
		 * connection.  But if the server was not connected, then 
		 * that means we already saw an EOF on either the read or
		 * write end, and so there is no connection to resume!  In
		 * which case we just keep cycling our servers until we find
		 * some place we like.
		 */
		if (registered)
			break;
	}

	/* If we reach this point, we have failed.  Time to punt */

	/*
	 * If our prior state was registered, revert back to the prior
	 * registered server.
	 */
	if (registered && newserv != oldserv)
	{
		say("A new server connection could not be established.");
		say("Your previous server connection will be resumed.");
		from_server = oldserv;
		window_check_servers();
		return -1;
	}

	/*
	 * In any situation, if 'oldserv' is not registered at this point, 
	 * then we need to throw away it's channels.
	 */
	if (!is_server_registered(oldserv))
	{
		destroy_waiting_channels(oldserv);
		destroy_server_channels(oldserv);
	}

	/*
	 * Our prior state was unregistered.  Tell the user
	 * that we give up and tough luck.
	 */
	if (do_hook(DISCONNECT_LIST, "Unable to connect to a server"))
		say("Sorry, cannot connect.  Use /SERVER to connect "
						"to a server");

	window_check_servers();
	return -1;
}


int 	close_all_servers (const char *message)
{
	int i;

	for (i = 0; i < number_of_servers; i++)
	{
		set_server_quit_message(i, message);
		server_reconnects_to(i, NOSERV);
		reconnect(i, 0);
	}

	return 0;
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

	/* Make sure server refnum is valid */
	if (!(s = get_server(refnum)))
	{
		yell("Closing server [%d] makes no sense!", refnum);
		return;
	}

	was_registered = is_server_registered(refnum);

	clean_server_queues(refnum);
	if (s->waiting_out > s->waiting_in)		/* XXX - hack! */
		s->waiting_out = s->waiting_in = 0;

	if (s->save_channels == 1)
		save_channels(refnum);
	else if (s->save_channels == 0)
	{
		destroy_waiting_channels(refnum);
		destroy_server_channels(refnum);
	}
	else
		panic("Somebody forgot to set save_channels for server %d", refnum);

	s->save_channels = -1;
	s->operator = 0;
	s->registration_pending = 0;
	s->registered = 0;
	s->rejoined_channels = 0;
	new_free(&s->nickname);
	new_free(&s->s_nickname);

	if (s->des != -1)
	{
		if (message && *message && !s->closing)
		{
		    s->closing = 1;
		    if (x_debug & DEBUG_OUTBOUND)
			yell("Closing server %d because [%s]", 
			   refnum, message ? message : empty_string);

		    /*
		     * Only tell the server we are leaving if we are 
		     * registered.  This avoids an infinite loop in the
		     * D-line case.
		     */
		    if (was_registered)
			    send_to_aserver(refnum, "QUIT :%s\n", message);
		    if (get_server_ssl_enabled(refnum) == TRUE)
		    {
#ifndef HAVE_SSL
			panic("close_server: Server %d claims to be using "
			      "ssl on a non-ssl client", refnum);
#else
			say("Closing SSL connection");
			SSL_shutdown((SSL *)s->ssl_fd);
#endif
		    }
		    do_hook(SERVER_LOST_LIST, "%d %s %s", 
				refnum, s->name, message);
		}

		s->des = new_close(s->des);
	}

	return;
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
		if (!s->away || !strcmp(s->away, message))
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
static char *do_umode (int refnum)
{
	Server *s;
	char *c;
	long flags, flags2, i;

	if (!(s = get_server(refnum)))
		return empty_string;

	c = s->umode;
	flags = s->flags;
	flags2 = s->flags2;

	for (i = 0; s->umodes[i]; i++)
	{
		if (i > 31)
		{
			if (flags2 & (0x1 << (i - 32)))
				*c++ = s->umodes[i];
		}
		else
		{
			if (flags & (0x1 << i))
				*c++ = s->umodes[i];
		}
	}

	*c = 0;
	return s->umode;
}

const char *	get_possible_umodes (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return empty_string;

	return s->umodes;
}

void	set_possible_umodes (int refnum, const char *umodes)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	malloc_strcpy(&s->umodes, umodes);
}

const char *	get_umode (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return empty_string;

	return s->umode;
}

void 	clear_user_modes (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	s->flags = 0;
	s->flags2 = 0;
	do_umode(refnum);
}

void	update_user_mode (const char *modes)
{
	int		onoff = 1;
	const char *	p_umodes = get_possible_umodes(from_server);

	if (x_debug & DEBUG_SERVER_CONNECT)
		yell("Possible user modes for server [%d]: [%s]", from_server, p_umodes);

	for (; *modes; modes++)
	{
		if (*modes == '-')
			onoff = 0;
		else if (*modes == '+')
			onoff = 1;

		else if   ((*modes >= 'a' && *modes <= 'z')
			|| (*modes >= 'A' && *modes <= 'Z'))
		{
			size_t 	idx;
			int 	c = *modes;

			idx = ccspan(p_umodes, c);
			if (p_umodes && p_umodes[idx] == 0)
				yell("WARNING: Invalid user mode %c referenced on server %d",
						*modes, last_server);
			else
				set_server_flag(from_server, idx, onoff);

			if (c == 'O' || c == 'o')
				set_server_operator(from_server, onoff);
		}
	}
	update_all_status();
}

void	reinstate_user_modes (void)
{
	const char *modes = get_umode(from_server);

	if (!modes && !*modes)
		modes = send_umode;

	if (modes && *modes)
	{
		if (x_debug & DEBUG_OUTBOUND)
			yell("Reinstating your user modes on server [%d] to [%s]", from_server, modes);
		send_to_server("MODE %s +%s", get_server_nickname(from_server), modes);
		clear_user_modes(from_server);
	}
}

void	set_server_flag (int refnum, int flag, int value)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	if (flag > 31)
	{
		if (value)
			s->flags2 |= 0x1 << (flag - 32);
		else
			s->flags2 &= ~(0x1 << (flag - 32));
	}
	else
	{
		if (value)
			s->flags |= 0x1 << flag;
		else
			s->flags &= ~(0x1 << flag);
	}

	do_umode(refnum);
}

int	get_server_flag (int refnum, int value)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return 0;

	if (value > 31)
		return s->flags2 & (0x1 << (value - 32));
	else
		return s->flags & (0x1 << value);
}


/* get_server_isssl: returns 1 if the server is using SSL connection */
int	get_server_isssl (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return 0;

	return (s->ssl_enabled == TRUE ? 1 : 0);
}

const char	*get_server_cipher (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)) || s->ssl_enabled == FALSE)
		return empty_string;

#ifndef HAVE_SSL
	return empty_string;
#else
	return SSL_get_cipher((SSL *)s->ssl_fd);
#endif
}


/* CONNECTION/REGISTRATION STATUS */
void	register_server (int refnum, const char *nickname)
{
	Server *	s;

	if (!(s = get_server(refnum)))
		return;

	if (s->registration_pending)
		return;		/* Whatever */

	if (is_server_registered(refnum))
		return;		/* Whatever */

	do_hook(SERVER_ESTABLISHED_LIST, "%s %d",
		get_server_name(refnum), get_server_port(refnum));

	s->registration_pending = 1;
	if (get_server_try_ssl(refnum) == TRUE)
	{
#ifndef HAVE_SSL
		panic("register_server on server %d claims to be doing"
			"SSL on a non-ssl client.", refnum);
#else
		char *		cert_issuer;
		char *		cert_subject;
		X509 *		server_cert;
		EVP_PKEY *	server_pkey;

		say("SSL negotiation in progress...");
		/* Set up SSL connection */
		s->ctx = SSL_CTX_init(0);
		s->ssl_fd = (void *)SSL_FD_init(s->ctx, s->des);

		if (x_debug & DEBUG_SSL)
			say("SSL negotiation using %s",
				get_server_cipher(refnum));
		say("SSL negotiation on port %d of server %s complete",
			s->port, get_server_name(refnum));
		server_cert = SSL_get_peer_certificate((SSL *)s->ssl_fd);

		if (!server_cert) {
			say ("SSL negotiation failed");
			say ("WARNING: Bailing to no encryption");
			SSL_CTX_free((SSL_CTX *)s->ctx);
			send_to_aserver(refnum, "%s", empty_string);
		} else {
			char *u_cert_subject, *u_cert_issuer;

			cert_subject = X509_NAME_oneline(
				X509_get_subject_name(server_cert),0,0);
			u_cert_subject = urlencode(cert_subject);
			say("subject: %s", u_cert_subject);
			cert_issuer = X509_NAME_oneline(
				X509_get_issuer_name(server_cert),0,0);
			u_cert_issuer = urlencode(cert_issuer);
			say("issuer: %s", u_cert_issuer);

			set_server_ssl_enabled(refnum, TRUE);
			server_pkey = X509_get_pubkey(server_cert);
			say("public key: %d", EVP_PKEY_bits(server_pkey));
			do_hook(SSL_SERVER_CERT_LIST, "%s %s %s",
				s->name, cert_subject, cert_issuer);

			new_free(&u_cert_issuer);
			new_free(&u_cert_subject);
			free(cert_issuer);
			free(cert_subject);
		}
#endif
	}
	if (s->password)
		send_to_aserver(refnum, "PASS %s", s->password);

	send_to_aserver(refnum, "USER %s %s %s :%s", username, 
			(send_umode && *send_umode) ? send_umode : 
			(LocalHostName ? LocalHostName : hostname), 
			username, (*realname ? realname : space));
	change_server_nickname(refnum, nickname);
}

/*
 * password_sendline: called by send_line() in get_password() to handle
 * hitting of the return key, etc 
 * -- Callback function
 */
void 	password_sendline (char *data, char *line)
{
	int	new_server;

	if (!line || !*line)
		return;

	new_server = parse_server_index(data, 0);
	set_server_password(new_server, line);
	change_window_server(new_server, new_server);
	server_reconnects_to(new_server, new_server);
	reconnect(new_server, 1);
}

char *	get_server_password (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return NULL;

	return s->password;
}

/*
 * set_server_password: this sets the password for the server with the given
 * index. If 'password' is NULL, the password is cleared
 */
char	*set_server_password (int refnum, const char *password)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return NULL;

	if (password)
		malloc_strcpy(&s->password, password);
	else
		new_free(&s->password);

	return s->password;
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

/*
 * is_server_registered: returns true if the given server is registered.  
 * This means that both the tcp connection is open and the user is properly
 * registered 
 */
int	is_server_registered (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return 0;

	return s->registered;
}

/*
 * Informs the client that the user is now officially registered or not
 * registered on the specified server.
 */
void 	server_is_registered (int refnum, int value)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	s->registered = value;
	s->registration_pending = 0;
	s->rejoined_channels = 0;

	if (value)
	{
		/* 
		 * By default, we want to save the server's channels.
		 * If anything happens where we don't want to do this,
		 * then we must turn it off, rather than the other way
		 * around.
		 */
#if 0
		set_server_save_channels(refnum, 0);
#endif
		s->reconnect_to = refnum;
		s->eof = 0;
		clear_reconnect_counts();
		destroy_005(refnum);
	}
}

void	server_did_rejoin_channels (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	s->rejoined_channels = 1;
}

int	did_server_rejoin_channels (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return 0;

	if (!is_server_registered(refnum))
		return 0;
	return s->rejoined_channels;
}

BUILT_IN_COMMAND(disconnectcmd)
{
	char	*server;
	char	*message;
	int	i;

	if (!(server = next_arg(args, &args)))
		i = get_window_server(0);
	else
	{
		if ((i = parse_server_index(server, 0)) == NOSERV)
		{
			say("No such server!");
			return;
		}
	}

	if (get_server(i))
	{
		if (!args || !*args)
			message = "Disconnecting";
		else
			message = args;

		say("Disconnecting from server %s", get_server_itsname(i));
		server_reconnects_to(i, NOSERV);
		set_server_save_channels(i, 0);
		close_server(i, message);
		update_all_status();
	}

	if (!connected_to_server)
                if (do_hook(DISCONNECT_LIST, "Disconnected by user request"))
			say("You are not connected to a server, use /SERVER to connect.");
} 

int 	auto_reconnect_callback (void *d)
{
	char *	stuff = (char *)d;
	int	servref;

	servref = parse_server_index(stuff, 0);
	new_free((char **)&d);

	if (servref == NOSERV)
		return 0;		/* Don't bother */

	server_reconnects_to(servref, servref);
	reconnect(servref, 1);
	return 0;
}

int	server_reconnects_to (int oldref, int newref)
{
	Server *old_s;
	Server *new_s;

	if (oldref == NOSERV)
	{
		reconnects_to_hint = newref;
		return 1;
	}

	if (!(old_s = get_server(oldref)))
		return 0;
	if (newref != NOSERV && !(new_s = get_server(newref)))
		return 0;

	old_s->reconnect_to = newref;
	return 1;
}

/* PORTS */
void    set_server_port (int refnum, int port)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	s->port = port;
}

/* get_server_port: Returns the connection port for the given server index */
int	get_server_port (int refnum)
{
	Server *s;
	char	p_port[12];

	if (!(s = get_server(refnum)))
		return 0;

	if (!inet_ntostr((SA *)&s->remote_sockname, NULL, 0, p_port, 12, 0))
		return atol(p_port);

	return s->port;
}

int	get_server_local_port (int refnum)
{
	Server *s;
	char	p_port[12];

	if (!(s = get_server(refnum)))
		return 0;

	if (!inet_ntostr((SA *)&s->remote_sockname, NULL, 0, p_port, 12, 0))
		return atol(p_port);

	return 0;
}

SS	get_server_local_addr (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		panic("Refnum %d isn't valid in get_server_local_addr", refnum);

	return s->local_sockname;
}

SS	get_server_uh_addr (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		panic("Refnum %d isn't valid in get_server_uh_addr", refnum);

	return s->uh_addr;
}

/* USERHOST */
void	set_server_userhost (int refnum, const char *userhost)
{
	Server *s;
	char *host;

	if (!(s = get_server(refnum)))
		return;

	if (!(host = strchr(userhost, '@')))
	{
		yell("Cannot set your userhost to [%s] because it does not"
		      "contain a @ character!", userhost);
		return;
	}

	malloc_strcpy(&s->userhost, userhost);

	/* Ack!  Oh well, it's for DCC. */
	FAMILY(s->uh_addr) = AF_INET;
	if (inet_strton(host + 1, zero, (SA *)&s->uh_addr, 0))
		yell("Ack.  The server says your userhost is [%s] and "
		     "I can't figure out the IPv4 address of that host! "
		     "You won't be able to use /SET DCC_USE_GATEWAY_ADDR ON "
		     "with this server connection!", host + 1);
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
	char *	n;

	if (!(s = get_server(refnum)))
		return;			/* Uh, no. */

	s->resetting_nickname = 0;
	if (nick)
	{
		n = LOCAL_COPY(nick);
		if ((n = check_nickname(n, 1)) != NULL)
		{
		    malloc_strcpy(&s->d_nickname, n);
		    malloc_strcpy(&s->s_nickname, n);
		}
		else
			reset_nickname(refnum);
	}

	if (s->s_nickname)
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

	if (!(s = get_server(refnum)))
		return;

	malloc_strcpy(&s->nickname, nick);
	malloc_strcpy(&s->d_nickname, nick);
	new_free(&s->s_nickname);
	s->fudge_factor = 0;

	if (refnum == primary_server)
		strmcpy(nickname, nick, NICKNAME_LEN);

	update_all_status();
}

/* 
 * This will generate up to 18 nicknames plus the 9-length(nickname)
 * that are unique but still have some semblance of the original.
 * This is intended to allow the user to get signed back on to
 * irc after a nick collision without their having to manually
 * type a new nick every time..
 * 
 * The func will try to make an intelligent guess as to when it is
 * out of guesses, and if it ever gets to that point, it will do the
 * manually-ask-you-for-a-new-nickname thing.
 */
void 	fudge_nickname (int refnum)
{
	Server *s;
const	char	*nicklen_005;
	int	nicklen;
	char 	l_nickname[NICKNAME_LEN + 1];

	if (!(s = get_server(refnum)))
		return;			/* Uh, no. */

	/*
	 * If we got here because the user did a /NICK command, and
	 * the nick they chose doesnt exist, then we just dont do anything,
	 * we just cancel the pending action and give up.
	 */
	if (s->nickname_pending)
	{
		set_server_nickname_pending(refnum, 0);
		new_free(&s->s_nickname);
		return;
	}

	/*
	 * Ok.  So we're not doing a /NICK command, so we need to see
	 * if maybe we're doing some other type of NICK change.
	 */
	if (s->s_nickname)
		strlcpy(l_nickname, s->s_nickname, NICKNAME_LEN);
	else if (s->nickname)
		strlcpy(l_nickname, s->nickname, NICKNAME_LEN);
	else
		strlcpy(l_nickname, nickname, NICKNAME_LEN);


	if (s->fudge_factor < strlen(l_nickname))
		s->fudge_factor = strlen(l_nickname);
	else
	{
		if (++s->fudge_factor == 17)
		{
			/* give up... */
			reset_nickname(refnum);
			s->fudge_factor = 0;
			return;
		}
	}

	/* 
	 * Process of fudging a nickname:
	 * If the nickname length is less then 9, add an underscore.
	 */
	nicklen_005 = get_server_005(refnum, "NICKLEN");
	nicklen = nicklen_005 ? atol(nicklen_005) : 9;
	nicklen = nicklen > 0 ? nicklen : 9;

	if (strlen(l_nickname) < nicklen)
		strcat(l_nickname, "_");

	/* 
	 * The nickname is 9 characters long. roll the nickname
	 */
	else
	{
		char tmp = l_nickname[nicklen-1];
		int foo;
		for (foo = nicklen-1; foo>0; foo--)
			l_nickname[foo] = l_nickname[foo-1];
		l_nickname[0] = tmp;
	}

	/*
	 * This is the degenerate case
	 */
	if (strspn(l_nickname, "_") >= nicklen)
	{
		reset_nickname(refnum);
		return;
	}

	change_server_nickname(refnum, l_nickname);
}


/*
 * -- Callback function
 */
void 	nickname_sendline (char *data, char *nick)
{
	int	new_server;

	new_server = parse_server_index(data, 0);
	change_server_nickname(new_server, nick);
}

/*
 * reset_nickname: when the server reports that the selected nickname is not
 * a good one, it gets reset here. 
 * -- Called by more than one place
 */
void 	reset_nickname (int refnum)
{
	Server *s;
	char	server_num[10];

	if (!(s = get_server(refnum)) || s->resetting_nickname == 1)
		return; 		/* Don't repeat the reset */

	s->resetting_nickname = 1;
	say("You have specified an invalid nickname");
	if (!dumb_mode)
	{
		say("Please enter your nickname");
		strcpy(server_num, ltoa(refnum));
		add_wait_prompt("Nickname: ", nickname_sendline, server_num,
			WAIT_PROMPT_LINE, 1);
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

/*
 * save_servers; dumps your serverlist to a file, which can be /load'ed
 * later.  --SrfRoG
 */
void 	save_servers (FILE *fp)
{
	Server *s;
	int	i;

	for (i = 0; i < number_of_servers; i++)
	{
		if (!(s = get_server(i)))
			continue;

		/* SERVER -ADD server:port:password:nick */
		fprintf(fp, "SERVER -ADD %s:%d:%s:%s:%s:%s\n",
			s->name, s->port,
			s->password ?  s->password : empty_string,
			s->nickname ?  s->nickname : empty_string,
			s->group ?  s->group : empty_string,
			get_server_type(i));
	}
}

void	clear_reconnect_counts (void)
{
	Server *s;
	int	i;

	for (i = 0; i < number_of_servers; i++)
	{
		if (!(s = get_server(i)))
			continue;
		s->reconnects = 0;
	}
}

const char *get_server_type (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return NULL;

	if (get_server_try_ssl(refnum) == TRUE)
		return "IRC-SSL";
	else
		return "IRC";
}

/*****************************************************************************/
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

#define IACCESSOR(param, member)		\
SET_IATTRIBUTE(param, member)			\
GET_IATTRIBUTE(member)

#define SACCESSOR(param, member, default)	\
SET_SATTRIBUTE(param, member)			\
GET_SATTRIBUTE(member, default)

IACCESSOR(v, doing_privmsg)
IACCESSOR(v, doing_notice)
IACCESSOR(v, doing_ctcp)
IACCESSOR(v, nickname_pending)
IACCESSOR(v, sent)
IACCESSOR(v, version)
IACCESSOR(v, save_channels)
SACCESSOR(chan, invite_channel, NULL)
SACCESSOR(nick, last_notify_nick, NULL)
SACCESSOR(nick, joined_nick, NULL)
SACCESSOR(nick, public_nick, NULL)
SACCESSOR(nick, recv_nick, NULL)
SACCESSOR(nick, sent_nick, NULL)
SACCESSOR(text, sent_body, NULL)
SACCESSOR(nick, redirect, NULL)
SACCESSOR(group, group, "<default>")
SACCESSOR(message, quit_message, "get_server_quit_message")
SACCESSOR(cookie, cookie, NULL);
SACCESSOR(ver, version_string, NULL);

GET_IATTRIBUTE(operator)
void	set_server_operator (int refnum, int flag)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

	s->operator = flag;
	oper_command = 0;		/* No longer doing oper */
	do_umode(refnum);
}

SACCESSOR(name, name, "<none>");
SET_SATTRIBUTE(name, itsname)
const char	*get_server_itsname (int refnum)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return "<none>";

	if (s->itsname)
		return s->itsname;
	else
		return s->name;
}

int	get_server_protocol_state (int refnum)
{
	int	retval;

	retval = get_server_doing_ctcp(refnum);
	retval = retval << 8;

	retval += get_server_doing_notice(refnum);
	retval = retval << 8;

	retval += get_server_doing_privmsg(refnum);

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
	state = state >> 8;
}

void	set_server_try_ssl (int refnum, int flag)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

#ifndef HAVE_SSL
	if (flag == TRUE)
		say("This server does not have SSL support.");
	flag = FALSE;
#endif
	s->try_ssl = flag;
}
GET_IATTRIBUTE(try_ssl)

void	set_server_ssl_enabled (int refnum, int flag)
{
	Server *s;

	if (!(s = get_server(refnum)))
		return;

#ifndef HAVE_SSL
	if (flag == TRUE)
		say("This server does not have SSL support.");
	flag = FALSE;
	s->try_ssl = flag;
#endif
	s->ssl_enabled = flag;
}
GET_IATTRIBUTE(ssl_enabled)

/*****/
/* WAIT STUFF */
/*
 * This isnt a command, its used by the wait command.  Since its extern,
 * and it doesnt use anything static in this file, im sure it doesnt
 * belong here.
 */
void 	server_hard_wait (int i)
{
	Server *s;
	int	proto, old_from_server;

	if (!(s = get_server(i)))
		return;

	if (!is_server_registered(i))
		return;

	proto = get_server_protocol_state(i);
	old_from_server = from_server;

	s->waiting_out++;
	lock_stack_frame();
	send_to_aserver(i, "%s", lame_wait_nick);
	while ((s = get_server(i)) && (s->waiting_in < s->waiting_out))
		io("oh_my_wait");

	set_server_protocol_state(i, proto);
	from_server = old_from_server;
}

void	server_passive_wait (int i, const char *stuff)
{
	Server *s;
	WaitCmd	*new_wait;

	if (!(s = get_server(i)))
		return;

	new_wait = (WaitCmd *)new_malloc(sizeof(WaitCmd));
	new_wait->stuff = m_strdup(stuff);
	new_wait->next = NULL;

	if (s->end_wait_list)
		s->end_wait_list->next = new_wait;
	s->end_wait_list = new_wait;
	if (!s->start_wait_list)
		s->start_wait_list = new_wait;

	send_to_aserver(i, "%s", wait_nick);
}

/*
 * How does this work?  Well, when we issue the /wait command it increments
 * a variable "waiting_out" which is the number of times that wait has been
 * caled so far.  If we get a wait token, we increase the waiting_in level
 * by one, and if the number of inbound waiting tokens is the same as the 
 * number of outbound tokens, then we are free to clear this stack frame
 * which will cause all of the pending waits to just fall out.
 */
int	check_server_wait (int refnum, const char *nick)
{
	Server	*s;

	if (!(s = get_server(refnum)))
		return 0;

	if ((s->waiting_out > s->waiting_in) && !strcmp(nick, lame_wait_nick))
	{
		s->waiting_in++;
		unlock_stack_frame();
	        return 1;
	}

	if (s->start_wait_list && !strcmp(nick, wait_nick))
	{
		WaitCmd *old = s->start_wait_list;

		s->start_wait_list = old->next;
		if (old->stuff)
		{
			parse_line("WAIT", old->stuff, empty_string, 0, 0);
			new_free(&old->stuff);
		}
		if (s->end_wait_list == old)
			s->end_wait_list = NULL;
		new_free((char **)&old);
		return 1;
	}
	return 0;
}

/****** FUNNY STUFF ******/
IACCESSOR(v, funny_min)
IACCESSOR(v, funny_max)
IACCESSOR(v, funny_flags)
SACCESSOR(match, funny_match, NULL);

void	set_server_funny_stuff (int refnum, int min, int max, int flags, const char *stuff)
{
	set_server_funny_min(refnum, min);
	set_server_funny_max(refnum, max);
	set_server_funny_flags(refnum, flags);
	set_server_funny_match(refnum, stuff);
}

/*****************************************************************************/

/* 005 STUFF */

void make_005 (int refnum)
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

void destroy_a_005 (A005_item *item)
{
	if (item) {
		new_free(&((*item).name));
		new_free(&((*item).value));
		new_free(&item);
	}
}

void destroy_005 (int refnum)
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

GET_ARRAY_NAMES_FUNCTION(get_server_005s, (__FROMSERV->a005))

const char* get_server_005 (int refnum, char *setting)
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

/* value should be null pointer or empty to clear. */
void set_server_005 (int refnum, char *setting, char *value)
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
		(*new_005).name = m_strdup(setting);
		(*new_005).value = m_strdup(value);
		add_to_array((array*)(&s->a005), (array_item*)new_005);
	}
}


/* Used by function_serverctl */
/*
 * $serverctl(REFNUM server-desc)
 * $serverctl(MAX)
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
 */
char 	*serverctl 	(char *input)
{
	int	refnum, num, len;
	char	*listc, *listc1;
	const char *ret;

	GET_STR_ARG(listc, input);
	len = strlen(listc);
	if (!my_strnicmp(listc, "REFNUM", len)) {
		char *server;

		GET_STR_ARG(server, input);
		if (is_number(server)) {
			int refnum;
			refnum = parse_server_index(server, 1);
			if (refnum != NOSERV)
				RETURN_STR(server);
			RETURN_EMPTY;
		}
		RETURN_INT(find_server_refnum(server, &input));
	} else if (!my_strnicmp(listc, "GET", len)) {
		GET_INT_ARG(refnum, input);
		if (!get_server(refnum))
			RETURN_EMPTY;

		GET_STR_ARG(listc, input);
		len = strlen(listc);
		if (!my_strnicmp(listc, "AWAY", len)) {
			ret = get_server_away(refnum);
			RETURN_STR(ret);
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
		} else if (!my_strnicmp(listc, "QUIT_MESSAGE", len)) {
			ret = get_server_quit_message(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "SSL", len)) {
			num = get_server_try_ssl(refnum);
			RETURN_INT(num);
		} else if (!my_strnicmp(listc, "UMODE", len)) {
			ret = get_umode(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "UMODES", len)) {
			ret = get_possible_umodes(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "USERHOST", len)) {
			ret = get_server_userhost(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "VERSION", len)) {
			ret = get_server_version_string(refnum);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "005", len)) {
			GET_STR_ARG(listc1, input);
			ret = get_server_005(refnum, listc1);
			RETURN_STR(ret);
		} else if (!my_strnicmp(listc, "005s", len)) {
			int ofs = from_server;
			char *ret;
			from_server = refnum;
			ret = get_server_005s(input);
			from_server = ofs;
			RETURN_MSTR(ret);
		}
	} else if (!my_strnicmp(listc, "SET", len)) {
		GET_INT_ARG(refnum, input);
		if (!get_server(refnum))
			RETURN_EMPTY;

		GET_STR_ARG(listc, input);
		len = strlen(listc);
		if (!my_strnicmp(listc, "AWAY", len)) {
			set_server_away(refnum, input);
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
			int value;

			GET_INT_ARG(value, input);
			set_server_try_ssl(refnum, value);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "UMODE", len)) {
			RETURN_EMPTY;		/* Read only for now */
		} else if (!my_strnicmp(listc, "UMODES", len)) {
			RETURN_EMPTY;		/* Read only for now */
		} else if (!my_strnicmp(listc, "USERHOST", len)) {
			set_server_userhost(refnum, input);
		} else if (!my_strnicmp(listc, "VERSION", len)) {
			set_server_version_string(refnum, input);
		} else if (!my_strnicmp(listc, "005", len)) {
			GET_STR_ARG(listc1, input);
			set_server_005(refnum, listc1, input);
			RETURN_INT(!!*input);
		}
	} else if (!my_strnicmp(listc, "OMATCH", len)) {
		int	i;
		char *retval = NULL;

		for (i = 0; i < number_of_servers; i++)
			if (wild_match(input, get_server_name(i)))
				m_s3cat(&retval, space, ltoa(i));
		RETURN_MSTR(retval);
	} else if (!my_strnicmp(listc, "IMATCH", len)) {
		int	i;
		char *retval = NULL;

		for (i = 0; i < number_of_servers; i++)
			if (wild_match(input, get_server_itsname(i)))
				m_s3cat(&retval, space, ltoa(i));
		RETURN_MSTR(retval);
	} else if (!my_strnicmp(listc, "GMATCH", len)) {
		int	i;
		char *retval = NULL;

		for (i = 0; i < number_of_servers; i++)
			if (wild_match(input, get_server_group(i)))
				m_s3cat(&retval, space, ltoa(i));
		RETURN_MSTR(retval);
	} else if (!my_strnicmp(listc, "MAX", len)) {
		RETURN_INT(number_of_servers);
	} else
		RETURN_EMPTY;

	RETURN_EMPTY;
}

/*
 * got_my_userhost -- callback function, XXXX doesnt belong here
 * XXX Really does not belong here. 
 */
void 	got_my_userhost (int refnum, UserhostItem *item, const char *nick, const char *stuff)
{
	char *freeme;

	freeme = m_3dup(item->user, "@", item->host);
	set_server_userhost(refnum, freeme);
	new_free(&freeme);
}


