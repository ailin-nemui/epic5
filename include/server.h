/*
 * server.h: header for server.c 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __server_h__
#define __server_h__
  

/* To get definition of Who, Ison, and Userhost queues. */
#include "who.h"

#ifdef NEED_SERVER_LIST
/* To get definition of Notify */
#include "notify.h"

#ifdef HAVE_SSL
#include "ssl.h"
#endif

/* XXXX Ick.  Gross.  Bad. XXX */
struct notify_stru;

/* Server: a structure for the server_list */
typedef	struct
{
	char	*name;			/* the name of the server */
	char	*itsname;		/* the server's idea of its name */
	char	*password;		/* password for that server */
	int	port;			/* port number on that server */
	char	*group;			/* Server group it belongs to */
	char	*nickname;		/* Authoritative nickname for us */
	char	*s_nickname;		/* last NICK command sent */
	char	*d_nickname;		/* Default nickname to use */
	int	fudge_factor;		/* How much s_nickname's fudged */
	int	nickname_pending;	/* Is a NICK command pending? */
	int	resetting_nickname;	/* Is a nickname reset in progress? */
	int	registration_pending;	/* Is a registration going on ? */
	int	rejoined_channels;	/* Has we tried to auto-rejoin? */
	char	*userhost;		/* my userhost on this server */
	char	*away;			/* away message for this server */
	int	oper;			/* true if operator */
	int	version;		/* the version of the server -
					 * defined above */
	int	server2_8;		/* defined if we get an 001 numeric */
	char	*version_string;	/* what is says */
	long	flags;			/* Various flags */
	long	flags2;			/* More Various flags */
	char	*umodes;		/* Possible user modes */
	char	umode[54];		/* Currently set user modes */
	int	s_takes_arg;		/* Set to 1 if s user mode has arg */
	int	connected;		/* true if connection is assured */
	int	des;			/* file descriptor to server */
	int	eof;			/* eof flag for server */
	int	motd;			/* motd flag (used in notice.c) */
	int	sent;			/* set if something has been sent,
					 * used for redirect */
	char	*redirect;		/* Who we're redirecting to here */
	WhoEntry *	who_queue;	/* Who queue */
	IsonEntry *	ison_queue;	/* Ison queue */
	UserhostEntry *	userhost_queue;	/* Userhost queue */

	SS	local_sockname; 	/* sockname of this connection */
	SS	remote_sockname; 	/* sockname of this connection */
	SS	uh_addr;		/* ip address the server sees */
	NotifyList	notify_list;	/* Notify list for this server */
	int	reconnects;		/* Number of reconnects done */
	char 	*cookie;		/* Erf/TS4 "cookie" value */
	int	save_channels;		/* True if abnormal connection */
	int	closing;		/* True if close_server called */
	int	reconnect_to;		/* Server to connect to on EOF */
	char	*quit_message;		/* Where we stash a quit message */
#ifdef HAVE_SSL
	SSL_CTX*	ctx;
	SSL_METHOD*	meth;
	int	enable_ssl;		/* SSL requested on next connection. */
	int	ssl_enabled;		/* Current SSL status. */
	SSL*	ssl_fd;
#endif
}	Server;
extern	Server	*server_list;
#endif	/* NEED_SERVER_LIST */

extern	int	number_of_servers;
extern	int	connected_to_server;
extern	int	never_connected;
extern	int	primary_server;
extern	int	from_server;
extern	int	last_server;
extern	int	parsing_server_index;

/*
 * type definition to distinguish different
 * server versions
 */
#define Server2_8	1
#define Server2_9	2
#define Server2_10	3
#define Server_u2_8	4
#define Server_u2_9	5
#define Server_u2_10	6
#define Server_u3_0	7



	BUILT_IN_COMMAND(servercmd);
	BUILT_IN_COMMAND(disconnectcmd);

	void	add_to_server_list 		(const char *, int, 
						 const char *, const char *, 
						 const char *, const char *,
						 int);
	int	find_in_server_list		(const char *, int);
	int	find_server_refnum		(char *, char **rest);
	int	parse_server_index		(const char *);
	void	parse_server_info		(char *, char **, char **,
						 char **, char **, char **);
	void	build_server_list		(char *, char *);
	int	read_server_file		(void);
	void	display_server_list		(void);
	char *	create_server_list		(void);	/* MALLOC */
	int	server_list_size		(void);

	void	do_server 			(fd_set *);
	void	flush_server			(int);
	void	send_to_aserver			(int, const char *, ...) __A(2);
	void	send_to_server			(const char *, ...) __A(1);
	void	send_to_server_raw		(size_t len, const char *buffer);
	void	clear_sent_to_server		(int);
	int	sent_to_server			(int);

	int	connect_to_new_server		(int, int, int);
	int	close_all_servers		(const char *);
	void	close_server			(int, const char *);
	void	save_server_channels		(int);
	void	dont_save_server_channels	(int);

	void	set_server_away			(int, const char *);
const	char *	get_server_away			(int);

const	char *	get_possible_umodes		(int);
const	char *	get_umode			(int);
	void	clear_user_modes		(int);
	void	set_server_flag			(int, int, int);
	int	get_server_flag			(int, int);

	void	set_server_version		(int, int);
	int	get_server_version		(int);

const	char *	get_server_name			(int);
const	char *	get_server_itsname		(int);
const	char *	get_server_group		(int);
const	char *	get_server_type			(int);
	void	set_server_itsname		(int, const char *);
	void	set_server_version_string	(int, const char *);
const 	char *	get_server_version_string	(int);
	int	get_server_isssl		(int);
const	char *	get_server_cipher		(int);
 
	void	register_server			(int, const char *);
	void	server_registration_is_not_pending (int);
	void	password_sendline		(char *, char *);
	char *	set_server_password		(int, const char *);
	int	is_server_open			(int);
	int	is_server_connected		(int);
	void	server_is_connected		(int, int);
	int	auto_reconnect_callback		(void *);
	int	server_reconnects_to		(int, int);
	int	reconnect			(int, int);

	int	get_server_port			(int);
	int	get_server_local_port		(int);
	IA	get_server_local_addr		(int);
	IA	get_server_uh_addr		(int);

const	char *	get_server_userhost		(int);
	void 	got_my_userhost 		(UserhostItem *, char *, 
						 char *);

	int	get_server_operator		(int);
	void	set_server_operator		(int, int);

	void	set_server_cookie		(int, const char *);

	void	set_server_motd			(int, int);
	int	get_server_motd			(int);

const	char *	get_server_nickname		(int);
	int	is_me				(int, const char *);
	void	change_server_nickname		(int, const char *);
const	char *	get_pending_nickname		(int);
	void	accept_server_nickname		(int, const char *);
	void	fudge_nickname			(int);
	void	nickname_sendline		(char *, char *);
	void	reset_nickname			(int);
	void	nick_command_is_pending		(int, int);
	int	is_nick_command_pending		(int);

	void	set_server_redirect		(int, const char *);
const	char *	get_server_redirect		(int);
	int	check_server_redirect		(const char *);
	void	save_servers			(FILE *);

	void	server_did_rejoin_channels	(int);
	int	did_server_rejoin_channels	(int);
	int     set_server_quit_message 	(int, const char *message);
const char *    get_server_quit_message		(int);

	void	clear_reconnect_counts		(void);

	int	get_server_enable_ssl 		(int);
	void   	set_server_enable_ssl 		(int, int);
#endif /* _SERVER_H_ */
