/*
 * server.c:  Things dealing with that wacky program we call ircd.
 *
 * Copyright 1990, 1999 Michael Sandrof and others
 * See the COPYRIGHT file for license information
 */

#define NEED_SERVER_LIST
#include "irc.h"
#include "parse.h"
#include "server.h"
#include "ircaux.h"
#include "lastlog.h"
#include "exec.h"
#include "window.h"
#include "output.h"
#include "names.h"
#include "hook.h"
#include "notify.h"
#include "screen.h"
#include "status.h"
#include "vars.h"
#include "newio.h"
#include "translat.h"
#ifdef HAVE_SSL
#include "ssl.h"
#endif

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

	int	never_connected = 1;		/* true until first connection
						 * is made */
	int	connected_to_server = 0;	/* true when connection is
						 * confirmed */
	int	reconnects_to_hint = 0;		/* XXX Hack.  Don't remind me */


/************************ SERVERLIST STUFF ***************************/

	Server	*server_list = (Server *) 0;
	int	number_of_servers = 0;

	int	primary_server = -1;
	int	from_server = -1;
	int	parsing_server_index = -1;
	int	last_server = -1;


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

	if ((from_server = find_in_server_list(server, port)) == -1)
	{
		from_server = number_of_servers++;
		RESIZE(server_list, Server, number_of_servers);

		s = &server_list[from_server];
		s->name = m_strdup(server);
		s->itsname = (char *) 0;
		s->password = (char *) 0;
		s->group = NULL;
		s->away = (char *) 0;
		s->version_string = (char *) 0;
		s->server2_8 = 0;
		s->oper = 0;
		s->des = -1;
		s->version = 0;
		s->flags = 0;
		s->flags2 = 0;
		s->nickname = (char *) 0;
		s->s_nickname = (char *) 0;
		s->d_nickname = (char *) 0;
		s->userhost = (char *) 0;
		s->connected = 0;
		s->eof = 0;
		s->motd = 1;
		s->port = port;
		s->who_queue = NULL;
		s->ison_queue = NULL;
		s->userhost_queue = NULL;
		s->local_addr.s_addr = 0;
		s->uh_addr.s_addr = 0;
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
#ifdef HAVE_SSL
		s->enable_ssl = FALSE;
		s->ssl_enabled = FALSE;
#endif

		if (password && *password)
			malloc_strcpy(&s->password, password);
		if (nick && *nick)
			malloc_strcpy(&s->d_nickname, nick);
		else if (!s->d_nickname)
			malloc_strcpy(&s->d_nickname, nickname);
		if (group && *group)
			malloc_strcpy(&s->group, group);
		if (server_type && *server_type)
			s->enable_ssl = my_stricmp(server_type, "IRC-SSL") ? 0 : 1;
		malloc_strcpy(&s->umodes, umodes);

		make_notify_list(from_server);
		do_umode(from_server);
	}
	else
	{
		s = &server_list[from_server];

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
	Window	*tmp = NULL;
	Server  *s;

	if (i < 0 || i >= number_of_servers)
		return;

	if (number_of_servers == 1)
	{
		say("You can't delete the last server!");
		return;
	}

	say("Deleting server [%d]", i);
	clean_server_queues(i);

	s = &server_list[i];
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
	new_free(&s->ison_queue);
	new_free(&s->who_queue);
	destroy_notify_list(i);

#ifdef HAVE_SSL
	if (server_list[i].ssl_enabled == TRUE)
	{
		SSL_free((SSL *)&s->ssl_fd);
		SSL_CTX_free((SSL_CTX *)&s->ctx);
	}
#endif

	memmove(&server_list[i], &server_list[i + 1], 
			(number_of_servers - i - 1) * sizeof(Server));
	number_of_servers--;
	RESIZE(server_list, Server, number_of_servers);

	/* update all he structs with server in them */
	channel_server_delete(i);
	exec_server_delete(i);
        if (i < primary_server)
                --primary_server;
        if (i < from_server)
                --from_server;
	while (traverse_all_windows(&tmp))
	{
		if (tmp->server > i)
			tmp->server--;
		if (tmp->last_server > i)
			tmp->last_server--;
	}
}



/*
 * Given a hostname and a port number, this function returns the server_list
 * index for that server.  If the specified server is not in the server_list,
 * then -1 is returned.  This function makes an attempt to do smart name
 * completion so that if you ask for "irc", it returns the first server that
 * has the leading prefix of "irc" in it.
 */
int	find_in_server_list (const char *server, int port)
{
	int	i;
	int	first_idx = -1;

	for (i = 0; i < number_of_servers; i++)
	{
		/* Check the port first.  This is a cheap, easy check */
		if (port && server_list[i].port && 
				port != server_list[i].port && port != -1)
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
		if (first_idx == -1)				\
			first_idx = i;				\
	}							\
}

		MATCH_WITH_COMPLETION(server, server_list[i].name)
		if (!server_list[i].itsname)
			continue;
		MATCH_WITH_COMPLETION(server, server_list[i].itsname)
	}

	return first_idx;	/* -1 if server was not found. */
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
	if ((refnum = parse_server_index(server)) != -1)
		return refnum;

	/*
	 * Next check to see if its a "server:port:password:nick"
	 */
	else if (strchr(server, ':'))
		parse_server_info(server, &cport, &password, &nick, &group, &server_type);

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
 * parse_server_index:  given a string, this checks if it's a number, and if
 * so checks it validity as a server index.  Otherwise -1 is returned 
 */
