/*
 * server groups
 */

struct sgroup_typ {
	Protocol *	proto;			/* The protocol we belong to */
	Server *	server_list;		/* The servers we own */
	int		active;			/* One server is connected */
	int		reconnect_pending;	/* Should be active but not */

	char *		name;			/* The name of this group */
	char *		nickname;		/* Our primary nickname */
	char *		s_nickname;		/* Last NICK sent */
	char *		d_nickname;		/* Default nickname */
	int		nickname_pending; 	/* Is a NICK pending? */
	int		fudge_factor;		/* How much s_nickname fudged */
	char *		away;			/* Current away message */
	char *		umodes;			/* Possible user modes */
	char		umode[54];		/* Current user modes */

	long		flags;			/* Miscelaneous flags */
};
typedef struct sgroup_typ SGroup;

struct server_typ {
	SGroup *	group;			/* Server group we belong to */
	char *		name;			/* Our idea of server's name */
	char *		itsname;		/* Its idea of server's name */
	int		port;			/* Port to use */
	char *		server_type		/* Server type */

	char *		password;		/* Password to use on server */
	char *		userhost;		/* My userhost on server */
	int		oper;			/* True if irc operator */
	int		version;		/* class of server (above) */
	int		server2_8;		/* True if 001 numeric seen */
	char *		version_string;		/* Full server version */
	long		flags;			/* Misc flags */
	int		s_takes_arg;		/* True if +s takes argument */
	int		connected;		/* True if we are connected */
	int		write;			/* Writing fd (== read) */
	int		read;			/* Reading fd (== write) */
	int		eof;			/* True if we've seen an EOF */
	int		motd;			/* True if we've seen MOTD */
	int		sent;			/* True if we've sent stuff */
	char *		redirect;		/* Who we're redirecting to */
	WhoisEntry *	who_queue;		/* WHO Queue */
	IsonEntry *	ison_queue;		/* ISON Queue */
	UserhostEntry *	userhost_queue;		/* USERHOST Queue */
	IA		local_addr;		/* My local ip address */
	ISA		local_sockname;		/* My local sockname */
	NotifyList	notify_list;		/* List of nicks to watch */
	int		reconnects;		/* Number of retries */
	char *		cookie;			/* EF/TS4 "cookie" value. */
};

typedef struct server_typ Server;

