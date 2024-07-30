/*
 * flood.h: header file for flood.c
 *
 * Copyright 1991 Tomi Ollila
 * Copyright 1997 EPIC Software Labs
 * See the Copyright file for license information
 */

#error "flood.h is deprecated"
#ifndef __flood_h__
#define __flood_h__

#include "levels.h"

	int	check_flooding 		(const char *, const char *, int, const char *);
	int	new_check_flooding 	(const char *, const char *, const char *, const char *, int);
	char *	function_floodinfo	(char *);

#endif /* _FLOOD_H_ */
