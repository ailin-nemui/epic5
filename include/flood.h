/*
 * flood.h: header file for flood.c
 *
 * Copyright 1991 Tomi Ollila
 * Copyright 1997 EPIC Software Labs
 * See the Copyright file for license information
 */

#ifndef __flood_h__
#define __flood_h__

typedef enum {
	CRAP_FLOOD	= 0,
	CTCP_FLOOD,
	INVITE_FLOOD,
	JOIN_FLOOD,
	MSG_FLOOD,
	NICK_FLOOD,
	NOTE_FLOOD,
	NOTICE_FLOOD,
	PUBLIC_FLOOD,
	TOPIC_FLOOD,
	WALLOP_FLOOD,
	WALL_FLOOD,
	NUMBER_OF_FLOODS
} FloodType;

	int	check_flooding 		(char *, char *, FloodType, char *);
	int	new_check_flooding 	(char *, char *, char *, char *, FloodType);

#endif /* _FLOOD_H_ */
