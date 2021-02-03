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

typedef array A005;
typedef struct
{
	char	*name;
	u_32int_t hash;
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
#if 0
	const char *  default_encoding;
#endif
} ServerInfo;

/* Server: a structure for the server_list */
typedef	struct
{
	ServerInfo *info;		/* Canonical information */

	AI 	*addrs;			/* Returned by getaddrinfo */
const	AI	*next_addr;		/* The next one to try upon failure */
	int	addr_counter;		/* How far we're into "addrs" */
	ssize_t	addr_len;
	ssize_t	addr_offset;

	char	*itsname;		/* the server's idea of its name */
	Bucket	*altnames;		/* Alternate handles for the server */
	char	*nickname;		/* Authoritative nickname for us */
	char	*s_nickname;		/* last NICK command sent */
	char	*d_nickname;		/* Default nickname to use */
	char	*unique_id;		/* Unique ID (for ircnet) */

	int	status;			/* See above */

	char *	default_encoding;	/* What string encoding we should 
					   assume for non-utf8 strings */
	char	*userhost;		/* my userhost on this server */
	char	*away;			/* away message for this server */
	int	operator;		/* true if operator */
	int	version;		/* the version of the server -
					 * defined above */
	int	server2_8;		/* defined if we get an 001 numeric */
	char	*version_string;	/* what is says */
	char	umode[54];		/* Currently set user modes */
	int	des;			/* file descriptor to server */
	int	sent;			/* set if something has been sent,
					 * used for redirect */
	char	*redirect;		/* Who we're redirecting to here */
	int	who_max;		/* Max pending whos */
	WhoEntry *	who_queue;	/* Who queue */
	int	ison_len;		/* Max ison characters */
	int	ison_max;		/* Max pending isons */
	IsonEntry *	ison_queue;	/* Ison queue */
	IsonEntry *	ison_wait;	/* Ison wait queue */
	int	userhost_max;		/* Max pending userhosts */
	UserhostEntry *	userhost_queue;	/* Userhost queue */
	UserhostEntry *	userhost_wait;	/* Userhost wait queue */

	SS	local_sockname; 	/* sockname of this connection */
	SS	remote_sockname; 	/* sockname of this connection */
	SS	uh_addr;		/* ip address the server sees */
	int	uh_addr_set;		/* 0 or 1, if set_uh_addr() has been called */
					/* Used to guard an annoying error message */
	NotifyList	notify_list;	/* Notify list for this server */
	char 	*cookie;		/* Erf/TS4 "cookie" value */
	int	line_length;		/* How long a protocol command may be */
	int	max_cached_chan_size;	/* Bigger channels won't cache U@H */
	int	closing;		/* True if close_server called */
	char	*quit_message;		/* Where we stash a quit message */
	A005	a005;			/* 005 settings kept kere. */
	int	stricmp_table;		/* Which case insensitive map to use */
	int	autoclose;		/* Whether the server is closed when
					   there are no windows on it */

	int	funny_min;		/* Funny stuff */
	int	funny_max;
	int	funny_flags;
	char *	funny_match;

	int	ssl_enabled;		/* Current SSL status. */

	char *	realname;		/* The actual realname. */
	char *	default_realname;	/* The default realname. */

        int             doing_privmsg;
        int             doing_notice;
        int             doing_ctcp;
        int             waiting_in;
        int             waiting_out;
        WaitCmd *       start_wait_list;
        WaitCmd *       end_wait_list;

        char *          invite_channel;
        char *          last_notify_nick;
        char *          joined_nick;
        char *          public_nick;
        char *          recv_nick;
        char *          sent_nick;
        char *          sent_body;

	char *		ssl_certificate;
	char *		ssl_certificate_hash;
}	Server;
extern	Server	**server_list;

	int     serverinfo_matches_servref	(ServerInfo *, int);
        int     clear_serverinfo (ServerInfo *s);
        int     str_to_serverinfo (char *str, ServerInfo *s);

#endif	/* NEED_SERVER_LIST */

	extern	int	number_of_servers;
	extern	int	connected_to_server;
	extern	int	primary_server;
	extern	int	from_server;
	extern	int	last_server;
	extern	int	parsing_server_index;

#ifdef NEED_SERVER_LIST
static __inline__ Server *	get_server (int server)
{
	if (server == -1 && from_server >= 0)
		server = from_server;
	if (server < 0 || server >= number_of_servers)
		return NULL;
	return server_list[server];
}

/* 
 * These two macros do bounds checking on server refnums that are
 * passed into various server functions
 */
#define CHECK_SERVER(x)				\
{						\
	if (!get_server(x))			\
		return;				\
}

#define CHECK_SERVER_RET(x, y)			\
{						\
	if (!get_server(x))			\
		return (y);			\
}


#define __FROMSERV	get_server(from_server)
#define SERVER(x)	get_server(x)

