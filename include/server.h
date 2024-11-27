/*
 * server.h: header for server.c 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997, 2007 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __server_h__
#define __server_h__
  
/* To get definition of Who, Ison, and Userhost queues. */
#include "who.h"

#ifdef NEED_SERVER_LIST
/* To get definition of Notify */
#include "notify.h"
#include "alist.h"

typedef struct
{
	char	*name;
	char	*value;
} A005_item;

typedef struct WaitCmdstru
{
        char    *stuff;
        struct  WaitCmdstru *next;
} WaitCmd;

typedef struct ServerInfo 
{
	int	clean;
        char *  freestr;
	char *	fulldesc;
        int     refnum;
        const char *  host;
        int     port;
        const char *  password;
        const char *  nick;
        const char *  group;
        const char *  server_type;
        const char *  proto_type;
	const char *  vhost;
	const char *  cert;
} ServerInfo;
typedef ServerInfo SI;

/* Server: a structure for the server_list */
typedef	struct
{
	int		des;			/* file descriptor to server (or helper) */
	int		state;			/* See above */

	/* status = CREATED, RECONNECT */
	SI *		info;			/* Canonical information */
	Bucket *	altnames;		/* Alternate handles for the server */

	/* state = DNS */
	AI *		addrs;			/* Returned by getaddrinfo */
const	AI *		next_addr;		/* The next one to try upon failure */
	int		addr_counter;		/* How far we're into "addrs" */
	ssize_t		addr_len;
	ssize_t		addr_offset;

	/* state = CONNECTING */
	SSu		local_sockname; 	/* sockname of this connection */
	SSu		remote_sockname; 	/* sockname of this connection */
	char *		remote_paddr;		/* p-addr of remote_sockname */

	/* state = SSL_CONNECTING */
	int		accept_cert;		/* Whether we accept the SSL certificate */

	/* state = REGISTERING */
	char *		nickname;		/* Authoritative nickname for us */
	char *		s_nickname;		/* last NICK command sent */
	char *		d_nickname;		/* Default nickname to use */
	char *		realname;		/* The actual realname. */
	char *		default_realname;	/* The default realname. */
	int		any_data;		/* SSL servers won't send any data back */

	/* state = SYNCING */


	/* state = ACTIVE */

		/* metadata about the server */
	char *		itsname;		/* the server's idea of its name */
	char *		version_string;		/* what is says */
	alist		a005;			/* 005 settings kept kere. */
	int		stricmp_table;		/* Which case insensitive map to use */
	int		line_length;		/* How long a protocol command may be */
	int		max_cached_chan_size;	/* Bigger channels won't cache U@H */

		/* metadata about us */
	char *		unique_id;		/* Unique ID (for ircnet) */
	char *		cookie;			/* Erf/TS4 "cookie" value */
	SSu		uh_addr;		/* ip address the server sees */
	int		uh_addr_set;		/* 0 or 1, if set_uh_addr() has been called */
						/* Used to guard an annoying error message */
	char		umode[54];		/* Currently set user modes */
	char *		userhost;		/* my userhost on this server */
	char *		away_message;		/* away message for this server */
	int		away_status;		/* whether the server thinks we're away */

		/* metadata about the session */
	int		sent;			/* set if something has been sent, used for redirect */
	char *		quit_message;		/* Where we stash a quit message */
	int		autoclose;		/* Whether the server is closed when
					   	   there are no windows on it */
	char *		redirect;		/* Who we're redirecting to here */

		/* Metadata about activity */
        char *          invite_channel;
        char *          last_notify_nick;
        char *          joined_nick;
        char *          public_nick;
        char *          recv_nick;
        char *          sent_nick;
        char *          sent_body;

		/* /WHO */
	int		who_max;		/* Max pending whos */
	WhoEntry *	who_queue;		/* Who queue */
	int		ison_len;		/* Max ison characters */
	int		ison_max;		/* Max pending isons */
	IsonEntry *	ison_queue;		/* Ison queue */
	IsonEntry *	ison_wait;		/* Ison wait queue */
	int		userhost_max;		/* Max pending userhosts */
	UserhostEntry *	userhost_queue;		/* Userhost queue */
	UserhostEntry *	userhost_wait;		/* Userhost wait queue */

		/* /NOTIFY */
	alist		notify_list;		/* Notify list for this server */
	char *		ison;

		/* /LIST, /NAMES */
	int		funny_min;		/* Funny stuff */
	int		funny_max;
	int		funny_flags;
	char *		funny_match;

		/* /WAIT */
        int             waiting_in;
        int             waiting_out;
        WaitCmd *       start_wait_list;
        WaitCmd *       end_wait_list;

		/* metadata about message processing */
#define DOING_PRIVMSG	1U
#define DOING_NOTICE	2U
#define DOING_CTCP	4U
	unsigned	protocol_metadata;
        int             doing_privmsg;
        int             doing_notice;
        int             doing_ctcp;

}	Server;
extern	Server	**server_list; 

	int    	serverinfo_matches_servref	(ServerInfo *, int);
        int    	clear_serverinfo 		(ServerInfo *s);
        int    	str_to_serverinfo 		(char *str, ServerInfo *s);
	Server *get_server 			(int);

