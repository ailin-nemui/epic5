/* $EPIC: crypt.c,v 1.39 2009/09/11 21:02:02 jnelson Exp $ */
/*
 * crypt.c: The /ENCRYPT command and all its attendant baggage.
 *
 * Copyright © 2006 EPIC Software Labs
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
const char *	happykey (const char *key, int type);

/* crypt_list: the list of nicknames and encryption keys */
static	Crypt	*crypt_list = (Crypt *) 0;

struct ciphertypes {
	int	flag;
	int	ctcp_flag;
	const char *flagname;
	const char *username;
	const char *ctcpname;
};

struct ciphertypes ciphers[] = {
   { PROGCRYPT,      -1,	      NULL,       "Program",  "SED"	      },
   { SEDCRYPT,       CTCP_SED,	     "-SED",      "SED",      "SED"	      },
   { SEDSHACRYPT,    CTCP_SEDSHA,    "-SEDSHA",   "SED+SHA",  "SEDSHA"        },
#ifdef HAVE_SSL
   { CAST5CRYPT,     CTCP_CAST5,     "-CAST",     "CAST5",    "CAST128ED-CBC" },
   { BLOWFISHCRYPT,  CTCP_BLOWFISH,  "-BLOWFISH", "BLOWFISH", "BLOWFISH-CBC"  },
   { AES256CRYPT,    CTCP_AES256,    "-AES",	  "AES",      "AES256-CBC"    },
   { AESSHA256CRYPT, CTCP_AESSHA256, "-AESSHA",   "AES+SHA",  "AESSHA256-CBC" },
   { FISHCRYPT,	     -1,	     NULL,	  "FiSH",     "BLOWFISH-EBC"  },
#endif
   { NOCRYPT,        -1,	       NULL,       NULL,       NULL           }
};

/* XXX sigh XXX */
#ifdef HAVE_SSL
const char *allciphers = "SED, SEDSHA, CAST, BLOWFISH, AES or AESSHA";
#else
const char *allciphers = "SED or SEDSHA (sorry, no SSL support)";
#endif

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
        Crypt   *tmp;

        for (tmp = crypt_list; tmp; tmp = tmp->next)
        {
                if (tmp->type != type)
                        continue;
                if (my_stricmp(tmp->nick, nick))
                        continue;

                if (serv && tmp->serv && !my_stricmp(tmp->serv, serv))
			return tmp;
		if (serv == NULL && tmp->serv == NULL)
			return tmp;
        }
	return NULL;
}

static void	cleanse_crypto_item (Crypt *item)
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
	return;
}

/*
 * remove_crypt: removes the given nickname from the crypt_list, returning 0
 * if successful, and 1 if not (because the nickname wasn't in the list) 
 */
static int	internal_remove_crypt (Char *nick, Char *serv, int type)
{
	Crypt	*item = NULL;

	if ((item = internal_is_crypted(nick, serv, type)) &&
		(remove_item_from_list((List **)&crypt_list, (List *)item)))
	{
		cleanse_crypto_item(item);
		return (0);
	}

	return (1);
}

static	void	clear_crypto_list (void)
{
	Crypt *item;

	while (crypt_list)
	{
		item = crypt_list;
		crypt_list = crypt_list->next;
		cleanse_crypto_item(item);
	}
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
		if (type == SEDCRYPT && tmp->type != PROGCRYPT) \
			continue;				\

#define CHECK_CRYPTO_LIST(x) \
	for (tmp = crypt_list; tmp; tmp = tmp->next)		\
	{							\
	    CHECK_NICK_AND_TYPE					\
	    if (tmp->serv && !my_stricmp(tmp->serv, x )) 	\
		return tmp;					\
	}

Crypt *	is_crypted (Char *nick, int serv, int ctcp_type)
{
	Crypt *	tmp;
	int	type = NOCRYPT;
	int	i;

	if (!crypt_list)
		return NULL;

	if (ctcp_type != ANYCRYPT)
	{
		for (i = 0; ciphers[i].username; i++)
			if (ciphers[i].ctcp_flag == ctcp_type)
				type = ciphers[i].flag;
		if (type == NOCRYPT)
			return NULL;
	}
	else
		type = ANYCRYPT;

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
 * encrypt_cmd: the ENCRYPT command, the user interface to the crypt list.
 *
 * 	/ENCRYPT server/target -type key
 *
 *  Where "server" is a refnum, ourname, itsname, group, or altname,
 *  Where "target" is a nickname or channel
 *  Where "type" is SED, SEDSHA, CAST, BLOWFISH, AES, or AESSHA
 *  Where "key" is a passkey
 *
 * Messages to and from the target on the corresponding server are ciphered
 * with the given algorithm using the given passkey.  If you receive a cipher
 * message from someone using a type for which you do not have an entry, you
 * will see [ENCRYPTED MESSAGE].  If you have an entry but it has the wrong
 * password, you will probably see garbage.
 *
 * The "server" argument is flexible, so if you do "efnet/#epic" then the
 * cipher will apply to #epic on any server that belongs to that group.
 * This allows you to have different types/keys with the same channel name 
 * on different networks.
 */
BUILT_IN_COMMAND(encrypt_cmd)
{
	char	*nick = NULL, 
		*key = NULL, 
		*prog = NULL;
	int	type = SEDCRYPT;
	char *	arg;
	int	i;

	while ((arg = new_next_arg(args, &args)))
	{
	    if (!my_stricmp(arg, "-CLEAR"))
		clear_crypto_list();

	    else if (*arg == '-')
	    {
		type = NOCRYPT;
		for (i = 0; ciphers[i].username; i++)
		{
		    if (ciphers[i].flagname && 
					!my_stricmp(arg,ciphers[i].flagname))
			type = ciphers[i].flag;
		}

		if (type == NOCRYPT)
		    goto usage_error;
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
usage_error:
		say("Usage: /ENCRYPT -TYPE nick key \"prog\"");
		say("       Where TYPE is %s", allciphers);
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
				prog ? prog : ciphers[type].username, key);
	    }
	    else if (internal_remove_crypt(nick, serv, type))
		say("Not ciphering messages with '%s' on '%s'.",
			nick, serv ? serv : "<any>",
			prog ? prog : ciphers[type].username);
	    else
		say("Will no longer cipher messages with '%s' on '%s' using '%s'.",
			nick, serv ? serv : "<any>",
			prog ? prog : ciphers[type].username);
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
				tmp->prog ? tmp->prog : 
					ciphers[tmp->type].username, 
				happykey(tmp->key, tmp->type));
	}
	else
	    say("You are not ciphering messages with anyone.");
}

