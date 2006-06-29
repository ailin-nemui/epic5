/* $EPIC: crypt.c,v 1.21 2006/06/29 01:13:53 jnelson Exp $ */
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

#define CRYPT_BUFFER_SIZE (IRCD_BUFFER_SIZE - 50)	/* Make this less than
							 * the transmittable
							 * buffer */

/* crypt_list: the list of nicknames and encryption keys */
static	Crypt	*crypt_list = (Crypt *) 0;
static const char *ciphertype[] = {
	"External Program",
	"SED",
	"CAST5",
	"BLOWFISH",
	"AES-256",
	"AES-SHA-256"
};

/*
 * add_to_crypt: adds the nickname and key pair to the crypt_list.  If the
 * nickname is already in the list, then the key is changed the the supplied
 * key. 
 */
static void	add_to_crypt (const char *nick, const char *key, const char *prog, int type)
{
	Crypt	*new_crypt;

	/* Create a 'new_crypt' if one doesn't already exist */
	if (!(new_crypt = is_crypted(nick, type)))
	{
		new_crypt = (Crypt *) new_malloc(sizeof(Crypt));
		new_crypt->nick = NULL;
		new_crypt->key = NULL;
		new_crypt->prog = NULL;
	}

	/* Fill in the 'nick' field. */
	malloc_strcpy(&new_crypt->nick, nick);

	/* Fill in the 'key' field. */
	if (type == AES256CRYPT)
	{
		if (new_crypt->key == NULL)
			new_crypt->key = new_malloc(32);
		memset(new_crypt->key, 0, 32);
		memcpy(new_crypt->key, key, strlen(key));
	}
	else if (type == AESSHA256CRYPT)
	{
		if (new_crypt->key == NULL)
			new_crypt->key = new_malloc(32);
		sha256(key, strlen(key), new_crypt->key);
	}
	else
		malloc_strcpy(&new_crypt->key, key);

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

/*
 * remove_crypt: removes the given nickname from the crypt_list, returning 0
 * if successful, and 1 if not (because the nickname wasn't in the list) 
 */
static int	remove_crypt (const char *nick, int type)
{
	Crypt	*item;

	if ((item = is_crypted(nick, type)) &&
	     (remove_item_from_list((List **)&crypt_list, (List *)item)))
	{
		new_free((char **)&(item->nick));
		new_free((char **)&(item->key));
		new_free((char **)&(item->prog));
		new_free((char **)&item);
		return (0);
	}
	return (1);
}

/*
 * is_crypted: looks up nick in the crypt_list and returns the encryption key
 * if found in the list.  If not found in the crypt_list, null is returned. 
 */
Crypt *	is_crypted (const char *nick, int type)
{
	Crypt *	tmp;
	Crypt *	best = NULL;
	int	bestval = -1;

	if (!crypt_list)
		return NULL;
	for (tmp = crypt_list; tmp; tmp = tmp->next)
	{
		if (!my_stricmp(tmp->nick, nick))
		{
			if (type == ANYCRYPT && tmp->type > bestval)
			{
				best = tmp;
				bestval = tmp->type;
			}
			else if (type == tmp->type)
				return tmp;
		}
	}
	return best;
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
		say("       Where TYPE is SED, CAST, BLOWFISH, AES or AESSHA");
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
		say("       Where TYPE is SED, CAST, BLOWFISH, AES or AESSHA");
		return;
	    }
	}

	if (nick)
	{
	    if (key)
	    {
		add_to_crypt(nick, key, prog, type);
		if (prog)
		    say("Will now cipher messages with '%s' using '%s' "
			"with the key '%s'.",
				nick, prog ? prog : ciphertype[type], key);
	    }
	    else if (remove_crypt(nick, type))
		say("Not ciphering messages with '%s' using '%s'",
			nick, prog ? prog : ciphertype[type], key);
	    else
		say("Will no longer cipher messages with '%s' using '%s'.",
			nick, prog ? prog : ciphertype[type]);
	}

	else if (crypt_list)
	{
		Crypt	*tmp;

		say("The crypt:");
		for (tmp = crypt_list; tmp; tmp = tmp->next)
		    put_it("Ciphering messages with '%s' using '%s' "
			   "with the key '%s'.",
				tmp->nick, 
				tmp->prog ? tmp->prog : ciphertype[type], 
				key);
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
static char *	do_crypt (const char *str, Crypt *key, int flag)
{
	int	i;
	size_t	c;
	char	*free_it = NULL;
	unsigned char *my_str;

	i = (int)strlen(str);
	if (flag)
	{
		free_it = my_str = cipher_message(str, strlen(str) + 1, 
							key, &i);
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
char *	crypt_msg (const char *str, Crypt *key)
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
char *	decrypt_msg (const char *str, Crypt *key)
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
