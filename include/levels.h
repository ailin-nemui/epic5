/*
 * levels.h: Unified levels system
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997, 2003 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __levels_h__
#define __levels_h__

#define LEVEL_NONE	0
#define LEVEL_CURRENT	0
#define LEVEL_CRAP	1
#define LEVEL_PUBLIC	2
#define LEVEL_MSG	3
#define LEVEL_NOTICE	4
#define LEVEL_WALL	5
#define LEVEL_WALLOP	6
#define LEVEL_OPNOTE	7
#define LEVEL_SNOTE	8
#define LEVEL_ACTION	9
#define LEVEL_DCC	10
#define LEVEL_CTCP	11
#define LEVEL_INVITE	12
#define LEVEL_JOIN	13
#define LEVEL_NICK	14
#define LEVEL_TOPIC	15
#define LEVEL_PART	16
#define LEVEL_QUIT	17
#define LEVEL_KICK	18
#define LEVEL_MODE	19
#define LEVEL_USER1	20
#define LEVEL_USER2	21
#define LEVEL_USER3	22
#define LEVEL_USER4	23
#define LEVEL_USER5	24
#define LEVEL_USER6	25
#define LEVEL_USER7	26
#define LEVEL_USER8	27
#define LEVEL_USER9	28
#define LEVEL_USER10	29
#define LEVEL_ALL       0x7FFFFFFF
#define NUMBER_OF_LEVELS	30

#ifdef WANT_LEVEL_NAMES
static  const char *level_types[NUMBER_OF_LEVELS] =
{
        "NONE",
        "CRAP",
        "PUBLICS",
        "MSGS",
        "NOTICES",
        "WALLS",
        "WALLOPS",
        "OPNOTES",
        "SNOTES",
        "ACTIONS",
        "DCCS",
        "CTCPS",
        "INVITES",
        "JOINS",
        "NICKS",
        "TOPICS",
        "PARTS",
        "QUITS",
        "KICKS",
        "MODES",
        "USER1",
        "USER2",
        "USER3",
        "USER4",
        "USER5",
        "USER6",
        "USER7",
        "USER8",
        "USER9",
        "USER10",
};
#endif

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

#define BIT_WORDS      1
#define BIT_MAXBIT     NUMBER_OF_LEVELS
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

#endif