/*
 * crypt_msg: Convert plain text into irc-ready ciphertext
 *
 * Whenever you have a C string containing plain text and you want to 
 * convert it into something you can send over irc, you call is_crypted()
 * with the cipher type of "ANYCRYPT" to fetch a crypt key, and then you 
 * pass the string and the key to this function.  This function returns a
 * malloced string containing a payload that you can send in a PRIVMSG/NOTICE.
 *
 * 	str	- A C string containing plaintext (cannot be binary data!)
 *	key	- Something previously returned by is_crypted().
 * Returns:	- The payload of a PRIVMSG/NOTICE suitable for inclusion in
 *		  an IRC protocol command or DCC CHAT.
 *
 * This is a convenience wrapper for CTCP handling.  It does not do binary
 * data.  You need to call cipher_message() directly for that.  You cannot
 * use this function to send binary data over irc.
 */
char *	crypt_msg (const unsigned char *str, Crypt *key)
{
	char	buffer[CRYPT_BUFFER_SIZE + 1];
	int	srclen;
	unsigned char *ciphertext;
	int	ciphertextlen;
	char *	dest;

	/* Convert the plaintext into ciphertext */
	srclen = (int)strlen(str);
	ciphertext = cipher_message(str, srclen + 1, key, &ciphertextlen);

	/* Convert the ciphertext into ctcp-enquoted payload */
	dest = transform_string_dyn("+CTCP", ciphertext, ciphertextlen, NULL);

	if (ciphers[key->type].ctcpname)
	     snprintf(buffer, sizeof(buffer), "%c%s %s%c",
			CTCP_DELIM_CHAR, ciphers[key->type].ctcpname, 
			dest, CTCP_DELIM_CHAR);
	else
		panic(1, "crypt_msg: key->type == %d not supported.", key->type);

	new_free(&ciphertext);
	new_free(&dest);
	return malloc_strdup(buffer);
}

/*
 * decrypt_msg: Convert ciphertext from irc into plain text.
 *
 * Whenever you receive a C string containing ctcp-enquoted ciphertext 
 * over irc, you call is_crypted() to fetch the crypt key.  Then you pass
 * the C string and the crypt key to this function, and it returns malloced
 * string containing the decrypted text.
 *
 *	str	- A C string containing CTCP-enquoted ciphertext
 *	key	- Something previously returned by is_crypted().
 * Returns:	- A C string containing plain text.
 *
 * This is a convenience wrapper for CTCP handling.  It does not do binary
 * data.  You need to call decipher_message() directly for that.  This means
 * you can't receive binary data sent over irc via this route.
 *
 * Note that the retval MUST be at least 'BIG_BUFFER_SIZE + 1'.  This is
 * not an oversight -- the caller will pass the retval back to do_ctcp() 
 * which requires a big buffer to scratch around.  (The decrypted text could
 * contain a CTCP UTC which would expand to a larger string of text)
 */ 
char *	decrypt_msg (const unsigned char *str, Crypt *key)
{
	char *	plaintext;
	int	srclen;
	char *	dest;
	size_t	destsize;
	int	destlen;

	/* Convert the ctcp-enquoted payload into ciphertext */
	dest = transform_string_dyn("-CTCP", str, 0, &destsize);

	if (!(plaintext = decipher_message(dest, destsize, key, &destlen)))
	{
		plaintext = dest;
		dest = NULL;
	}

	new_free(&dest);
	return plaintext;
}

const char *	happykey (const char *key, int type)
{
	static char prettykey[BIG_BUFFER_SIZE];

	if (type == AESSHA256CRYPT || type == SEDSHACRYPT)
	{
		int	i;
		for (i = 0; i < 32; i++)		/* XXX */
		    snprintf(prettykey + (i * 2), 3, "%2.2X", 
				(unsigned)(unsigned char)key[i]);
	}
	else
		strlcpy(prettykey, key, sizeof(prettykey));

	return prettykey;
}


