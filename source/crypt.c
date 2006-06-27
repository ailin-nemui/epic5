/* $EPIC: crypt.c,v 1.20 2006/06/27 02:51:22 jnelson Exp $ */
/*
 * crypt.c: Canoodling message payloads to thwart eavesdropping.
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

/*
 * Some will look at these routines and recoil in horror with their insecurity.
 * It is true that SED ("Simple Encrypted Data", although maybe "Slightly 
 * Entropic Data" is closer to truth) is an ECB (Electronic Cook Book -- see 
 * http://en.wikipedia.org/wiki/Cipher_block_chaining), and does not provide 
 * a defense against cryptoanalysis of your messages.
 *
 * But what SED does provide is an effective defense against server-side 
 * pattern matching.  This is useful for discretely sending /DCC offers.
 * Because a DCC SEND reveals not only your real IP address, but also a port
 * from which you will send a file to the first taker, and because the server
 * sees this information before anyone else does, the security of your DCCs
 * are only as secure as your server admin.  For those who use rogue networks,
 * you would be unwise to offer DCCs without using SED.
 *
 * Alas, CTCP-over-SED is only compatable with EPIC and BitchX.
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

/*
 * add_to_crypt: adds the nickname and key pair to the crypt_list.  If the
 * nickname is already in the list, then the key is changed the the supplied
 * key. 
 */
static	void	add_to_crypt(char *nick, char *key, char* prog, int type)
{
	Crypt	*new_crypt;

	if ((new_crypt = (Crypt *) remove_from_list((List **)&crypt_list, nick)) != NULL)
	{
		new_free((char **)&(new_crypt->nick));
		new_free((char **)&(new_crypt->key));
		new_free((char **)&(new_crypt->prog));
		new_free((char **)&new_crypt);
	}
	new_crypt = (Crypt *) new_malloc(sizeof(Crypt));
	new_crypt->nick = (char *) 0;
	new_crypt->key = (char *) 0;
	new_crypt->prog = (char *) 0;
	new_crypt->type = type;

	malloc_strcpy(&(new_crypt->nick), nick);
	if (type == AES256CRYPT)
	{
		new_crypt->key = new_malloc(32);
		memset(new_crypt->key, 0, 32);
		memcpy(new_crypt->key, key, strlen(key));
	}
	else
		malloc_strcpy(&(new_crypt->key), key);

	if (prog && *prog)
	{
		malloc_strcpy(&(new_crypt->prog), prog);
		new_crypt->type = EXTCRYPT;
	}
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
		new_free((char **)&(tmp->prog));
		new_free((char **)&tmp);
		return (0);
	}
	return (1);
}

/*
 * is_crypted: looks up nick in the crypt_list and returns the encryption key
 * if found in the list.  If not found in the crypt_list, null is returned. 
 */
Crypt	*is_crypted (const char *nick, int type)
{
	Crypt	*tmp;

	if (!crypt_list)
		return NULL;
	for (tmp = crypt_list; tmp; tmp = tmp->next)
	{
		if (!my_stricmp(tmp->nick, nick) && tmp->type == type)
			return tmp;
	}
	return NULL;
}

/*
 * encrypt_cmd: the ENCRYPT command.  Adds the given nickname and key to the
 * encrypt list, or removes it, or list the list, you know. 
 */