int	parse_server_index (const char *str)
{
	int	i;

	if (is_number(str))
	{
		i = atoi(str);
		if ((i >= 0) && (i < number_of_servers))
			return (i);
	}
	return (-1);
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
void	parse_server_info (char *name, char **port, char **password, char **nick, char **group, char **server_type)
{
	char *ptr;

	*port = *password = *nick = *server_type = NULL;

	do
	{
		ptr = name;
		if (*ptr == '"')
			name = new_next_arg(ptr, &ptr);
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
			parse_server_info(host, &port, &password, &nick, &group, &server_type);
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
	char	*expanded;
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
		return 1;
#endif
	}

	if ((expanded = expand_twiddle(file_path)))
	{
	    if ((fp = fopen(expanded, "r")))
	    {
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
		return (0);
	    }

	    new_free(&expanded);
	    new_free(&defaultgroup);
	}
	return 1;
}

/* display_server_list: just guess what this does */
void 	display_server_list (void)
{
	int	i;

	if (!server_list)
	{
		say("The server list is empty");
		return;
	}

	if (from_server != -1)
		say("Current server: %s %d",
				server_list[from_server].name,
				server_list[from_server].port);
	else
		say("Current server: <None>");

	if (primary_server != -1)
		say("Primary server: %s %d",
			server_list[primary_server].name,
			server_list[primary_server].port);
	else
		say("Primary server: <None>");

	say("Server list:");
	for (i = 0; i < number_of_servers; i++)
	{
		if (!server_list[i].nickname)
		{
			say("\t%d) %s %d [%s] %s", i,
				server_list[i].name,
				server_list[i].port,
				get_server_group(i),
				get_server_type(i));
		}
		else
		{
			if (is_server_open(i))
				say("\t%d) %s %d (%s) [%s] %s", i,
					server_list[i].name,
					server_list[i].port,
					server_list[i].nickname,
					get_server_group(i),
					get_server_type(i));
			else
				say("\t%d) %s %d (was %s) [%s] %s", i,
					server_list[i].name,
					server_list[i].port,
					server_list[i].nickname,
					get_server_group(i),
					get_server_type(i));
		}
	}
}

char *	create_server_list (void)
{
	int	i;
	char	*buffer = NULL;
	size_t	bufclue = 0;

	for (i = 0; i < number_of_servers; i++)
	{
		if (server_list[i].des != -1)
		{
			if (server_list[i].itsname)
				m_sc3cat(&buffer, space, server_list[i].itsname, &bufclue);
			else
				yell("Warning: I don't have server #%d's real"
					"name yet -- using the hostname you "
					"gave me instead", i);
		}
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
#ifdef HAVE_SSL
	int     ssl_connect = FALSE;
#endif

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

		if ((i = parse_server_index(server)) == -1 &&
		    (i = find_in_server_list(server, 0)) == -1)
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
			server_reconnects_to(i, -1);
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
#ifdef HAVE_SSL
			if (my_stricmp(get_server_type(i), "IRC-SSL") == 0)
				ssl_connect=TRUE;
			if (ssl_connect == TRUE)
				server_list[i].enable_ssl = TRUE;
#endif

			server_reconnects_to(j, i);
			reconnect(j, 0);
			window_check_servers();
		}
		else
			say("Connected to port %d of server %s",
				server_list[j].port, server_list[j].name);
	}
}


/* SERVER INPUT STUFF */
/*
 * do_server: check the given fd_set against the currently open servers in
 * the server list.  If one have information available to be read, it is read
 * and and parsed appropriately.  If an EOF is detected from an open server,
 * we call reconnect() to try to keep that server connection alive.
 */
void	do_server (fd_set *rd)
{
	char	buffer[IO_BUFFER_SIZE + 1];
	int	des,
		i;

	for (i = 0; i < number_of_servers; i++)
	{
		int	junk;
		char 	*bufptr = buffer;

		des = server_list[i].des;
		if (des == -1 || !FD_ISSET(des, rd))
		{
#ifdef HAVE_SSL
			if (server_list[i].ssl_enabled == TRUE)
			{
				if (!SSL_pending(server_list[i].ssl_fd))
					continue;
			}
			else
#endif
 			continue;
		}
		FD_CLR(des, rd);	/* Make sure it never comes up again */

		last_server = from_server = i;
#ifdef HAVE_SSL
		if (server_list[i].ssl_enabled == TRUE)
			junk = SSL_dgets(bufptr, des, 1, IO_BUFFER_SIZE, server_list[i].ssl_fd);
		else
#endif
		junk = dgets(bufptr, des, 1);

		switch (junk)
 		{
			case 0:		/* Sit on incomplete lines */
				break;

			case -1:	/* EOF or other error */
			{
				if (server_list[i].save_channels == -1)
					save_server_channels(i);

				server_is_connected(i, 0);
				close_server(i, NULL);
				say("Connection closed from %s: %s", 
					server_list[i].name,
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
						server_list[i].des, buffer);

				if (translation)
					translate_from_server(buffer);
				parsing_server_index = i;
				parse_server(buffer);
				parsing_server_index = -1;
				message_from(NULL, LOG_CRAP);
				break;
			}
		}
		from_server = primary_server;
	}

	/* Make sure primary_server is legit before we leave */
	if (primary_server == -1 || !is_server_connected(primary_server))
		window_check_servers();
}


/* SERVER OUTPUT STUFF */
static void 	vsend_to_server (const char *format, va_list args);
void		send_to_server_raw (size_t len, const char *buffer);

void	send_to_aserver (int refnum, const char *format, ...)
{
	int old_from_server = from_server;
	va_list args;

	from_server = refnum;
	va_start(args, format);
	vsend_to_server(format, args);
	va_end(args);
	from_server = old_from_server;
}

void	send_to_server (const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vsend_to_server(format, args);
	va_end(args);
}

/* send_to_server: sends the given info the the server */
static void 	vsend_to_server (const char *format, va_list args)
{
	char	buffer[BIG_BUFFER_SIZE * 3 + 1]; /* make this buffer *much*
						  * bigger than needed */
	size_t	size = BIG_BUFFER_SIZE * 3;
	int	len,
		des;
	int	server;

	if ((server = from_server) == -1)
		server = primary_server;

	if (server != -1 && (des = server_list[server].des) != -1 && format)
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

		server_list[server].sent = 1;
		if (len > (IRCD_BUFFER_SIZE - 2) || len == -1)
			buffer[IRCD_BUFFER_SIZE - 2] = 0;
		if (x_debug & DEBUG_OUTBOUND)
			yell("[%d] -> [%s]", des, buffer);
		strmcat(buffer, "\r\n", IRCD_BUFFER_SIZE);
		if (do_hook(SEND_TO_SERVER_LIST, "%d %d %s", 
				server, des, buffer))
		{
			send_to_server_raw (strlen(buffer), buffer);
		}
	}
	else if (from_server == -1)
        {
	    if (do_hook(DISCONNECT_LIST,"No Connection to %d", server))
		say("You are not connected to a server, "
			"use /SERVER to connect.");
        }
}