#endif	/* NEED_SERVER_LIST */

	extern	int	number_of_servers;
	extern	int	connected_to_server;
	extern	int	primary_server;
	extern	int	from_server;
	extern	int	last_server;
	extern	int	parsing_server_index;

#define NOSERV		-2
#define FROMSERV	-1

/* Funny stuff */
#define FUNNY_PUBLIC	(1 << 0)
#define FUNNY_PRIVATE	(1 << 1)
#define FUNNY_TOPIC	(1 << 2)
#define FUNNY_USERS	(1 << 4)
#define FUNNY_NAME	(1 << 5)

#define SERVER_CREATED		0
#define SERVER_RECONNECT	1
#define SERVER_DNS		2
#define SERVER_CONNECTING	3
#define SERVER_SSL_CONNECTING	4
#define SERVER_REGISTERING	5
#define SERVER_SYNCING		6
#define SERVER_ACTIVE		7
#define SERVER_EOF		8
#define SERVER_ERROR		9
#define SERVER_CLOSING		10
#define SERVER_CLOSED		11
#define SERVER_DELETED		12


	BUILT_IN_COMMAND(servercmd);
	BUILT_IN_COMMAND(disconnectcmd);
	BUILT_IN_COMMAND(reconnectcmd);

	int	str_to_servref			(const char *);
	int	str_to_servref_with_update	(const char *desc);
	int	str_to_newserv			(const char *);
	void	destroy_server_list		(void);
	void	add_servers			(char *, const char *);
	int	read_default_server_file 	(void);
	void	display_server_list		(void);
	char *	create_server_list		(void);	/* MALLOC */
	int	server_list_size		(void);
	int	is_server_valid			(int refnum);

	void	flush_server			(int);
	void	send_to_server			(const char *, ...) __A(1);
	void	send_to_aserver			(int, const char *, ...) __A(2);
	void	send_to_server_with_payload	(const char *, const char *, ...) __A(2);
	void	send_to_aserver_with_payload	(int, const char *, const char *, ...) __A(3);
	void	send_to_aserver_raw		(int, size_t len, const char *buffer);
	int	grab_server_address		(int);
	int	connect_to_server		(int);
	int	close_all_servers		(const char *);
	void	close_server			(int, const char *);

	void	do_server			(int);

	void	set_server_away_message		(int, const char *);
const	char *	get_server_away_message		(int);
	void	set_server_away_status		(int, int);
	int	get_server_away_status		(int);
	int	get_server_operator		(int);

const	char *	get_umode			(int);
	void    update_user_mode        	(int, const char *);

const	char *	get_server_name			(int);
const	char *	get_server_itsname		(int);
const	char *	get_server_group		(int);
const	char *  get_server_server_type		(int);
const	char *	get_server_vhost		(int);
const	char *	get_server_cert			(int);

	void	set_server_version_string	(int, const char *);
const 	char *	get_server_version_string	(int);
	int	get_server_ssl_enabled		(int);
const	char *	get_server_ssl_cipher		(int);
 
	void	register_server			(int, const char *);
	void	password_sendline		(char *, const char *);
	int	is_server_open			(int);
	int	is_server_registered		(int);
	void	server_is_registered		(int, const char *, const char *);
	int	is_server_active		(int);
	int	auto_reconnect_callback		(void *);
	int	server_reconnects_to		(int, int);
	int	reconnect			(int, int);

	int	get_server_port			(int);
	int	get_server_local_port		(int);
	SSu	get_server_local_addr		(int);
	SSu	get_server_uh_addr		(int);

