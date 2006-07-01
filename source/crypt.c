/* $EPIC: crypt.c,v 1.22 2006/07/01 04:17:12 jnelson Exp $ */
/*
 * crypt.c: The /ENCRYPT command and all its attendant baggage.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright © 1995, 2003 EPIC Software Labs
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notices, the above paragraph (the one permitting redistribution),
 *    this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The names of the author(s) may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "irc.h"
#include "sedcrypt.h"
#include "ctcp.h"
#include "ircaux.h"
#include "list.h"
#include "output.h"
#include "vars.h"
#include "words.h"
#include "server.h"

#define CRYPT_BUFFER_SIZE (IRCD_BUFFER_SIZE - 50)	/* Make this less than
							 * the transmittable
							 * buffer */

/* crypt_list: the list of nicknames and encryption keys */
static	Crypt	*crypt_list = (Crypt *) 0;

/* This must be in sync with the constants in sedcrypt.h */
static const char *ciphertype[] = {
	"External Program",
	"SED",
	"SEDSHA",
	"CAST5",
	"BLOWFISH",
	"AES-256",
	"AES-SHA-256"
};

static	Crypt *	internal_is_crypted (Char *nick, Char *serv, int type);
static int	internal_remove_crypt (Char *nick, Char *serv, int type);

/*
 * add_to_crypt: adds the nickname and key pair to the crypt_list.  If the
 * nickname is already in the list, then the key is changed the the supplied
 * key. 
 */
static void	add_to_crypt (Char *nick, Char *serv, Char *key, Char *prog, int type)
{
	Crypt	*new_crypt;

	/* Create a 'new_crypt' if one doesn't already exist */
	if (!(new_crypt = internal_is_crypted(nick, serv, type)))
	{
		new_crypt = (Crypt *) new_malloc(sizeof(Crypt));
		new_crypt->nick = NULL;
		new_crypt->serv = NULL;
		new_crypt->key = NULL;
		new_crypt->keylen = 0;
		new_crypt->prog = NULL;
		new_crypt->type = type;
	}

	/* Fill in the 'nick' field. */
	malloc_strcpy(&new_crypt->nick, nick);

	/* Fill in the 'serv' field (only for certain servs, not global) */
	if (serv)
		malloc_strcpy(&new_crypt->serv, serv);

	/* Fill in the 'key' field. */
	if (type == AES256CRYPT || type == AESSHA256CRYPT || 
		type == SEDSHACRYPT)
	{
		if (new_crypt->key == NULL)
			new_crypt->key = new_malloc(32);
		memset(new_crypt->key, 0, 32);
		new_crypt->keylen = 32;

		if (type == AES256CRYPT)
			memcpy(new_crypt->key, key, strlen(key));
		else
			sha256(key, strlen(key), new_crypt->key);
	}
	else
	{
		malloc_strcpy(&new_crypt->key, key);
		new_crypt->keylen = strlen(new_crypt->key);
	}

	/* Fill in the 'prog' field. */
	if (prog && *prog)
	{
		malloc_strcpy(&new_crypt->prog, prog);
		new_crypt->type = PROGCRYPT;
	}
	else
		new_free(&new_crypt->prog);

	/* XXX new_crypt has bifurcated primary key! */
	add_to_list((List **)&crypt_list, (List *)new_crypt);
}

static	Crypt *	internal_is_crypted (Char *nick, Char *serv, int type)
{
        Crypt   *item = NULL, *tmp;

        for (tmp = crypt_list; tmp; tmp = tmp->next)
        {
                if (tmp->type != type)
                        continue;
                if (my_stricmp(tmp->nick, nick))
                        continue;
                if (serv && tmp->serv && my_stricmp(tmp->serv, serv))
                        continue;
                if (serv != NULL || tmp->serv != NULL)
                        continue;

		return tmp;
        }
	return NULL;
}

