/* $EPIC: crypt.c,v 1.46 2015/07/10 03:16:18 jnelson Exp $ */
/*
 * crypt.c: The /ENCRYPT command and all its attendant baggage.
 *
 * Copyright 2006, 2015 EPIC Software Labs
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

/*
 * Here's the plan
 *
 * In addition to the traditional /ENCRYPT command, which will be supported
 * "forever", but not necessarily emphasized, we want to introduce a new
 * syntax that is clearer to work with.
 *
 * Select an ENCRYPT to work on:
 *   - /ENCRYPT NEW serv/nick
 *   - /ENCRYPT TARGET serv/nick
 *   - /ENCRYPT 5
 *
 * Change a selected ENCRYPT:
 *   - NEWTARGET serv/nick
 *   - PASSWORD newpass
 *   - TYPE newcipher
 *   - PROGRAM {newcode}
 *
 * Change status of ENCRYPT:
 *   - ON       (send encrypted messages by default)
 *   - OFF      (do not send outbound; but decrypt inbound)
 *   - DELETE   (irreversibly delete)
 */

#define CRYPT_BUFFER_SIZE (IRCD_BUFFER_SIZE - 50)	/* Make this less than
							 * the transmittable
							 * buffer */

/* crypt_list: the list of nicknames and encryption sessions */
static	Crypt	*crypt_list = (Crypt *) 0;

struct ciphertypes {
	int	sed_type;
	const char *flagname;
	const char *username;
	const char *ctcpname;
};

struct ciphertypes ciphers[] = {
   { PROGCRYPT,      NULL,        "Program",  "SED"	      },
   { SEDCRYPT,       "-SED",      "SED",      "SED"	      },
   { SEDSHACRYPT,    "-SEDSHA",   "SED+SHA",  "SEDSHA"        },
   { CAST5CRYPT,     "-CAST",     "CAST5",    "CAST128ED-CBC" },
   { BLOWFISHCRYPT,  "-BLOWFISH", "BLOWFISH", "BLOWFISH-CBC"  },
   { AES256CRYPT,    "-AES",	  "AES",      "AES256-CBC"    },
   { AESSHA256CRYPT, "-AESSHA",	  "AES+SHA",  "AESSHA256-CBC" },
   { FISHCRYPT,	     NULL,	  "FiSH",     "BLOWFISH-EBC"  },
   { NOCRYPT,        NULL,        NULL,       NULL            }
};

/* XXX sigh XXX */
const char *allciphers = "SED, SEDSHA, CAST, BLOWFISH, AES or AESSHA";

static	Crypt *	internal_is_crypted (Char *nick, Char *serv);
static int	internal_remove_crypt (Char *nick, Char *serv);
static void	cleanse_crypto_item (Crypt *item);
const char *	happypasswd (const char *key, int sed_type);

/*
 * add_to_crypt:  Create a new ENCRYPT entry
 *
 * Arguments:
 *	nick	- The "nick" part of "serv/nick"
 *	serv	- The "serv" part of "serv/nick"
 *	passwd	- The password
 *	prog	- A program to /exec (instead of crypt cipher)
 *	sed_type - The Cipher to use (use PROGCRYPT if prog != NULL)
 *
 * add_to_crypt: adds the nickname and key pair to the crypt_list.  If the
 * nickname is already in the list, then the password is changed to the 
 * supplied password. 
 */