BUILT_IN_COMMAND(encrypt_cmd)
{
	char	*nick, *key, *prog, *typestr;
	int	type = SEDCRYPT;

	if ((nick = next_arg(args, &args)) != NULL)
	{
	    if ((typestr = next_arg(args, &args)) != NULL)
	    {
		if (!my_stricmp(typestr, "-SED"))
			type = SEDCRYPT;
		else if (!my_stricmp(typestr, "-CAST"))
			type = CAST5CRYPT;
		else if (!my_stricmp(typestr, "-BLOWFISH"))
			type = BLOWFISHCRYPT;
		else if (!my_stricmp(typestr, "-AES"))
			type = AES256CRYPT;
		else
		{
			say("You must specify an encrypt type: /ENCRYPT nick -TYPE key, where TYPE is either -SED, -CAST, -BLOWFISH, or -AES");
			return;
		}

		if ((key = new_next_arg(args, &args)) != NULL)
		{
			prog = new_next_arg(args, &args);
			add_to_crypt(nick, key, prog, type);
			say("%s added to the crypt with key %s and program %s",
					nick, key, prog?prog:"[none]");
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
		say("Usage: /ENCRYPT nick -TYPE key");
	}
	else if (crypt_list)
	{
		Crypt	*tmp;

		say("The crypt:");
		for (tmp = crypt_list; tmp; tmp = tmp->next)
			put_it("%s with key %s and program %s",
				tmp->nick, tmp->key, tmp->prog?tmp->prog:"[none]");
	}
	else
		say("The crypt is empty");
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

static char * 	prog_crypt (char *str, size_t *len, Crypt *key, int flag)
{
	char	*ret = NULL, *input;
	char *	args[3];
	int	iplen;

	args[0] = malloc_strdup(key->prog);
	args[1] = malloc_strdup(flag ? "encrypt" : "decrypt");
	args[2] = NULL;

	input = malloc_strdup2(key->key, "\n");
	iplen = strlen(input);
	new_realloc((void**)&input, *len + iplen);
	memmove(input + iplen, str, *len);
	*len += iplen;
	ret = exec_pipe(key->prog, input, len, args);
	new_free(&args[0]);
	new_free(&args[1]);
	new_free((char**)&input);
	new_realloc((void**)&ret, 1+*len);
	ret[*len] = 0;
	return ret;
}

char	*do_crypt (char *str, Crypt *key, int flag)
{
	size_t	c;
	char	*free_it = NULL;

	c = strlen(str);
	if (flag)
	{
		if (key->prog)
		{
			free_it = str = (char*)prog_crypt(str, &c, key, flag);
			str = enquote_it(str, c);
		}
		else if (key->type == SEDCRYPT)
		{
			my_encrypt(str, c, key->key);
			str = enquote_it(str, c);
		}
		else if (key->type == CAST5CRYPT || 
			 key->type == BLOWFISHCRYPT ||
			 key->type == AES256CRYPT)
			str = cipher_message(str, strlen(str) + 1, key);
	}
	else
	{
		if (key->prog)
		{
			str = dequote_it(str, &c);
			str = (char*)prog_crypt(free_it = str, &c, key, flag);
		}
		else if (key->type == SEDCRYPT)
		{
			str = dequote_it(str, &c);
			my_decrypt(str, c, key->key);
		}
		else if (key->type == CAST5CRYPT || 
			 key->type == BLOWFISHCRYPT ||
			 key->type == AES256CRYPT)
			str = decipher_message(str, key);
	}
	new_free(&free_it);
	return (str);
}

/*
 * crypt_msg: Given plaintext 'str', constructs a body suitable for sending
 * via PRIVMSG or DCC CHAT.
 */
char 	*crypt_msg (char *str, Crypt *key)
{
	char	buffer[CRYPT_BUFFER_SIZE + 1];
	char	thing[6];
	char	*ptr;

	if (key->type == SEDCRYPT)
		snprintf(thing, 6, "%cSED ", CTCP_DELIM_CHAR);
	else if (key->type == CAST5CRYPT)
		snprintf(thing, 16, "%cCAST128ED-CBC ", CTCP_DELIM_CHAR);
	else if (key->type == BLOWFISHCRYPT)
		snprintf(thing, 15, "%cBLOWFISH-CBC ", CTCP_DELIM_CHAR);
	else if (key->type == AES256CRYPT)
		snprintf(thing, 13, "%cAES256-CBC ", CTCP_DELIM_CHAR);

	*buffer = 0;
	if ((ptr = do_crypt(str, key, 1)))
	{
		if (!*ptr) {
			yell("WARNING: Empty encrypted message, but message "
			     "sent anyway.  Bug?");
		}
		strlcat(buffer, thing, sizeof buffer);
		strlcat(buffer, ptr, sizeof buffer);
		strlcat(buffer, CTCP_DELIM_STR, sizeof buffer);
		new_free(&ptr);
	}
	else
		strlcat(buffer, str, sizeof buffer);

	return malloc_strdup(buffer);
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
char 	*decrypt_msg (char *str, Crypt *key)
{
	char	*buffer = (char *)new_malloc(BIG_BUFFER_SIZE + 1);
	char	*ptr;

	if ((ptr = do_crypt(str, key, 0)) != NULL)
	{
		strlcpy(buffer, ptr, CRYPT_BUFFER_SIZE + 1);
		new_free(&ptr);
	}
	else
		strlcat(buffer, str, CRYPT_BUFFER_SIZE + 1);

	return buffer;
}
