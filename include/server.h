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
#include "alist.h"

/* XXXX Ick.  Gross.  Bad. XXX */
struct notify_stru;

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
	size_t	fudge_factor;		/* How much s_nickname's fudged */
	int	nickname_pending;	/* Is a NICK command pending? */
	int	resetting_nickname;	/* Is a nickname reset in progress? */
	int	registration_pending;	/* Is a registration going on ? */
	int	registered;		/* true if registration is assured */
	int	rejoined_channels;	/* Has we tried to auto-rejoin? */
	char	*userhost;		/* my userhost on this server */
	char	*away;			/* away message for this server */
	int	operator;		/* true if operator */
	int	version;		/* the version of the server -
					 * defined above */
	int	server2_8;		/* defined if we get an 001 numeric */
	char	*version_string;	/* what is says */
	long	flags;			/* Various flags */
	long	flags2;			/* More Various flags */
	char	*umodes;		/* Possible user modes */
	char	umode[54];		/* Currently set user modes */
	int	s_takes_arg;		/* Set to 1 if s user mode has arg */
	int	des;			/* file descriptor to server */
	int	eof;			/* eof flag for server */
	int	sent;			/* set if something has been sent,
					 * used for redirect */
	char	*redirect;		/* Who we're redirecting to here */
	WhoEntry *	who_queue;	/* Who queue */
	IsonEntry *	ison_wait;	/* Ison wait queue */
	IsonEntry *	ison_queue;	/* Ison queue */
	UserhostEntry *	userhost_queue;	/* Userhost queue */

	SS	local_sockname; 	/* sockname of this connection */
	SS	remote_sockname; 	/* sockname of this connection */
	SS	uh_addr;		/* ip address the server sees */
	NotifyList	notify_list;	/* Notify list for this server */
	int	reconnects;		/* Number of reconnects done */
	char 	*cookie;		/* Erf/TS4 "cookie" value */
	int	save_channels;		/* True if abnormal connection */
	int	line_length;		/* How long a protocol command may be */
	int	max_cached_chan_size;	/* Bigger channels won't cache U@H */
	int	closing;		/* True if close_server called */
	int	reconnect_to;		/* Server to connect to on EOF */
	char	*quit_message;		/* Where we stash a quit message */
	A005	a005;			/* 005 settings kept kere. */

	int	funny_min;		/* Funny stuff */
	int	funny_max;
	int	funny_flags;
	char *	funny_match;

#ifdef HAVE_SSL
	SSL_CTX*	ctx;
	SSL_METHOD*	meth;
#endif
	void *	ssl_fd;
	int	try_ssl;		/* SSL requested on next connection. */
	int	ssl_enabled;		/* Current SSL status. */

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

	int		(*dgets) (char *, int, int, void *);
}	Server;
extern	Server	**server_list;
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

/* Funny stuff */
#define FUNNY_PUBLIC            1 << 0
#define FUNNY_PRIVATE           1 << 1
#define FUNNY_TOPIC             1 << 2
#define FUNNY_USERS             1 << 4
#define FUNNY_NAME              1 << 5



	BUILT_IN_COMMAND(servercmd);
	BUILT_IN_COMMAND(disconnectcmd);

	void	add_to_server_list 		(const char *, int, 
						 const char *, const char *, 
						 const char *, const char *,
						 int);
	int	find_in_server_list		(const char *, int);
	void	destroy_server_list		(void);
	int	find_server_refnum		(char *, char **rest);
	int	parse_server_index		(const char *, int);
	void	parse_server_info		(char **, char **, char **,
						 char **, char **, char **);
	void	build_server_list		(char *, char *);
	int	read_server_file		(void);
	void	display_server_list		(void);
	char *	create_server_list		(void);	/* MALLOC */
	int	server_list_size		(void);

	void	do_server 			(fd_set *, fd_set *);
	void	flush_server			(int);
	void	send_to_server			(const char *, ...) __A(1);
	void	send_to_aserver			(int, const char *, ...) __A(2);
	void	send_to_aserver_raw		(int, size_t len, const char *buffer);
	int	connect_to_new_server		(int, int, int);
	int	close_all_servers		(const char *);
	void	close_server			(int, const char *);

	void	set_server_away			(int, const char *);
const	char *	get_server_away			(int);

const	char *	get_possible_umodes		(int);
	void	set_possible_umodes		(int, const char *);
const	char *	get_umode			(int);
	void	clear_user_modes		(int);
	void    reinstate_user_modes    	(void);
	void    update_user_mode        	(const char *);
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
const	char *	get_server_type			(int);
	void	set_server_version_string	(int, const char *);
const 	char *	get_server_version_string	(int);
	int	get_server_isssl		(int);
const	char *	get_server_cipher		(int);
 
	void	register_server			(int, const char *);
	void	server_registration_is_not_pending (int);
	void	password_sendline		(char *, char *);
	char *	set_server_password		(int, const char *);
	int	is_server_open			(int);
	int	is_server_registered		(int);
	void	server_is_registered		(int, int);
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
	void	fudge_nickname			(int);
	void	nickname_sendline		(char *, char *);
	void	reset_nickname			(int);

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
	void	set_server_005			(int, char*, char*);

        void    server_hard_wait 		(int);
        void    server_passive_wait 		(int, const char *);
        int     check_server_wait 		(int, const char *);

        void    set_server_doing_privmsg 	(int, int);
        int     get_server_doing_privmsg 	(int);
        void    set_server_doing_notice 	(int, int);
        int     get_server_doing_notice 	(int);
        void    set_server_doing_ctcp 		(int, int);
        int     get_server_doing_ctcp 		(int);
	void	set_server_nickname_pending	(int, int);
	int	get_server_nickname_pending	(int);
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

	char *	serverctl			(char *);
#endif /* _SERVER_H_ */