void	send_to_server_raw (size_t len, const char *buffer)
{
	int des, server;
	int err = 0;

	if ((server = from_server) == -1)
		server = primary_server;

	if (server != -1 && (des = server_list[server].des) != -1 && buffer)
	{
#ifdef HAVE_SSL
		if (server_list[server].ssl_enabled == TRUE)
		{
			if (server_list[server].ssl_fd == 0)
			{
				say("SSL write error - ssl socket=0");
				return;
			}
			err = SSL_write(server_list[server].ssl_fd, buffer, strlen(buffer));
			BIO_flush(SSL_get_wbio(server_list[server].ssl_fd));
		}
		else
#endif 
		err = write(des, buffer, strlen(buffer));

		if (err == -1 &&
			(!get_int_var(NO_FAIL_DISCONNECT_VAR)))
		{
			if (server_list[server].connected)
			{
				/* server_list[server].save_channels == 1; */
				/* close_server(server, strerror(errno));  */
				server_is_connected(des, 0);
				say("Write to server failed.  Closing connection.");
#ifdef HAVE_SSL
				if (server_list[server].ssl_enabled == TRUE)
					SSL_shutdown(server_list[server].ssl_fd);
#endif
				reconnect(server, 1);
			}
		}
	}
}

void	clear_sent_to_server (int servnum)
{
	server_list[servnum].sent = 0;
}

int	sent_to_server (int servnum)
{
	return server_list[servnum].sent;
}

