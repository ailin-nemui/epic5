/*
 * if.h: header for if.c
 *
 * Copyright 1994 Matthew Green
 * Copyright 1997 EPIC Software Labs
 * See the copyright file, or do a help ircii copyright
 */

#ifndef __if_h__
#define __if_h__

	char *	next_expr 		(char **, char);
	char *	next_expr_failok 	(char **, char);
	char *	next_expr_with_type	(char **, char);

	BUILT_IN_COMMAND(ifcmd);
	BUILT_IN_COMMAND(docmd);
	BUILT_IN_COMMAND(whilecmd);
	BUILT_IN_COMMAND(foreach);
	BUILT_IN_COMMAND(fe);
	BUILT_IN_COMMAND(forcmd);
	BUILT_IN_COMMAND(fec);
	BUILT_IN_COMMAND(switchcmd);
	BUILT_IN_COMMAND(repeatcmd);

#endif /* __if_h */