/*
 * remove_crypt: removes the given nickname from the crypt_list, returning 0
 * if successful, and 1 if not (because the nickname wasn't in the list) 
 */
static int	internal_remove_crypt (Char *nick, Char *serv, int type)
{
	Crypt	*item = NULL, *tmp;

	if ((item = internal_is_crypted(nick, serv, type)) &&
		(remove_item_from_list((List **)&crypt_list, (List *)item)))
	{
		if (item->nick)
		{
			memset(item->nick, 0, strlen(item->nick));
			new_free((char **)&(item->nick));
		}
		if (item->serv)
		{
			memset(item->serv, 0, strlen(item->serv));
			new_free((char **)&(item->serv));
		}
		if (item->key)
		{
			memset(item->key, 0, strlen(item->key));
			new_free((char **)&(item->key));
		}
		if (item->prog)
		{
			memset(item->prog, 0, strlen(item->prog));
			new_free((char **)&(item->prog));
		}
		memset(item, 0, sizeof(Crypt));
		new_free((char **)&item);
		return (0);
	}

	return (1);
}

/*
 * is_crypted: looks up nick in the crypt_list and returns the encryption key
 * if found in the list.  If not found in the crypt_list, null is returned. 
 *
 * This is done by multiple iterations over the list.  Remember that the
 * items in the list store strings which are intended to be flexible over
 * time (ie, /encrypt efnet/hop -cast booya needs to be flexible about 
 * what "efnet" means.)
 *
 * We pick the first such crypt item in the same way /server does:
 *	1) The item's servdesc is the number 'serv'
 *	2) The item's servdesc is the 'ourname' for 'serv'
 *	3) The item's servdesc is the 'itsname' for 'serv'
 *	4) The item's servdesc is the 'group' for 'serv'
 *	5) The item's servdesc is an altname for 'serv'.
 *	6) The item doesn't have a servdesc.
 *
 * It's not supposed to be possible for two crypt keys to collide because
 * (nick, serv, type) is the primary key of the crypt list.
 */
#define CHECK_NICK_AND_TYPE \
	    if (tmp->nick && my_stricmp(tmp->nick, nick))	\
		continue;					\
	    if (type != ANYCRYPT && tmp->type != type)		\
		continue;					\

#define CHECK_CRYPTO_LIST(x) \
	for (tmp = crypt_list; tmp; tmp = tmp->next)		\
	{							\
	    CHECK_NICK_AND_TYPE					\
	    if (tmp->serv && !my_stricmp(tmp->serv, x )) 	\
		return tmp;					\
	}


Crypt *	is_crypted (Char *nick, int serv, int type)
{
	Crypt *	tmp;
	Crypt *	best = NULL;
	int	bestval = -1;

	if (!crypt_list)
		return NULL;

	/* Look for the refnum -- Bummer, special case */
	for (tmp = crypt_list; tmp; tmp = tmp->next)
	{
	    CHECK_NICK_AND_TYPE
	    if (tmp->serv && is_number(tmp->serv) && atol(tmp->serv) == serv)
		return tmp;
	}

	CHECK_CRYPTO_LIST(get_server_name(serv))
	CHECK_CRYPTO_LIST(get_server_itsname(serv))
	CHECK_CRYPTO_LIST(get_server_group(serv))
	CHECK_CRYPTO_LIST(get_server_group(serv))

	/* Look for the nickname -- special case */
	for (tmp = crypt_list; tmp; tmp = tmp->next)
	{
	    CHECK_NICK_AND_TYPE;
	    return tmp;			/* Any nick in a storm */
	}

	return NULL;
}

/*
 * encrypt_cmd: the ENCRYPT command.  Adds the given nickname and key to the
 * encrypt list, or removes it, or list the list, you know. 
 */