void	flush_server (int servnum)
{
	if (!is_server_connected(servnum))
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
	int 			des;
	struct sockaddr_in *	localaddr;
	struct sockaddr_in *	remaddr;
	int			len;
	unsigned short		this_sucks;
	Server 			*s;

	/*
	 * Can't connect to refnum -1, this is definitely an error.
	 */
	if (new_server < 0)
	{
		say("Connecting to refnum %d.  That makes no sense.", 
			new_server);
		return -1;		/* XXXX */
	}

	s = &server_list[new_server];

	/*
	 * If we are already connected to the new server, go with that.
	 */
	if (s->des != -1)
	{
		say("Connected to port %d of server %s", s->port, s->name);
		from_server = new_server;
		return -2;		/* Server is already connected */
	}

	/*
	 * Make an attempt to connect to the new server.
	 */
	say("Connecting to port %d of server %s [refnum %d]", 
			s->port, s->name, new_server);

	s->closing = 0;
	oper_command = 0;
	errno = 0;
	localaddr = &s->local_sockname;
	remaddr = &s->remote_sockname;
	memset(localaddr, 0, sizeof(*localaddr));
	memset(remaddr, 0, sizeof(*remaddr));

	this_sucks = (unsigned short)s->port;
	des = connect_by_number(s->name, &this_sucks,
					SERVICE_CLIENT, PROTOCOL_TCP);
	s->port = this_sucks;

	if (des < 0)
	{
		if (x_debug & DEBUG_SERVER_CONNECT)
			say("new_des is %d", des);

		say("Unable to connect to port %d of server %s: %s", 
				s->port, s->name, my_strerror(errno));
#ifdef HAVE_SSL
		s->ssl_enabled = FALSE; /* Would cause client to crash, if not wiped out */
#endif
		return -1;		/* Connect failed */
	}

	from_server = new_server;	/* XXX sigh */
	new_open(des);

	if (*s->name != '/')
	{
		len = sizeof(*localaddr);
		getsockname(des, (struct sockaddr *)localaddr, &len);
		s->local_addr = localaddr->sin_addr;

		len = sizeof(*remaddr);
		getpeername(des, (struct sockaddr *)remaddr, &len);
	}


	/*
	 * Initialize all of the server_list data items
	 * XXX - Calling add_to_server_list is a hack.
	 */
	add_to_server_list(s->name, s->port, NULL, NULL, NULL, NULL, 1);
	s->des = des;
	s->oper = 0;
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
 * If 'old_server' is -1, then this is the first connection for the entire
 * 	client.  We have to handle this differently because no windows exist
 *	at this point and we have to be careful not to call 
 *	window_check_servers.
 * If 'old_server' is -2, then this is a new server connection on an existing
 * 	window.  This is used when a window "splits off" from an existing
 *	server connection to a new server.
 */
int 	connect_to_new_server (int new_server, int old_server, int new_conn)
{
	int	x;
	int	old;

	/*
	 * First of all, if we can't connect to the new server, we don't
	 * do anything here.  Note that this might succeed because we are
	 * already connected to the new server, in which case 
	 * 'is_server_connected' should be true.
	 */
	if ((x = connect_to_server(new_server)) == -1)
	{
		if (old_server >= 0 && server_list[old_server].des != -1)
			say("Connection to server %s resumed...", 
				server_list[old_server].name);

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
		register_server(new_server, server_list[new_server].d_nickname);

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
		if (x == -2)
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
			save_server_channels(old_server);
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
		if (x == -2)
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
		 * If 'new_conn' is 0, and 'old_server' is -1, then we're
		 * doing something like a /server in a disconnected window.
		 * That's no sweat.  We will just try to figure out what
		 * server this window was last connected to, and move all
		 * of those windows over to this new server.
		 */
		if (old_server == -1)
		{
			if (!(old = get_window_oldserver(0)) != -1)
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
 * behaves.  Setting the reconnecting server to -1 inhibits any connection
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
	int	newserv;
	int	i, j;
	int	was_connected = 0;
	int	connected;
	int	max_reconnects = get_int_var(MAX_RECONNECTS_VAR);

	if (!server_list)
	{
                if (do_hook(DISCONNECT_LIST, "No Server List"))
		    say("No server list. Use /SERVER to connect to a server");
		return -1;
	}

	if (oldserv >= 0 && oldserv < number_of_servers)
	{
		was_connected = is_server_connected(oldserv);
		newserv = server_list[oldserv].reconnect_to;

		/*
		 * Inhibit automatic reconnections.
		 */
		if (!was_connected && newserv == oldserv &&
				get_int_var(AUTO_RECONNECT_VAR) == 0)
			newserv = -1;

		if (newserv != -1)
			save_server_channels(oldserv);
		else
			dont_save_server_channels(oldserv);

		if (newserv == oldserv || newserv == -1)
			close_server(oldserv, get_server_quit_message(oldserv));
		connected = is_server_connected(oldserv);
	}
	else
	{
		newserv = reconnects_to_hint;
		connected = 0;
	}

	if (newserv < 0)
	{
		window_check_servers();
		return -1;		/* User wants to disconnect */
	}

	/* Try all of the other servers, stop when one of them works. */
	for (i = 0; i < number_of_servers; i++)
	{
		j = (i + newserv) % number_of_servers;
		if (samegroup && oldserv >= 0 &&
			my_stricmp(get_server_group(oldserv), 
				   get_server_group(j)))
			continue;
		if (newserv != oldserv && j == oldserv)
			continue;
		if ((server_list[j].reconnects++) > max_reconnects) {
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
		if (connected)
			break;
	}

	/* If we reach this point, we have failed.  Time to punt */

	/*
	 * If our prior state was connected, revert back to the prior
	 * connected server.
	 */
	if (connected && newserv != oldserv)
	{
		say("A new server connection could not be established.");
		say("Your previous server connection will be resumed.");
		from_server = oldserv;
		window_check_servers();
		return -1;
	}

	/*
	 * In any situation, if 'oldserv' is not connected at this point, 
	 * then we need to throw away it's channels.
	 */
	if (!is_server_connected(oldserv))
	{
		destroy_waiting_channels(oldserv);
		destroy_server_channels(oldserv);
	}

	/*
	 * Our prior state was unconnected.  Tell the user
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
		server_reconnects_to(i, -1);
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
void	close_server (int old, const char *message)
{
	int	was_connected;

	/* Make sure server refnum is valid */
	if (old < 0 || old >= number_of_servers)
	{
		yell("Closing server [%d] makes no sense!", old);
		return;
	}

	was_connected = server_list[old].connected;

	clean_server_queues(old);
	if (waiting_out > waiting_in)		/* XXX - hack! */
		waiting_out = waiting_in = 0;

	if (server_list[old].save_channels == 1)
		save_channels(old);
	else if (server_list[old].save_channels == 0)
	{
		destroy_waiting_channels(old);
		destroy_server_channels(old);
	}
	else
		panic("Somebody forgot to set "
			"server_list[%d].save_channels!", old);

	server_list[old].save_channels = -1;
	server_list[old].oper = 0;
	server_list[old].registration_pending = 0;
	server_list[old].connected = 0;
	server_list[old].rejoined_channels = 0;
	new_free(&server_list[old].nickname);
	new_free(&server_list[old].s_nickname);

	if (server_list[old].des != -1)
	{
		if (message && *message && !server_list[old].closing)
		{
		    server_list[old].closing = 1;
		    if (x_debug & DEBUG_OUTBOUND)
			yell("Closing server %d because [%s]", 
			   old, message ? message : empty_string);

		    /*
		     * Only tell the server we are leaving if we are 
		     * registered.  This avoids an infinite loop in the
		     * D-line case.
		     */
		    if (was_connected)
			    send_to_aserver(old, "QUIT :%s\n", message);
#ifdef HAVE_SSL
		    if (server_list[old].ssl_enabled == TRUE)
		    {
			    say("Closing SSL connection");
			    SSL_shutdown(server_list[old].ssl_fd);
		    }
#endif
		    do_hook(SERVER_LOST_LIST, "%d %s %s", 
				old, server_list[old].name, message);
		}

		server_list[old].des = new_close(server_list[old].des);
	}

	return;
}

void 	save_server_channels (int servnum)
{
	server_list[servnum].save_channels = 1;
}

void	dont_save_server_channels (int servnum)
{
	server_list[servnum].save_channels = 0;
}


/********************* OTHER STUFF ************************************/

/* AWAY STATUS */
/*
 * Encapsulates everything we need to change our AWAY status.
 * This improves greatly on having everyone peek into that member.
 * Also, we can deal centrally with someone changing their AWAY
 * message for a server when we're not connected to that server
 * (when we do connect, then we send out the AWAY command.)
 * All this saves a lot of headaches and crashes.
 */
void	set_server_away (int servnum, const char *message)
{
	if (servnum == -1)
		say("You are not connected to a server.");

	else if (message && *message)
	{
		if (server_list[servnum].away != message)
			malloc_strcpy(&server_list[servnum].away, message);
		if (server_list[servnum].connected)
			send_to_aserver(servnum, "AWAY :%s", message);
	}
	else
	{
		new_free(&server_list[servnum].away);
		if (server_list[servnum].connected)
			send_to_aserver(servnum, "AWAY :");
	}
}

const char *	get_server_away (int gsa_index)
{
	if (gsa_index == -2)
	{
		int	i;
		for (i = 0; i < number_of_servers; i++)
		{
			if (server_list[i].connected && server_list[i].away)
				return server_list[i].away;
		}
		return 0;
	}
	if (gsa_index < 0 || gsa_index >= number_of_servers)
		return NULL;
	
	return server_list[gsa_index].away;
}


/* USER MODES */
static char *do_umode (int du_index)
{
	char *c = server_list[du_index].umode;
	long flags = server_list[du_index].flags;
	long flags2 = server_list[du_index].flags2;
	int i;

	for (i = 0; server_list[du_index].umodes[i]; i++)
	{
		if (i > 31)
		{
			if (flags2 & (0x1 << (i - 32)))
				*c++ = server_list[du_index].umodes[i];
		}
		else
		{
			if (flags & (0x1 << i))
				*c++ = server_list[du_index].umodes[i];
		}
	}

	*c = '\0';
	return server_list[du_index].umode;
}

const char *	get_possible_umodes (int gu_index)
{
	if (gu_index == -1)
		gu_index = primary_server;
	else if (gu_index >= number_of_servers)
		return empty_string;

	return server_list[gu_index].umodes;
}

const char *	get_umode (int gu_index)
{
	if (gu_index == -1)
		gu_index = primary_server;
	else if (gu_index >= number_of_servers)
		return empty_string;

	return server_list[gu_index].umode;
}

void 	clear_user_modes (int gindex)
{
	if (gindex == -1)
		gindex = primary_server;
	else if (gindex >= number_of_servers)
		return;

	server_list[gindex].flags = 0;
	server_list[gindex].flags2 = 0;
	do_umode(gindex);
}

void	set_server_flag (int ssf_index, int flag, int value)
{
	if (ssf_index == -1)
		ssf_index = primary_server;
	else if (ssf_index >= number_of_servers)
		return;

	if (flag > 31)
	{
		if (value)
			server_list[ssf_index].flags2 |= 0x1 << (flag - 32);
		else
			server_list[ssf_index].flags2 &= ~(0x1 << (flag - 32));
	}
	else
	{
		if (value)
			server_list[ssf_index].flags |= 0x1 << flag;
		else
			server_list[ssf_index].flags &= ~(0x1 << flag);
	}

	do_umode(ssf_index);
}

int	get_server_flag (int gsf_index, int value)
{
	if (gsf_index == -1)
		gsf_index = primary_server;
	else if (gsf_index >= number_of_servers)
		return 0;

	if (value > 31)
		return server_list[gsf_index].flags2 & (0x1 << (value - 32));
	else
		return server_list[gsf_index].flags & (0x1 << value);
}

/* SERVER VERSIONS */
/*
 * set_server_version: Sets the server version for the given server type.  A
 * zero version means pre 2.6, a one version means 2.6 aso. (look server.h
 * for typedef)
 */
void	set_server_version (int ssv_index, int version)
{
	if (ssv_index == -1)
		ssv_index = primary_server;
	else if (ssv_index >= number_of_servers)
		return;
	server_list[ssv_index].version = version;
}

/*
 * get_server_version: returns the server version value for the given server
 * index 
 */
int	get_server_version (int gsv_index)
{
	if (gsv_index == -1)
		gsv_index = primary_server;
	else if (gsv_index >= number_of_servers)
		return 0;

	return (server_list[gsv_index].version);
}

/* SERVER NAMES */
/* get_server_name: returns the name for the given server index */
const char	*get_server_name (int gsn_index)
{
	if (gsn_index == -1)
		gsn_index = primary_server;
	else if (gsn_index == -1 || gsn_index >= number_of_servers)
		return empty_string;

	return (server_list[gsn_index].name);
}

/* get_server_itsname: returns the server's idea of its name */
const char	*get_server_itsname (int gsi_index)
{
	if (gsi_index==-1)
		gsi_index=primary_server;
	else if (gsi_index >= number_of_servers)
		return empty_string;

	/* better check gsi_index for -1 here CDE */
	if (gsi_index == -1)
		return empty_string;

	if (server_list[gsi_index].itsname)
		return server_list[gsi_index].itsname;
	else
		return server_list[gsi_index].name;
}

void	set_server_name (int ssi_index, const char *name)
{
	if (ssi_index==-1)
		ssi_index=primary_server;
	else if (ssi_index >= number_of_servers)
		return;

	malloc_strcpy(&server_list[ssi_index].name, name);
}

void	set_server_itsname (int ssi_index, const char *name)
{
	if (ssi_index==-1)
		ssi_index=primary_server;
	else if (ssi_index >= number_of_servers)
		return;

	malloc_strcpy(&server_list[ssi_index].itsname, name);
}

void	set_server_version_string (int servnum, const char *ver)
{
	malloc_strcpy(&server_list[servnum].version_string, ver);
}

const char *	get_server_version_string (int servnum)
{
	 return server_list[servnum].version_string;
}

#ifdef HAVE_SSL

int	get_server_enable_ssl (int gsn_index)
{
	return server_list[gsn_index].enable_ssl;
}

void	set_server_enable_ssl (int gsn_index, int value)
{
	server_list[gsn_index].enable_ssl = value;
}

/* get_server_isssl: returns 1 if the server is using SSL connection */
int	get_server_isssl (int gsn_index)
{
	if (gsn_index == -1)
		gsn_index = primary_server;
	else if (gsn_index == -1 || gsn_index >= number_of_servers)
		return 0;

	return ((server_list[gsn_index].ssl_enabled == TRUE) ? (1) : (0));
}

const char	*get_server_cipher (int gsn_index)
{
	if (gsn_index == -1)
		gsn_index = primary_server;
	else if (gsn_index == -1 || gsn_index >= number_of_servers
		|| server_list[gsn_index].ssl_enabled == FALSE)
		return empty_string;

	return (SSL_get_cipher(server_list[gsn_index].ssl_fd));
}

#else

/* get_server_isssl: returns 1 if the server is using SSL connection */
int	get_server_isssl (int gsn_index)
{
	return 0;
}
#endif

/* CONNECTION/REGISTRATION STATUS */
void	register_server (int ssn_index, const char *nickname)
{
#ifdef HAVE_SSL
	int		alg;
	int		sign_alg;
	X509		*server_cert;
	EVP_PKEY	*server_pkey;
#endif;
	if (server_list[ssn_index].registration_pending)
		return;		/* Whatever */

	if (server_list[ssn_index].connected)
		return;		/* Whatever */

	server_list[ssn_index].registration_pending = 1;
#ifdef HAVE_SSL
	if (server_list[ssn_index].enable_ssl == TRUE)
	{
		say("SSL negotiation in progress...");
/* Old SSL connection routines.
		SSLeay_add_ssl_algorithms();
		SSL_load_error_strings();
		server_list[ssn_index].ctx = SSL_CTX_new(SSLv3_client_method());
		server_list[ssn_index].ssl_fd = SSL_new(server_list[ssn_index].ctx);
		SSL_set_fd(server_list[ssn_index].ssl_fd, server_list[ssn_index].des);
		SSL_connect(server_list[ssn_index].ssl_fd);
*/		
		/* Set up SSL connection */
		server_list[ssn_index].ctx = SSL_CTX_init(0);
		server_list[ssn_index].ssl_fd = SSL_FD_init(server_list[ssn_index].ctx,
			server_list[ssn_index].des);

		if (x_debug & DEBUG_SSL)
			say("SSL negotiation using %s",
				get_server_cipher(ssn_index));
		say("SSL negotiation on port %d of server %s complete",
			server_list[ssn_index].port, get_server_name(ssn_index));
		server_cert = SSL_get_peer_certificate(server_list[ssn_index].ssl_fd);
		if (!(server_cert == NULL))
		{
			server_list[ssn_index].ssl_enabled = TRUE;
			server_pkey = X509_get_pubkey(server_cert);
			/* urlencoded to avoid problems with spaces */
			do_hook(SSL_SERVER_CERT_LIST, "%s %s %s",
				server_list[ssn_index].name,
				urlencode(X509_NAME_oneline(X509_get_subject_name(server_cert),0,0)),
				urlencode(X509_NAME_oneline(X509_get_issuer_name(server_cert),0,0)));

			/* We should check certificate date validity here. */

			if (x_debug & DEBUG_SSL)
			{
				alg = OBJ_obj2nid(server_cert->cert_info->key->algor->algorithm);
				sign_alg = OBJ_obj2nid(server_cert->sig_alg->algorithm);		
				say("Public key algorithm: %s (%d bits)  Sign algorithm: %s",
					(alg == NID_undef) ? "UNKNOWN" : OBJ_nid2ln(alg), EVP_PKEY_bits(server_pkey),
					(sign_alg == NID_undef) ? "UNKNOWN" : OBJ_nid2ln(sign_alg));
			}
			EVP_PKEY_free(server_pkey);
		}
		else
		{
			server_list[ssn_index].ssl_enabled = FALSE;
			/* No server certificate found */
			do_hook(SSL_SERVER_CERT_LIST, "%s %s %s",
				server_list[ssn_index].name,
				empty_string, empty_string);
		}
		X509_free(server_cert);
	}
#endif
	if (server_list[ssn_index].password)
		send_to_aserver(ssn_index, "PASS %s", 
			server_list[ssn_index].password);

	send_to_aserver(ssn_index, "USER %s %s %s :%s", username, 
			(send_umode && *send_umode) ? send_umode : 
			(LocalHostName ? LocalHostName : hostname), 
			username, (*realname ? realname : space));
	change_server_nickname(ssn_index, nickname);
}

/*
 * password_sendline: called by send_line() in get_password() to handle
 * hitting of the return key, etc 
 * -- Callback function
 */
void 	password_sendline (char *data, char *line)
{
	int	new_server;

	if (line && *line)
	{
		new_server = atoi(data);
		set_server_password(new_server, line);
		change_window_server(new_server, new_server);
		server_reconnects_to(new_server, new_server);
		reconnect(new_server, 1);
	}
}

char *	get_server_password (int refnum)
{
	return server_list[refnum].password;
}

/*
 * set_server_password: this sets the password for the server with the given
 * index.  If password is null, the password for the given server is returned 
 */
char	*set_server_password (int ssp_index, const char *password)
{
	if (server_list)
	{
		if (password)
		   malloc_strcpy(&(server_list[ssp_index].password), password);
		return (server_list[ssp_index].password);
	}
	else
		return ((char *) 0);
}


/*
 * is_server_open: Returns true if the given server index represents a server
 * with a live connection, returns false otherwise 
 */
int	is_server_open (int iso_index)
{
	if (iso_index < 0 || iso_index >= number_of_servers) 
		return (0);
	return (server_list[iso_index].des != -1);
}

/*
 * is_server_connected: returns true if the given server is connected.  This
 * means that both the tcp connection is open and the user is properly
 * registered 
 */
int	is_server_connected (int isc_index)
{
	if (isc_index >= 0 && isc_index < number_of_servers)
		return (server_list[isc_index].connected);
	return 0;
}

/*
 * Informs the client that the user is now officially registered or not
 * registered on the specified server.
 */
void 	server_is_connected (int sic_index, int value)
{
	if (sic_index < 0 || sic_index >= number_of_servers)
		return;

	server_list[sic_index].connected = value;
	server_list[sic_index].registration_pending = 0;
	server_list[sic_index].rejoined_channels = 0;
	if (value)
	{
		/* 
		 * By default, we want to save the server's channels.
		 * If anything happens where we don't want to do this,
		 * then we must turn it off, rather than the other way
		 * around.
		 */
		save_server_channels(sic_index);
		server_list[sic_index].reconnect_to = sic_index;
		server_list[sic_index].eof = 0;
		clear_reconnect_counts();
	}
}

void	server_did_rejoin_channels (int sic_index)
{
	if (sic_index < 0 || sic_index >= number_of_servers)
		return;
	server_list[sic_index].rejoined_channels = 1;
}

int	did_server_rejoin_channels (int sic_index)
{
	if (sic_index < 0 || sic_index >= number_of_servers)
		return 0;
	if (server_list[sic_index].connected == 0)
		return 0;
	return server_list[sic_index].rejoined_channels;
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
		if ((i = parse_server_index(server)) == -1)
		{
			say("No such server!");
			return;
		}
	}

	if (i >= 0 && i < number_of_servers)
	{
		if (!args || !*args)
			message = "Disconnecting";
		else
			message = args;

		say("Disconnecting from server %s", get_server_itsname(i));
		server_reconnects_to(i, -1);
		dont_save_server_channels(i);
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

	servref = my_atol(stuff);
	new_free((char **)&d);

	server_reconnects_to(servref, servref);
	reconnect(servref, 1);
	return 0;
}

int	server_reconnects_to (int oldref, int newref)
{
	if (oldref == -1)
	{
		reconnects_to_hint = newref;
		return 1;
	}
	if (oldref < 0 || oldref >= number_of_servers)
		return 0;
	if (newref >= number_of_servers)
		newref = 0;
	if (newref < -1)
		return 0;
	server_list[oldref].reconnect_to = newref;
	return 1;
}

int	set_server_quit_message (int servref, const char *message)
{
	if (servref < 0 || servref >= number_of_servers)
		return -1;
	malloc_strcpy(&server_list[servref].quit_message, message);
	return 0;
}

const char *	get_server_quit_message (int servref)
{
	if (servref < 0 || servref >= number_of_servers ||
			server_list[servref].quit_message == NULL)
		return "get_server_quit_message";

	return server_list[servref].quit_message;
}

/* PORTS */
void    set_server_port (int refnum, int port)
{
        server_list[refnum].port = port;
}

/* get_server_port: Returns the connection port for the given server index */
int	get_server_port (int gsp_index)
{
	if (gsp_index == -1)
		gsp_index = primary_server;
	else if (gsp_index >= number_of_servers)
		return 0;

	return (server_list[gsp_index].port);
}

int	get_server_local_port (int gsp_index)
{
	if (gsp_index == -1)
		gsp_index = primary_server;
	else if (gsp_index >= number_of_servers)
		return 0;

	return ntohs(server_list[gsp_index].local_sockname.sin_port);
}

struct in_addr	get_server_local_addr (int servnum)
{
	return server_list[servnum].local_addr;
}

struct	in_addr	get_server_uh_addr (int servnum)
{
	return server_list[servnum].uh_addr;
}

/* USERHOST */
void	set_server_userhost (int refnum, const char *userhost)
{
	char *host;

	if (!(host = strchr(userhost, '@')))
	{
		yell("Cannot set your userhost to [%s] because it does not
		      contain a @ character!", userhost);
		return;
	}

	malloc_strcpy(&server_list[from_server].userhost, userhost);

	/* Ack! */
	if (lame_external_resolv(host + 1, &server_list[from_server].uh_addr))
		yell("Ack.  The server says your userhost is [%s] and "
		     "I can't figure out the IP address of that host! "
		     "You won't be able to use /SET DCC_USE_GATEWAY_ADDR ON "
		     "with this server connection!", host + 1);
}

/*
 * get_server_userhost: return the userhost for this connection to server
 */
const char	*get_server_userhost (int gsu_index)
{
	if (gsu_index >= number_of_servers)
		return empty_string;
	else if (gsu_index != -1 && server_list[gsu_index].userhost)
		return (server_list[gsu_index].userhost);
	else
		return get_userhost();
}

/*
 * got_my_userhost -- callback function, XXXX doesnt belong here
 */
void 	got_my_userhost (UserhostItem *item, char *nick, char *stuff)
{
	char *freeme;

	freeme = m_3dup(item->user, "@", item->host);
	set_server_userhost(from_server, freeme);
	new_free(&freeme);
}



/* SERVER OPERATOR */
/*
 * get_server_operator: returns true if the user has op privs on the server,
 * false otherwise 
 */
int	get_server_operator (int gso_index)
{
	if ((gso_index < 0) || (gso_index >= number_of_servers))
		return 0;
	return (server_list[gso_index].oper);
}

/*
 * set_server_operator: If flag is non-zero, marks the user as having op
 * privs on the given server.  
 */
void	set_server_operator (int sso_index, int flag)
{
	if (sso_index < 0 || sso_index >= number_of_servers)
		return;

	server_list[sso_index].oper = flag;
	oper_command = 0;		/* No longer doing oper */
	do_umode(sso_index);
}



/* COOKIES */
void	set_server_cookie (int ssc_index, const char *cookie)
{
	if (server_list[ssc_index].cookie)
		send_to_aserver(ssc_index, "COOKIE %s", 
				server_list[ssc_index].cookie);
	malloc_strcpy(&server_list[ssc_index].cookie, cookie);
}

char *  get_server_cookie (int ssc_index)
{
        return server_list[ssc_index].cookie;
}

/* MOTD */
void 	set_server_motd (int ssm_index, int flag)
{
	if (ssm_index != -1 && ssm_index < number_of_servers)
		server_list[ssm_index].motd = flag;
}

int 	get_server_motd (int gsm_index)
{
	if (gsm_index != -1 && gsm_index < number_of_servers)
		return(server_list[gsm_index].motd);
	return (0);
}



/* NICKNAMES */
/*
 * get_server_nickname: returns the current nickname for the given server
 * index 
 */
const char	*get_server_nickname (int gsn_index)
{
	if (gsn_index >= number_of_servers)
		return empty_string;
	else if (gsn_index != -1 && server_list[gsn_index].nickname)
		return (server_list[gsn_index].nickname);
	else
		return "<not registered yet>";
}

int	is_me (int refnum, const char *nick)
{
	if (refnum == -1 && from_server != -1)
		refnum = from_server;

	if (refnum >= number_of_servers || refnum < 0)
		return 0;

	if (server_list[refnum].nickname && nick)
		return !my_stricmp(nick, server_list[refnum].nickname);

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
void	change_server_nickname (int ssn_index, const char *nick)
{
	Server *s = &server_list[ssn_index];
	char	*n;

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
			reset_nickname(ssn_index);
	}

	if (s->s_nickname)
		send_to_aserver(ssn_index, "NICK %s", s->s_nickname);
}

const char *	get_pending_nickname (int servnum)
{
	return server_list[servnum].s_nickname;
}


void	accept_server_nickname (int ssn_index, const char *nick)
{
	malloc_strcpy(&server_list[ssn_index].nickname, nick);
	malloc_strcpy(&server_list[ssn_index].d_nickname, nick);
	new_free(&server_list[ssn_index].s_nickname);
	server_list[ssn_index].fudge_factor = 0;

	if (ssn_index == primary_server)
		strmcpy(nickname, nick, NICKNAME_LEN);

	update_all_status();
}

void	nick_command_is_pending (int servnum, int value)
{
	server_list[servnum].nickname_pending = value;
}

int	is_nick_command_pending (int servnum)
{
	return server_list[servnum].nickname_pending;
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
void 	fudge_nickname (int servnum)
{
	char 	l_nickname[NICKNAME_LEN + 1];
	Server *s = &server_list[servnum];

	/*
	 * If we got here because the user did a /NICK command, and
	 * the nick they chose doesnt exist, then we just dont do anything,
	 * we just cancel the pending action and give up.
	 */
	if (s->nickname_pending)
	{
		nick_command_is_pending(servnum, 0);
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
			reset_nickname(servnum);
			s->fudge_factor = 0;
			return;
		}
	}

	/* 
	 * Process of fudging a nickname:
	 * If the nickname length is less then 9, add an underscore.
	 */
	if (strlen(l_nickname) < 9)
		strcat(l_nickname, "_");

	/* 
	 * The nickname is 9 characters long. roll the nickname
	 */
	else
	{
		char tmp = l_nickname[8];
		l_nickname[8] = l_nickname[7]; l_nickname[7] = l_nickname[6];
		l_nickname[6] = l_nickname[5]; l_nickname[5] = l_nickname[4];
		l_nickname[4] = l_nickname[3]; l_nickname[3] = l_nickname[2];
		l_nickname[2] = l_nickname[1]; l_nickname[1] = l_nickname[0];
		l_nickname[0] = tmp;
	}

	/*
	 * This is the degenerate case
	 */
	if (!strcmp(l_nickname, "_________"))
	{
		reset_nickname(servnum);
		return;
	}

	change_server_nickname(servnum, l_nickname);
}


/*
 * -- Callback function
 */
void 	nickname_sendline (char *data, char *nick)
{
	int	new_server;

	new_server = atoi(data);
	change_server_nickname(new_server, nick);
}

/*
 * reset_nickname: when the server reports that the selected nickname is not
 * a good one, it gets reset here. 
 * -- Called by more than one place
 */
void 	reset_nickname (int servnum)
{
	char	server_num[10];

	if (server_list[servnum].resetting_nickname == 1)
		return;		/* Don't repeat the reset */

	server_list[servnum].resetting_nickname = 1;
	say("You have specified an invalid nickname");
	if (!dumb_mode)
	{
		say("Please enter your nickname");
		strcpy(server_num, ltoa(servnum));
		add_wait_prompt("Nickname: ", nickname_sendline, server_num,
			WAIT_PROMPT_LINE, 1);
	}
	update_all_status();
}


/* REDIRECT STUFF */
void 	set_server_redirect (int s, const char *who)
{
	malloc_strcpy(&server_list[s].redirect, who);
}

const char *get_server_redirect (int s)
{
	return server_list[s].redirect;
}

int	check_server_redirect (const char *who)
{
	if (!who || !server_list[from_server].redirect)
		return 0;

	if (!strncmp(who, "***", 3) && 
	    !strcmp(who + 3, server_list[from_server].redirect))
	{
		set_server_redirect(from_server, NULL);
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
	int	i;

	if (!server_list)
		return;		/* no servers */

	for (i = 0; i < number_of_servers; i++)
	{
		/* SERVER -ADD server:port:password:nick */
		fprintf(fp, "SERVER -ADD %s:%d:%s:%s:%s:%s\n",
			server_list[i].name,
			server_list[i].port,
			server_list[i].password ? 
				server_list[i].password : empty_string,
			server_list[i].nickname ?
				server_list[i].nickname : empty_string,
			server_list[i].group ?
				server_list[i].group : empty_string,
			get_server_type(i));
	}
}

void	clear_reconnect_counts (void)
{
	int	i;

	for (i = 0; i < number_of_servers; i++)
		server_list[i].reconnects = 0;
}

/* This didn't belong in the middle of the redirect stuff. */
void    set_server_group (int refnum, const char *group)
{
        malloc_strcpy(&server_list[refnum].group, group);
}

const char *get_server_group (int refnum)
{
	if (refnum == -1 && from_server != -1)
		refnum = from_server;

	if (refnum >= number_of_servers || refnum < 0)
		return 0;

	if (server_list[refnum].group == NULL)
		return "<default>";

	return server_list[refnum].group;
}

const char *get_server_type (int refnum)
{
	if (refnum == -1 && from_server != -1)
		refnum = from_server;

	if (refnum >= number_of_servers || refnum < 0)
		return 0;

	if (server_list[refnum].enable_ssl)
		return "IRC-SSL";
	else
		return "IRC";
}


#define EMPTY empty_string
#define RETURN_EMPTY return m_strdup(EMPTY)
#define RETURN_IF_EMPTY(x) if (empty( x )) RETURN_EMPTY
#define GET_INT_ARG(x, y) {RETURN_IF_EMPTY(y); x = my_atol(safe_new_next_arg(y, &y));}
#define GET_FLOAT_ARG(x, y) {RETURN_IF_EMPTY(y); x = atof(safe_new_next_arg(y, &y));}
#define GET_STR_ARG(x, y) {RETURN_IF_EMPTY(y); x = new_next_arg(y, &y);RETURN_IF_EMPTY(x);}
#define RETURN_STR(x) return m_strdup((x) ? (x) : EMPTY)
#define RETURN_INT(x) return m_strdup(ltoa((x)))

/* Used by function_aliasctl */
/*
 * $serverctl(REFNUM server-desc)
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
 */
char 	*serverctl 	(char *input)
{
	int	refnum;
	char *	listc;

	GET_STR_ARG(listc, input);
	if (!my_strnicmp(listc, "REFNUM", 1)) {
		char *server;

		GET_STR_ARG(server, input);
		if (is_number(server)) {
			int refnum;
			refnum = atol(server);
			if (refnum >= 0 && refnum < number_of_servers)
				RETURN_STR(server);
			RETURN_EMPTY;
		}
		RETURN_INT(find_server_refnum(server, &input));
	} else if (!my_strnicmp(listc, "GET", 2)) {
		GET_INT_ARG(refnum, input);
		if (refnum < 0 || refnum >= number_of_servers)
			RETURN_EMPTY;

		GET_STR_ARG(listc, input);
		if (!my_strnicmp(listc, "AWAY", 1)) {
			RETURN_STR(get_server_away(refnum));
		} else if (!my_strnicmp(listc, "CONNECTED", 3)) {
			RETURN_INT(is_server_connected(refnum));
		} else if (!my_strnicmp(listc, "COOKIE", 3)) {
			RETURN_STR(get_server_cookie(refnum));
		} else if (!my_strnicmp(listc, "GROUP", 1)) {
			RETURN_STR(get_server_group(refnum));
		} else if (!my_strnicmp(listc, "ITSNAME", 1)) {
			RETURN_STR(get_server_itsname(refnum));
		} else if (!my_strnicmp(listc, "NAME", 2)) {
			RETURN_STR(get_server_name(refnum));
		} else if (!my_strnicmp(listc, "NICKNAME", 2)) {
			RETURN_STR(get_server_nickname(refnum));
		} else if (!my_strnicmp(listc, "PASSWORD", 2)) {
			RETURN_STR(get_server_password(refnum));
		} else if (!my_strnicmp(listc, "PORT", 2)) {
			RETURN_INT(get_server_port(refnum));
		} else if (!my_strnicmp(listc, "QUIT_MESSAGE", 1)) {
			RETURN_STR(get_server_quit_message(refnum));
		} else if (!my_strnicmp(listc, "SSL", 1)) {
			RETURN_INT(get_server_enable_ssl(refnum));
		} else if (!my_strnicmp(listc, "UMODE", 2)) {
			RETURN_STR(get_umode(refnum));
		} else if (!my_strnicmp(listc, "USERHOST", 2)) {
			RETURN_STR(get_server_userhost(refnum));
		} else if (!my_strnicmp(listc, "VERSION", 1)) {
			RETURN_STR(get_server_version_string(refnum));
		}
	} else if (!my_strnicmp(listc, "SET", 1)) {
		GET_INT_ARG(refnum, input);
		if (refnum < 0 || refnum >= number_of_servers)
			RETURN_EMPTY;

		GET_STR_ARG(listc, input);
		if (!my_strnicmp(listc, "AWAY", 1)) {
			set_server_away(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "CONNECTED", 3)) {
			RETURN_EMPTY;		/* Read only. */
		} else if (!my_strnicmp(listc, "COOKIE", 3)) {
			set_server_cookie(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "GROUP", 1)) {
			set_server_group(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "ITSNAME", 1)) {
			set_server_itsname(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "NAME", 2)) {
			set_server_name(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "NICKNAME", 2)) {
			change_server_nickname(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "PASSWORD", 2)) {
			set_server_password(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "PORT", 2)) {
			int port;

			GET_INT_ARG(port, input);
			set_server_port(refnum, port);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "QUIT_MESSAGE", 1)) {
			set_server_quit_message(refnum, input);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "SSL", 1)) {
			int value;

			GET_INT_ARG(value, input);
			set_server_enable_ssl(refnum, value);
			RETURN_INT(1);
		} else if (!my_strnicmp(listc, "UMODE", 2)) {
			RETURN_EMPTY;		/* Read only for now */
		} else if (!my_strnicmp(listc, "USERHOST", 2)) {
			set_server_userhost(refnum, input);
		} else if (!my_strnicmp(listc, "VERSION", 1)) {
			set_server_version_string(refnum, input);
		}
	} else if (!my_strnicmp(listc, "MATCH", 1)) {
		RETURN_EMPTY;		/* Not implemented for now. */
	} else if (!my_strnicmp(listc, "PMATCH", 1)) {
		RETURN_EMPTY;		/* Not implemented for now. */
	} else if (!my_strnicmp(listc, "GMATCH", 2)) {
		int	i;
		char *retval = NULL;

		for (i = 0; i < number_of_servers; i++) {
			if (!my_stricmp(get_server_group(i), input))
				m_s3cat(&retval, space, ltoa(i));
		}
		RETURN_STR(retval);
	} else
		RETURN_EMPTY;

	RETURN_EMPTY;
}
