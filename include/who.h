/*
 * who.h -- header info for the WHO, ISON, and USERHOST queues.
 * Copyright 1996 EPIC Software Labs
 */

#ifndef __who_h__
#define __who_h__

void clean_server_queues (int);

/* WHO queue */

typedef struct WhoEntryT
{
	int  dirty;
	int  piggyback;
	int  undernet_extended;
	char *undernet_extended_args;
	int  dalnet_extended;
	char *dalnet_extended_args;
        int  who_mask;
	char *who_target;
        char *who_name;
        char *who_host;
        char *who_server;
        char *who_nick;
        char *who_real;
	char *who_stuff;
	char *who_end;
        struct WhoEntryT *next;
	void (*line) (char *, char **);
	void (*end) (char *, char **);

} WhoEntry;

	BUILT_IN_COMMAND(whocmd);
	void 	whobase (char *, void (*)(char *, char **), 
				void (*)(char *, char **));
	void 	whoreply (char *, char **);
	void 	xwhoreply (char *, char **);
	void 	who_end (char *, char **);
	int 	fake_who_end (char *, char *);



/* ISON queue */

typedef struct IsonEntryT
{
	char *ison_asked;
	char *ison_got;
	struct IsonEntryT *next;
	void (*line) (char *, char *);
} IsonEntry;

	BUILT_IN_COMMAND(isoncmd);
	void 	isonbase 	(char *args, void (*line) (char *, char *));
	void 	ison_returned 	(char *, char **);



/* USERHOST queue */

typedef struct UserhostItemT
{
	char *	nick;
	int   	oper;
	int	connected;
	int   	away;
	char *	user;
	char *	host;
} UserhostItem;

typedef struct UserhostEntryT
{
	char *		userhost_asked;
	char *		text;
	struct UserhostEntryT *	next;
	void 		(*func) (UserhostItem *, char *, char *);
} UserhostEntry;

	BUILT_IN_COMMAND(userhostcmd);
	BUILT_IN_COMMAND(useripcmd);
	BUILT_IN_COMMAND(usripcmd);
	void 	userhostbase 		(char *arg, 
					void (*line) (UserhostItem *, 
							char *, char *), 
					int);
	void 	userhost_returned 	(char *, char **);
	void 	userhost_cmd_returned 	(UserhostItem *, char *, char *);

#endif 