BUILT_IN_COMMAND(encrypt_cmd)
{
	char	*nick = NULL, 
		*key = NULL, 
		*prog = NULL;
	int	type = SEDCRYPT;
	char *	arg;

	while ((arg = new_next_arg(args, &args)))
	{
	    if (!my_stricmp(arg, "-SED"))
		type = SEDCRYPT;
	    else if (!my_stricmp(arg, "-SEDSHA"))
		type = SEDSHACRYPT;
	    else if (!my_stricmp(arg, "-CAST"))
		type = CAST5CRYPT;
	    else if (!my_stricmp(arg, "-BLOWFISH"))
		type = BLOWFISHCRYPT;
	    else if (!my_stricmp(arg, "-AES"))
		type = AES256CRYPT;
	    else if (!my_stricmp(arg, "-AESSHA"))
		type = AESSHA256CRYPT;
	    else if (*arg == '-')
	    {
		say("Usage: /ENCRYPT -TYPE nick key prog");
		say("       Where TYPE is SED, SEDSHA, CAST, BLOWFISH, AES or AESSHA");
		return;
	    }
	    else if (nick == NULL)
		nick = arg;
	    else if (key == NULL)
		key = arg;
	    else if (prog == NULL)
	    {
		prog = arg;
		type = PROGCRYPT;
	    }
	    else
	    {
		say("Usage: /ENCRYPT -TYPE nick key prog");
		say("       Where TYPE is SED, SEDSHA, CAST, BLOWFISH, AES or AESSHA");
		return;
	    }
	}

	if (nick)
	{
	    char *serv;

	    serv = nick;
	    if ((nick = strchr(serv, '/')))
		*nick++ = 0;
	    else
	    {
		nick = serv;
		serv = NULL;
	    }

	    if (key)
	    {
		add_to_crypt(nick, serv, key, prog, type);
		say("Will now cipher messages with '%s' on '%s' using '%s' "
			"with the key '%s'.",
				nick, serv ? serv : "<any>",
				prog ? prog : ciphertype[type], key);
	    }
	    else if (internal_remove_crypt(nick, serv, type))
		say("Not ciphering messages with '%s' on '%s' using '%s'",
			nick, serv ? serv : "<any>",
			prog ? prog : ciphertype[type], key);
	    else
		say("Will no longer cipher messages with '%s' on '%s' using '%s'.",
			nick, serv ? serv : "<any>",
			prog ? prog : ciphertype[type]);
	}

	else if (crypt_list)
	{
		Crypt	*tmp;

		say("The crypt:");
		for (tmp = crypt_list; tmp; tmp = tmp->next)
		    say("Ciphering messages with '%s' on '%s' using '%s' "
			   "with the key '%s'.",
				tmp->nick, 
				tmp->serv ? tmp->serv : "<any>",
				tmp->prog ? tmp->prog : ciphertype[tmp->type], 
				tmp->key);
	}
	else
	    say("You are not ciphering messages with anyone.");
}

/*
 * do_crypt: Transform a string using a symmetric cipher into its opposite.
 *
 * -- If flag == 0 (decryption)
 *	str	A C string containing CTCP-enquoted ciphertext
 *	key	An ircII crypt key with the details for decryption
 *	flag	0
 * returns:	A C string containing the corresponding plain text.
 *
 * -- If flag == 1 (encryption)
 *	str	A C string containing plain text
 *	key	An ircII crypt key with details for encryption
 *	flag	1
 * returns:	A C string containing CTCP-enquoted ciphertext.
 *
 * This cannot encrypt any binary data -- 'str' must be a C string.
 */
static char *	do_crypt (Char *str, Crypt *key, int flag)
{
	int	i;
	size_t	c;
	char	*free_it = NULL;
	unsigned char *my_str;

	i = (int)strlen(str);
	if (flag)
	{
		free_it = my_str = cipher_message(str, strlen(str)+1, key, &i);
		c = i;
		my_str = enquote_it(my_str, c);
	}
	else
	{
		c = i;
		free_it = my_str = dequote_it(str, &c);
		i = c;
		my_str = decipher_message(my_str, c, key, &i);
	}
	new_free(&free_it);
	return my_str;
}