static void	add_to_crypt (Char *nick, Char *serv, Char *passwd, Char *prog, int sed_type)
{
	Crypt	*new_crypt;

	/* Always purge an old item if there is one */
	if ((new_crypt = internal_is_crypted(nick, serv)))
		cleanse_crypto_item(new_crypt);
	else
		new_crypt = (Crypt *)new_malloc(sizeof(Crypt));

	new_crypt->nick = NULL;
	new_crypt->serv = NULL;
	new_crypt->passwd = NULL;
	new_crypt->passwdlen = 0;
	new_crypt->prog = NULL;
	new_crypt->sed_type = sed_type;

	/* Fill in the 'nick' field. */
	malloc_strcpy(&new_crypt->nick, nick);

	/* Fill in the 'serv' field (only for certain servs, not global) */
	if (serv)
		malloc_strcpy(&new_crypt->serv, serv);

	/* Fill in the 'passwd' field. */
	if (sed_type == AES256CRYPT || sed_type == AESSHA256CRYPT || 
		sed_type == SEDSHACRYPT)
	{
		if (new_crypt->passwd == NULL)
			new_crypt->passwd = new_malloc(32);
		memset(new_crypt->passwd, 0, 32);
		new_crypt->passwdlen = 32;

		if (sed_type == AES256CRYPT)
			memcpy(new_crypt->passwd, passwd, strlen(passwd));
		else
			sha256(passwd, strlen(passwd), new_crypt->passwd);
	}
	else
	{
		malloc_strcpy(&new_crypt->passwd, passwd);
		new_crypt->passwdlen = strlen(new_crypt->passwd);
	}

	/* Fill in the 'prog' field. */
	if (prog && *prog)
	{
		malloc_strcpy(&new_crypt->prog, prog);
		new_crypt->sed_type = PROGCRYPT;
	}
	else
		new_free(&new_crypt->prog);

	/* XXX new_crypt has bifurcated primary passwd! */
	add_to_list((List **)&crypt_list, (List *)new_crypt);
}

static	Crypt *	internal_is_crypted (Char *nick, Char *serv)
{
        Crypt   *tmp;

        for (tmp = crypt_list; tmp; tmp = tmp->next)
        {
                if (my_stricmp(tmp->nick, nick))
                        continue;

                if (serv && tmp->serv && !my_stricmp(tmp->serv, serv))
			return tmp;
		if (serv == NULL && tmp->serv == NULL)
			return tmp;
        }
	return NULL;
}

/*
 * remove_crypt: removes the given nickname from the crypt_list, returning 0
 * if successful, and 1 if not (because the nickname wasn't in the list) 
 */
static int	internal_remove_crypt (Char *nick, Char *serv)
{
	Crypt	*item = NULL;

	if ((item = internal_is_crypted(nick, serv)) &&
		(remove_item_from_list((List **)&crypt_list, (List *)item)))
	{
		cleanse_crypto_item(item);
		new_free((char **)&item);
		return 0;	/* Success */
	}

	return -1;		/* Not found */
}

static	void	clear_crypto_list (void)
{
	Crypt *item;

	while (crypt_list)
	{
		item = crypt_list;
		crypt_list = crypt_list->next;
		cleanse_crypto_item(item);
		new_free((char **)&item);
	}
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
	if (item->passwd)
	{
		memset(item->passwd, 0, strlen(item->passwd));
		new_free((char **)&(item->passwd));
	}
	if (item->prog)
	{
		memset(item->prog, 0, strlen(item->prog));
		new_free((char **)&(item->prog));
	}
	memset(item, 0, sizeof(Crypt));
	return;
}


/* * * */
/*
 * is_crypted: looks up nick in the crypt_list and returns the encryption
 * session if found in the list.  If not found in the crypt_list, null is 
 * returned. 
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
 * It's not supposed to be possible for two encryption sessions to collide 
 * because (nick, serv) is the primary key of the crypt list.
 */
#define CHECK_NICK_AND_TYPE \
	    if (tmp->nick && my_stricmp(tmp->nick, nick))		\
		continue;						\
	    if (sed_type != ANYCRYPT && tmp->sed_type != sed_type)	\
	    {								\
		if (sed_type == SEDCRYPT && tmp->sed_type != PROGCRYPT) \
			/* ok */;					\
		else							\
			continue;					\
	    }

#define CHECK_CRYPTO_LIST(x) \
	for (tmp = crypt_list; tmp; tmp = tmp->next)		\
	{							\
	    CHECK_NICK_AND_TYPE					\
	    if (tmp->serv && !my_stricmp(tmp->serv, x )) 	\
		return tmp;					\
	}