#endif	/* NEED_SERVER_LIST */

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
extern	const char *server_states[13];



	BUILT_IN_COMMAND(servercmd);
	BUILT_IN_COMMAND(disconnectcmd);
	BUILT_IN_COMMAND(reconnectcmd);

	int	str_to_servref		(Char *);
	int	str_to_servref_with_update	(const char *desc);
	int	str_to_newserv		(Char *);
	void	destroy_server_list	(void);
	void	add_servers		(char *, Char *);
	int	read_default_server_file (void);
	void	display_server_list	(void);
	char *	create_server_list	(void);	/* MALLOC */
	int	server_list_size	(void);
	int	is_server_valid		(int refnum);

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

	void	set_server_away			(int, const char *);
const	char *	get_server_away			(int);

const	char *	get_possible_umodes		(int);
	void	set_possible_umodes		(int, const char *);
const	char *	get_umode			(int);
	void	clear_user_modes		(int);
	void    reinstate_user_modes    	(void);
	void    update_user_mode        	(int, const char *);
	void	set_server_flag			(int, int, int);
	int	get_server_flag			(int, int);

	void	set_server_version		(int, int);
	int	get_server_version		(int);

	void	set_server_name			(int, const char *);
const	char *	get_server_name			(int);
	void	set_server_itsname		(int, const char *);
const	char *	get_server_itsname		(int);
	void	set_server_group		(int, const char *);
const	char *	get_server_group		(int);
const	char *    get_server_server_type	(int);
	void    set_server_server_type		(int, const char *);
	void	set_server_vhost		(int, const char *);
const	char *	get_server_vhost		(int);

const	char *	get_server_type			(int);
	void	set_server_version_string	(int, const char *);
const 	char *	get_server_version_string	(int);
	int	get_server_isssl		(int);
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
	SS	get_server_local_addr		(int);
	SS	get_server_uh_addr		(int);

const	char *	get_server_userhost		(int);
	void 	got_my_userhost 		(int, UserhostItem *, 
						 const char *, const char *);

	int	get_server_operator		(int);
	void	set_server_operator		(int, int);

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
	void	save_servers			(FILE *);

	void	server_did_rejoin_channels	(int);
	int	did_server_rejoin_channels	(int);

	void	clear_reconnect_counts		(void);

	int	get_server_enable_ssl 		(int);
	void   	set_server_enable_ssl 		(int, int);

	void	make_005			(int);
	void	destroy_005			(int);
const	char*	get_server_005			(int, const char *);
	void	set_server_005			(int, char*, const char*);

        void    server_hard_wait 		(int);
        void    server_passive_wait 		(int, const char *);
        int     check_server_wait 		(int, const char *);

        void    set_server_doing_privmsg 	(int, int);
        int     get_server_doing_privmsg 	(int);
        void    set_server_doing_notice 	(int, int);
        int     get_server_doing_notice 	(int);
        void    set_server_doing_ctcp 		(int, int);
        int     get_server_doing_ctcp 		(int);
	void	set_server_sent			(int, int);
	int	get_server_sent			(int);
	void	set_server_try_ssl		(int, int);
	int	get_server_try_ssl		(int);
	void	set_server_ssl_enabled		(int, int);
	int	get_server_ssl_enabled		(int);
	void	set_server_save_channels	(int, int);
	int	get_server_save_channels	(int);
	void	set_server_protocol_state	(int, int);
	int	get_server_protocol_state	(int);
	void	set_server_line_length		(int, int);
	int	get_server_line_length		(int);
	void	set_server_max_cached_chan_size	(int, int);
	int	get_server_max_cached_chan_size	(int);
	void	set_server_ison_max		(int, int);
	int	get_server_ison_max		(int);
	void	set_server_userhost_max		(int, int);
	int	get_server_userhost_max		(int);
	void	set_server_status		(int, int);
	int	get_server_status		(int);
const char *	get_server_status_str		(int);
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
	void	set_server_default_realname	(int, const char *);
const char *	get_server_default_realname	(int);
        void    set_server_ssl_certificate      (int, const char *);
const char *	get_server_ssl_certificate      (int);
        void    set_server_ssl_certificate_hash (int, const char *);
const char *	get_server_ssl_certificate_hash (int);

	void	set_server_funny_min         	(int, int);
	int	get_server_funny_min         	(int);
	void	set_server_funny_max         	(int, int);
	int	get_server_funny_max         	(int);
	void	set_server_funny_flags         	(int, int);
	int	get_server_funny_flags         	(int);
	void	set_server_funny_match		(int, const char *);
const char *	get_server_funny_match         	(int);
	void	set_server_funny_stuff		(int, int, int, int, const char *);

        void    set_server_window_count         (int, int);
        int     get_server_window_count         (int);
        void    set_server_stricmp_table        (int, int);
        int     get_server_stricmp_table        (int);
        void    set_server_ison_len             (int, int);
        int     get_server_ison_len             (int);

const	char *	get_server_default_encoding	(int);

	char *	serverctl			(char *);

	int	server_more_addrs		(int);
	int	server_addrs_left		(int);
	int	get_server_by_desc		(const char *, int);

const char *	get_server_altname		(int refnum, int which);
	int     which_server_altname		(int refnum, const char *);


#endif /* _SERVER_H_ */