/*
 * crypt_msg: Prepare a message payload containing an encrypted 'str'.
 *
 * 	str	- A C string containing plaintext (cannot be binary data!)
 *	key	- Something previously returned by is_crypted().
 * Returns:	- The payload of a PRIVMSG/NOTICE suitable for inclusion in
 *		  an IRC protocol command or DCC CHAT.
 */
char *	crypt_msg (Char *str, Crypt *key)
{
	char	*ptr;
	char	buffer[CRYPT_BUFFER_SIZE + 1];
	char	prefix[17];

	/* If the encryption can't be done, just return the string */
	if (!(ptr = do_crypt(str, key, 1)))
		return malloc_strdup(str);
	if (!*ptr)
		yell("WARNING: Empty encrypted message, but message "
		     "sent anyway.  Bug?");

	if (key->type == SEDCRYPT)
		snprintf(buffer, sizeof(buffer), "%c%s %s%c",
			CTCP_DELIM_CHAR, "SED", ptr, CTCP_DELIM_CHAR);
	else if (key->type == SEDSHACRYPT)
		snprintf(buffer, sizeof(buffer), "%c%s %s%c",
			CTCP_DELIM_CHAR, "SEDSHA", ptr, CTCP_DELIM_CHAR);
	else if (key->type == CAST5CRYPT)
		snprintf(buffer, sizeof(buffer), "%c%s %s%c",
			CTCP_DELIM_CHAR, "CAST128ED-CBC", ptr, CTCP_DELIM_CHAR);
	else if (key->type == BLOWFISHCRYPT)
		snprintf(buffer, sizeof(buffer), "%c%s %s%c",
			CTCP_DELIM_CHAR, "BLOWFISH-CBC", ptr, CTCP_DELIM_CHAR);
	else if (key->type == AES256CRYPT)
		snprintf(buffer, sizeof(buffer), "%c%s %s%c",
			CTCP_DELIM_CHAR, "AES256-CBC", ptr, CTCP_DELIM_CHAR);
	else if (key->type == AESSHA256CRYPT)
		snprintf(buffer, sizeof(buffer), "%c%s %s%c",
			CTCP_DELIM_CHAR, "AESSHA256-CBC", ptr, CTCP_DELIM_CHAR);
#if 0		/* Wink Wink */
	else if (key->type == FiSHCRYPT)
		snprintf(buffer, sizeof(buffer), "+OK %s", ptr);
#endif
	else
		panic("crypt_msg: key->type == %d not supported.", key->type);

	new_free(&ptr);
	return malloc_strdup(buffer);
}

/*
 * decrypt_msg: convert some ciphertext into plain text.
 *
 * This is a little simpler than crypt_msg, because the caller (do_ctcp)
 * already knows what the cipher is and has split out the payload from 
 * the privmsg/notice/dcc chat for us.
 *
 *	str	- A C string containing CTCP-enquoted ciphertext
 *	key	- Something previously returned by is_crypted().
 * Returns:	- A C string containing plain text.
 *
 * Obviously, both the input and output need to be C strings.  This does not
 * support binary data.
 *
 * Note that the retval MUST be at least 'BIG_BUFFER_SIZE + 1'.  This is
 * not an oversight -- the caller will pass the retval back to do_ctcp() 
 * which requires a big buffer to scratch around.  (The decrypted text could
 * contain a CTCP UTC which would expand to a larger string of text)
 */ 
char *	decrypt_msg (Char *str, Crypt *key)
{
	char	*buffer = (char *)new_malloc(BIG_BUFFER_SIZE + 1);
	char	*ptr = NULL;

	if (!(ptr = do_crypt(str, key, 0)))
		strlcat(buffer, str, CRYPT_BUFFER_SIZE + 1);
	else
		strlcpy(buffer, ptr, CRYPT_BUFFER_SIZE + 1);

	new_free(&ptr);
	return buffer;
}