Crypt *	is_crypted (Char *nick, int serv, const char *ctcp_cmd)
{
	Crypt *	tmp;
	int	sed_type = NOCRYPT;
	int	i;

	if (!crypt_list)
		return NULL;

	/* 
	 * ctcp_cmd is either NULL ("Any type")
	 * or a string containing a specific ctcp type.
	 * We can convert the ctcp type to a sed type using
	 * the 'ciphers' table.
	 */
	if (ctcp_cmd != NULL)
	{
		for (i = 0; ciphers[i].username; i++)
			if (!my_stricmp(ciphers[i].ctcpname, ctcp_cmd))
				sed_type = ciphers[i].sed_type;

		if (sed_type == NOCRYPT)
			return NULL;	/* This nick is not ciphered with that type */
	}
	else
		sed_type = ANYCRYPT;

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
	/* Why is get_server_group listed twice? */

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
 * 	/ENCRYPT server/target -type passwd
 *
 *  Where "server" is a refnum, ourname, itsname, group, or altname,
 *  Where "target" is a nickname or channel
 *  Where "type" is SED, SEDSHA, CAST, BLOWFISH, AES, or AESSHA
 *  Where "passwd" is a passkey
 *
 * Messages to and from the target on the corresponding server are ciphered
 * with the given algorithm using the given password.  If you receive a cipher
 * message from someone using a type for which you do not have an entry, you
 * will see [ENCRYPTED MESSAGE].  If you have an entry but it has the wrong
 * password, you will probably see garbage.
 *
 * The "server" argument is flexible, so if you do "efnet/#epic" then the
 * cipher will apply to #epic on any server that belongs to that group.
 * This allows you to have different sessions with the same channel name 
 * on different networks.
 */
BUILT_IN_COMMAND(encrypt_cmd)
{
	char	*nick = NULL, 
		*passwd = NULL, 
		*prog = NULL;
	int	sed_type = SEDCRYPT;
	char *	arg;
	int	i;
	int	remove_it = 0;

	while ((arg = new_next_arg(args, &args)))
	{
	    if (!my_stricmp(arg, "-REMOVE"))
		remove_it = 1;

	    else if (!my_stricmp(arg, "-CLEAR"))
		clear_crypto_list();

	    else if (*arg == '-')
	    {
		sed_type = NOCRYPT;
		for (i = 0; ciphers[i].username; i++)
		{
		    if (ciphers[i].flagname && 
					!my_stricmp(arg,ciphers[i].flagname))
			sed_type = ciphers[i].sed_type;
		}

		if (sed_type == NOCRYPT)
		    goto usage_error;
	    }
	    else if (nick == NULL)
		nick = arg;
	    else if (passwd == NULL)
		passwd = arg;
	    else if (prog == NULL)
	    {
		prog = arg;
		sed_type = PROGCRYPT;
	    }
	    else
	    {
usage_error:
		say("Usage: /ENCRYPT -TYPE nick passwd \"prog\"");
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

	    if (remove_it || !passwd)
	    {
		if (internal_remove_crypt(nick, serv))
			say("You are not ciphering messages with '%s' on '%s'.",
				nick, serv ? serv : "<any>");
		else
			say("You will no longer cipher messages with '%s' on '%s'.",
				nick, serv?serv:"<any>");
	    }
	    else 
	    {
		add_to_crypt(nick, serv, passwd, prog, sed_type);
		say("You will now cipher messages with '%s' on '%s' using '%s' "
			"with the passwd '%s'.",
				nick, serv ? serv : "<any>",
				prog ? prog : ciphers[sed_type].username, 
				passwd);
	    }
	}

	else if (crypt_list)
	{
		Crypt	*tmp;

		say("Your %ss:", command);
		for (tmp = crypt_list; tmp; tmp = tmp->next)
		    say("You are ciphering messages with '%s' on '%s' using '%s' "
			   "with the passwd '%s'.",
				tmp->nick, 
				tmp->serv ? tmp->serv : "<any>",
				tmp->prog ? tmp->prog : 
					ciphers[tmp->sed_type].username, 
				happypasswd(tmp->passwd, tmp->sed_type));
	}
	else
	    say("You are not ciphering messages with anyone.");
}

/*
 * crypt_msg: Convert plain text into irc-ready ciphertext
 *
 * Whenever you have a C string containing plain text and you want to 
 * convert it into something you can send over irc, you call is_crypted()
 * with the cipher sed_type of "ANYCRYPT" to return a session, and then you 
 * pass the string and the passwd to this function.  This function returns a
 * malloced string containing a payload that you can send in a PRIVMSG/NOTICE.
 *
 * 	str	- A C string containing plaintext (cannot be binary data!)
 *	crypt	- Something previously returned by is_crypted().
 * Returns:	- The payload of a PRIVMSG/NOTICE suitable for inclusion in
 *		  an IRC protocol command or DCC CHAT.
 *
 * This is a convenience wrapper for CTCP handling.  It does not do binary
 * data.  You need to call cipher_message() directly for that.  You cannot
 * use this function to send binary data over irc.
 */
char *	crypt_msg (const unsigned char *str, Crypt *crypti)
{
	char	buffer[CRYPT_BUFFER_SIZE + 1];
	int	srclen;
	unsigned char *ciphertext;
	int	ciphertextlen;
	char *	dest;

	/* Convert the plaintext into ciphertext */
	srclen = (int)strlen(str);
	ciphertext = cipher_message(str, srclen + 1, crypti, &ciphertextlen);

	/* Convert the ciphertext into ctcp-enquoted payload */
	if (!(dest = transform_string_dyn("+CTCP", ciphertext, ciphertextlen, NULL)))
	{
		yell("crypt_msg: Could not CTCP-enquote [%s]", ciphertext);
		return ciphertext;	/* Here goes nothing! */
	}

	if (ciphers[crypti->sed_type].ctcpname)
	     snprintf(buffer, sizeof(buffer), "%c%s %s%c",
			CTCP_DELIM_CHAR, ciphers[crypti->sed_type].ctcpname, 
			dest, CTCP_DELIM_CHAR);
	else
		panic(1, "crypt_msg: crypti->sed_type == %d not supported.", crypti->sed_type);

	new_free(&ciphertext);
	new_free(&dest);
	return malloc_strdup(buffer);
}

/*
 * decrypt_msg: Convert ciphertext from irc into plain text.
 *
 * Whenever you receive a C string containing ctcp-enquoted ciphertext 
 * over irc, you call is_crypted() to fetch the session.  Then you pass
 * the C string and the session to this function, and it returns malloced
 * string containing the decrypted text.
 *
 *	str	- A C string containing CTCP-enquoted ciphertext
 *	crypti	- Something previously returned by is_crypted().
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
char *	decrypt_msg (const unsigned char *str, Crypt *crypti)
{
	char *	plaintext;
	char *	dest;
	size_t	destsize;
	int	destlen;

	/* Convert the ctcp-enquoted payload into ciphertext */
	destlen = strlen(str) * 2 + 2;
	dest = new_malloc(destlen);
	destsize = transform_string(CTCP_xform, XFORM_DECODE, NULL, str, strlen(str), dest, destlen);
	if (destsize == 0)
	{
		yell("decrypt_msg: Could not ctcp-dequote encrypted privmsg");
		return malloc_strdup(str);
	}

	if (!(plaintext = decipher_message(dest, destsize, crypti, &destlen)))
	{
		plaintext = dest;
		dest = NULL;
	}

	new_free(&dest);
	return plaintext;
}

const char *	happypasswd (const char *passwd, int sed_type)
{
	static char prettypasswd[BIG_BUFFER_SIZE];

	if (sed_type == AESSHA256CRYPT || sed_type == SEDSHACRYPT)
	{
		int	i;
		for (i = 0; i < 32; i++)		/* XXX */
		    snprintf(prettypasswd + (i * 2), 3, "%2.2X", 
				(unsigned)(unsigned char)passwd[i]);
	}
	else
		strlcpy(prettypasswd, passwd, sizeof(prettypasswd));

	return prettypasswd;
}




/**************************************************************************/

