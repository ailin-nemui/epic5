/*
 * crypt.c: handles some encryption of messages stuff. 
 *
 * Written By Michael Sandrof
 *
 * Copyright(c) 1990, 1995 Michael Sandroff and others 
 *
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#if 0
static	char	rcsid[] = "@(#)$Id: crypt.c,v 1.1 2000/12/05 00:11:56 jnelson Exp $";
#endif

#include "irc.h"
#include "crypt.h"
#include "ctcp.h"
#include "ircaux.h"
#include "list.h"
#include "output.h"
#include "vars.h"

#define CRYPT_BUFFER_SIZE (IRCD_BUFFER_SIZE - 50)	/* Make this less than
							 * the transmittable
							 * buffer */

/*
 * Crypt: the crypt list structure,  consists of the nickname, and the
 * encryption key 
 */
typedef struct	CryptStru
{
	struct	CryptStru *next;
	char	*nick;
	char	*key;
	int	filename;
}	Crypt;

/* crypt_list: the list of nicknames and encryption keys */
static	Crypt	*crypt_list = (Crypt *) 0;

/*
 * add_to_crypt: adds the nickname and key pair to the crypt_list.  If the
 * nickname is already in the list, then the key is changed the the supplied
 * key. 
 */
static	void	add_to_crypt(char *nick, char *key)
{
	Crypt	*new_crypt;

	if ((new_crypt = (Crypt *) remove_from_list((List **)&crypt_list, nick)) != NULL)
	{
		new_free((char **)&(new_crypt->nick));
		new_free((char **)&(new_crypt->key));
		new_free((char **)&new_crypt);
	}
	new_crypt = (Crypt *) new_malloc(sizeof(Crypt));
	new_crypt->nick = (char *) 0;
	new_crypt->key = (char *) 0;

	malloc_strcpy(&(new_crypt->nick), nick);
	malloc_strcpy(&(new_crypt->key), key);
	add_to_list((List **)&crypt_list, (List *)new_crypt);
}

/*
 * remove_crypt: removes the given nickname from the crypt_list, returning 0
 * if successful, and 1 if not (because the nickname wasn't in the list) 
 */
static	int	remove_crypt (char *nick)
{
	Crypt	*tmp;

	if ((tmp = (Crypt *) list_lookup((List **)&crypt_list, nick, !USE_WILDCARDS,
			REMOVE_FROM_LIST)) != NULL)
	{
		new_free((char **)&(tmp->nick));
		new_free((char **)&(tmp->key));
		new_free((char **)&tmp);
		return (0);
	}
	return (1);
}

/*
 * is_crypted: looks up nick in the crypt_list and returns the encryption key
 * if found in the list.  If not found in the crypt_list, null is returned. 
 */
char	*is_crypted (char *nick)
{
	Crypt	*tmp;

	if (!crypt_list)
		return NULL;
	if ((tmp = (Crypt *) list_lookup((List **)&crypt_list, nick,
			!USE_WILDCARDS, !REMOVE_FROM_LIST)) != NULL)
		return (tmp->key);
	return NULL;
}

/*
 * encrypt_cmd: the ENCRYPT command.  Adds the given nickname and key to the
 * encrypt list, or removes it, or list the list, you know. 
 */
BUILT_IN_COMMAND(encrypt_cmd)
{
	char	*nick,
	*key;

	if ((nick = next_arg(args, &args)) != NULL)
	{
		if ((key = next_arg(args, &args)) != NULL)
		{
			add_to_crypt(nick, key);
			say("%s added to the crypt with key %s", nick, key);
		}
		else
		{
			if (remove_crypt(nick))
				say("No such nickname in the crypt: %s", nick);
			else
				say("%s removed from the crypt", nick);
		}
	}
	else
	{
		if (crypt_list)
		{
			Crypt	*tmp;

			say("The crypt:");
			for (tmp = crypt_list; tmp; tmp = tmp->next)
				put_it("%s with key %s", tmp->nick, tmp->key);
		}
		else
			say("The crypt is empty");
	}
}

void 	my_encrypt (char *str, int len, char *key)
{
	int	key_len,
		key_pos,
		i;
	char	mix,
		tmp;

	if (!key)
		return;			/* no encryption */

	key_len = strlen(key);
	key_pos = 0;
	mix = 0;

	for (i = 0; i < len; i++)
	{
		tmp = str[i];
		str[i] = mix ^ tmp ^ key[key_pos];
		mix ^= tmp;
		key_pos = (key_pos + 1) % key_len;
	}
	str[i] = (char) 0;
}

void 	my_decrypt (char *str, int len, char *key)
{
	int	key_len,
		key_pos,
		i;
	char	mix,
		tmp;

	if (!key)
		return;			/* no decryption */

	key_len = strlen(key);
	key_pos = 0;
	mix = 0;

	for (i = 0; i < len; i++)
	{
		tmp = mix ^ str[i] ^ key[key_pos];
		str[i] = tmp;
		mix ^= tmp;
		key_pos = (key_pos + 1) % key_len;
	}
	str[i] = (char) 0;
}

static 	char *do_crypt (char *str, char *key, int flag)
{
	int	c;
	char	*ptr;

	c = strlen(str);
	if (flag)
	{
		my_encrypt(str, c, key);
		ptr = ctcp_quote_it(str, c);
	}
	else
	{
		ptr = ctcp_unquote_it(str, &c);
		my_decrypt(ptr, c, key);
	}
	return (ptr);
}

/*
 * crypt_msg: Given plaintext 'str', constructs a body suitable for sending
 * via PRIVMSG or DCC CHAT.
 */
char 	*crypt_msg (char *str, char *key)
{
	char	buffer[CRYPT_BUFFER_SIZE + 1];
	char	thing[6];
	char	*ptr;

	sprintf(thing, "%cSED ", CTCP_DELIM_CHAR);
	*buffer = 0;
	if ((ptr = do_crypt(str, key, 1)))
	{
		strmcat(buffer, thing, CRYPT_BUFFER_SIZE);
		strmcat(buffer, ptr, CRYPT_BUFFER_SIZE);
		strmcat(buffer, CTCP_DELIM_STR, CRYPT_BUFFER_SIZE);
		new_free(&ptr);
	}
	else
		strmcat(buffer, str, CRYPT_BUFFER_SIZE);

	return m_strdup(buffer);
}

/*
 * Given a CTCP SED argument 'str', it attempts to unscramble the text
 * into something more sane.  If the 'key' is not the one used to scramble
 * the text, the results are unpredictable.  This is probably the point.
 *
 * Note that the retval MUST be at least 'BIG_BUFFER_SIZE + 1'.  This is
 * not an oversight -- the retval is passed is to do_ctcp() which requires
 * a big buffer to scratch around (The decrypted text could be a CTCP UTC
 * which could expand to a larger string of text.)
 */ 
char 	*decrypt_msg (char *str, char *key)
{
	char	*buffer = (char *)new_malloc(BIG_BUFFER_SIZE + 1);
	char	*ptr;

	if ((ptr = do_crypt(str, key, 0)) != NULL)
	{
		strmcpy(buffer, ptr, CRYPT_BUFFER_SIZE);
		new_free(&ptr);
	}
	else
		strmcat(buffer, str, CRYPT_BUFFER_SIZE);

	return buffer;
}
