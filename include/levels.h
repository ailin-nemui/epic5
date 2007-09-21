/*
 * levels.h: Unified levels system
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997, 2003 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __levels_h__
#define __levels_h__

extern int	LEVEL_NONE;
extern int	LEVEL_OTHER;
extern int	LEVEL_PUBLIC;
extern int	LEVEL_MSG;
extern int	LEVEL_NOTICE;
extern int	LEVEL_WALL;
extern int	LEVEL_WALLOP;
extern int	LEVEL_OPNOTE;
extern int	LEVEL_SNOTE;
extern int	LEVEL_ACTION;
extern int	LEVEL_DCC;
extern int	LEVEL_CTCP;
extern int	LEVEL_INVITE;
extern int	LEVEL_JOIN;
extern int	LEVEL_NICK;
extern int	LEVEL_TOPIC;
extern int	LEVEL_PART;
extern int	LEVEL_QUIT;
extern int	LEVEL_KICK;
extern int	LEVEL_MODE;
extern int	LEVEL_OPERWALL;
extern int	LEVEL_SYSERR;
extern int	LEVEL_USER1;
extern int	LEVEL_USER2;
extern int	LEVEL_USER3;
extern int	LEVEL_USER4;
extern int	LEVEL_USER5;
extern int	LEVEL_USER6;
extern int	LEVEL_USER7;
extern int	LEVEL_USER8;
extern int	LEVEL_USER9;
extern int	LEVEL_USER10;
extern int	LEVEL_ALL;

/*-
 * Copyright (c) 1982, 1986, 1989, 1991, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#define BIT_WORDS      2
#define BIT_MAXBIT     64
#define BIT_IDX(bit)   ((bit) - 1)
#define BIT_WORD(bit)  (BIT_IDX(bit) >> 5)
#define BIT_BIT(bit)   (1 << (BIT_IDX(bit) & 31))
#define BIT_VALID(bit) ((bit) < BIT_MAXBIT && (bit) > 0)

typedef struct Mask {
	unsigned int	__bits[BIT_WORDS];
} Mask;

__inline static int	mask_setall (Mask *set)
{
	int i;

	for (i = 0; i < BIT_WORDS; i++)
		set->__bits[i] = ~0U;
	return 0;
}

__inline static int	mask_unsetall (Mask *set)
{
	int i;

	for (i = 0; i < BIT_WORDS; i++)
		set->__bits[i] = 0;
	return 0;
}

__inline static int	mask_set (Mask *set, int bit)
{
	if (bit == LEVEL_NONE)
		return mask_unsetall(set);
	if (bit == LEVEL_ALL)
		return mask_setall(set);

	if (!BIT_VALID(bit))
		return -1;
	set->__bits[BIT_WORD(bit)] |= BIT_BIT(bit);
	return 0;
}

__inline static int	mask_unset (Mask *set, int bit)
{
	if (bit == LEVEL_NONE)
		return mask_setall(set);
	if (bit == LEVEL_ALL)
		return mask_unsetall(set);

	if (!BIT_VALID(bit))
		return -1;
	set->__bits[BIT_WORD(bit)] &= ~BIT_BIT(bit);
	return 0;
}

__inline static int	mask_isall (const Mask *set)
{
	int	i;

	for (i = 0; i < BIT_WORDS; i++)
		if (set->__bits[i] != ~0U)
			return 0;
	return 1;
}

__inline static int	mask_isnone (const Mask *set)
{
	int	i;

	for (i = 0; i < BIT_WORDS; i++)
		if (set->__bits[i] != 0U)
			return 0;
	return 1;
}

__inline static int	mask_isset (const Mask *set, int bit)
{
	if (bit == LEVEL_NONE)
		return mask_isnone(set);
	if (bit == LEVEL_ALL)
		return mask_isall(set);

	if (!BIT_VALID(bit))
		return -1;
	return ((set->__bits[BIT_WORD(bit)] & BIT_BIT(bit)) ? 1 : 0);
}

/*---------------- end of bsd stuff ------------------*/

	void		init_levels	(void);
	int		add_new_level	(const char *);
	int		add_new_level_alias (int, const char *);
	char *		get_all_levels	(void);
	const char *	mask_to_str	(const Mask *);
	int		str_to_mask	(Mask *, const char *, char **);
	int     	standard_level_warning (const char *, char **);
	const char *	level_to_str	(int);
	int		str_to_level	(const char *);
	char *		levelctl	(char *);

#endif