const	char *	get_server_userhost		(int);
	void	use_server_cookie		(int);

const	char *	get_server_nickname		(int);
	int	is_me				(int, const char *);
	void	change_server_nickname		(int, const char *);
const	char *	get_pending_nickname		(int);
	void	accept_server_nickname		(int, const char *);
	void   nickname_change_rejected		(int, const char *);

	void	set_server_redirect		(int, const char *);
const	char *	get_server_redirect		(int);
	int	check_server_redirect		(int, const char *);

const	char*	get_server_005			(int, const char *);
	void	set_server_005			(int, char*, const char*);

	void	server_hard_wait		(int);
        void    server_passive_wait 		(int, const char *);
        int     check_server_wait 		(int, const char *);

	int	get_server_line_length		(int);
	int	get_server_state		(int);
const char *	get_server_state_str		(int);
	int	get_server_ison_max		(int);
	int	get_server_userhost_max		(int);
	int	get_server_max_cached_chan_size	(int);

        void    set_server_doing_privmsg 	(int, int);
        int     get_server_doing_privmsg 	(int);
        void    set_server_doing_notice 	(int, int);
        int     get_server_doing_notice 	(int);
        void    set_server_doing_ctcp 		(int, int);
        int     get_server_doing_ctcp 		(int);
	void	set_server_sent			(int, int);
	int	get_server_sent			(int);
#if 0
	void	set_server_ssl_enabled		(int, int);
	int	get_server_ssl_enabled		(int);
#endif
	void	set_server_protocol_state	(int, int);
	int	get_server_protocol_state	(int);
	void	set_server_autoclose		(int, int);
	int	get_server_autoclose		(int);

        void    set_server_invite_channel       (int, const char *);
const char *    get_server_invite_channel       (int);
        void    set_server_last_notify          (int, const char *);
const char *    get_server_last_notify          (int);
        void    set_server_joined_nick          (int, const char *);
const char *    get_server_joined_nick          (int);
        void    set_server_public_nick          (int, const char *);
const char *    get_server_public_nick          (int);
        void    set_server_recv_nick            (int, const char *);
const char *    get_server_recv_nick            (int);
        void    set_server_sent_nick            (int, const char *);
const char *    get_server_sent_nick            (int);
        void	set_server_sent_body            (int, const char *);
const char *    get_server_sent_body            (int);
	void	set_server_quit_message 	(int, const char *message);
const char *    get_server_quit_message		(int);
	void	set_server_cookie		(int, const char *);
const char *	get_server_cookie         	(int);
	void	set_server_last_notify_nick	(int, const char *);
const char *	get_server_last_notify_nick    	(int);
	void	set_server_unique_id		(int, const char *);
const char *	get_server_unique_id    	(int);
	void	set_server_realname		(int, const char *);
const char *	get_server_realname		(int);
	void	set_server_default_realname	(int, const char *);	/* static */
const char *	get_server_default_realname	(int);
#if 0
        void    set_server_ssl_certificate      (int, const char *);	/* static */
const char *	get_server_ssl_certificate      (int);
        void    set_server_ssl_certificate_hash (int, const char *);	/* static */
const char *	get_server_ssl_certificate_hash (int);
#endif

	void	set_server_funny_min         	(int, int);
	int	get_server_funny_min         	(int);
	void	set_server_funny_max         	(int, int);
	int	get_server_funny_max         	(int);
	void	set_server_funny_flags         	(int, int);
	int	get_server_funny_flags         	(int);
	void	set_server_funny_match		(int, const char *);
const char *	get_server_funny_match         	(int);
	void	set_server_funny_stuff		(int, int, int, int, const char *);

        void    set_server_stricmp_table        (int, int);		/* static */
        int     get_server_stricmp_table        (int);
        void    set_server_ison_len             (int, int);		/* static */
        int     get_server_ison_len             (int);

	char *	serverctl			(char *);

	int	server_more_addrs		(int);

const char *	get_server_altname		(int refnum, int which);


#endif /* _SERVER_H_ */

