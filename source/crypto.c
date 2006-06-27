/* $EPIC: crypto.c,v 1.2 2006/06/27 02:51:22 jnelson Exp $ */
/*
 * crypto.c: Calling out to OpenSSL to encrypt/decrypt cast5-cbc messages.
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

/*
 *	-About CAST5
 *
 * IRCII includes support for cast5-cbc (ircII calls it cast128) which is a
 * symmetric cipher that uses a 128 bit key (hense cast128) and a 64 bit 
 * initialization vector.
 *
 * IRCII sends the a CTCP message:
 *	PRIVMSG <target> :\001CAST128ED-CBC <payload>\001\r\n
 *
 * Where the <payload> is composed of:
 *	<Initialization Vector>		An 8-byte random Cast IV
 *	<Blocks>			Some number of 8 byte blocks
 *	<Final Block>			A final block with up to 7 bytes of 
 *					data the final byte tells you how many
 *					bytes should be ignored.
 *
 * The whole thing is encrypted with CAST5-CBC.  Well, the IV is not encrypted
 * but you still have to run it through the decrypter because the CBC won't be
 * set up correctly if you don't.
 *
 * If a message is not divisible by 8 chars, and does not have at least two
 * blocks (an IV and a Final Block), it is probably a rogue message and should 
 * be discarded.
 *
 * The ircII format is almost compatable with openssl -- whereas openssl
 * expects the all of the fill bytes in <Final Block> to be the same, ircII
 * fills them in with random chars.  We must decrypt the string in unbuffered
 * modeor openssl will throw fits.  We handle the fill bytes ourself.
 *
 * When we encrypt a message, we let openssl do the buffering, so the fill 
 * bytes are not random, but ircII doesn't care.
 */

/*
 *	-About BlowFish
 *
 * EPIC supports BlowFish-cbc in the same format as Cast5-CBC:
 *	PRIVMSG <target> :\001BLOWFISH-CBC <payload>\001\r\n
 * with the same <payload> as cast5.  This is only supported with EPIC for
 * now.  It is not compatable with FiSH (a plugin for mirc, irssi, and xchat)
 */

/*
 *	- About FiSH
 *
 * EPIC does not support FiSH yet.  Fish looks like this:
 *	PRIVMSG <target> :+OK <payload>\r\n
 * where <payload> are 64 byte blocks encoded with base64 into 12 characters.
 */

#include "irc.h"
#include "sedcrypt.h"
#include "ctcp.h"
#include "ircaux.h"
#include "list.h"
#include "output.h"
#include "vars.h"
#include "words.h"
#ifdef HAVE_SSL
#include <openssl/evp.h>
#include <openssl/err.h>

static char *	cipher_evp (const char *key, int keylen, const char *plaintext, int plaintextlen, const EVP_CIPHER *type, int *retsize, int ivsize);
static char *	decipher_evp (const char *key, int keylen, const char *ciphertext, int cipherlen, const EVP_CIPHER *type, int iv_size);
#endif

char *	decipher_message (unsigned char *orig_message, Crypt *key)
{
	unsigned char *	copy, *free_it;
	size_t	copylen = 0, decipher_len = 0;

	/* Check for trailing new lines chars */
	copylen = strlen(orig_message);
	free_it = copy = dequote_it(orig_message, &copylen);

    do
    {
	if (key->type == CAST5CRYPT || key->type == BLOWFISHCRYPT)
	{
	    unsigned char *	outbuf = NULL;
#ifdef HAVE_SSL
	    const EVP_CIPHER *type;
	    int	bytes_to_trim;

	    if (copylen % 8 != 0)
	    {
		yell("Encrypted message [%s] isn't multiple of 8! (is %d)", 
				orig_message, copylen);
		break;
	    }
	    if (copylen < 16)
	    {
		yell("Encrypted message [%s] doesn't contain message! "
				"(copylen is %d)", orig_message, copylen);
		break;
	    }

	    if (key->type == CAST5CRYPT)
		type = EVP_cast5_cbc();
	    else if (key->type == BLOWFISHCRYPT)
		type = EVP_bf_cbc();
	    else
		break;		/* Not supported */

	    if (!(outbuf = decipher_evp(key->key, strlen(key->key), 
					copy, copylen, type, 8)))
	    {
		yell("bummer");
		break;
	    }

	    bytes_to_trim = outbuf[copylen - 1] & 0x07;
	    outbuf[copylen - bytes_to_trim - 1] = 0;
	    memmove(outbuf, outbuf + 8, copylen - 8);
#endif
	    new_free(&free_it);
	    return outbuf;
	}
	if (key->type == AES256CRYPT)
	{
	    unsigned char *	outbuf = NULL;
#ifdef HAVE_SSL
	    const EVP_CIPHER *type;
	    int	bytes_to_trim;

	    if (copylen % 16 != 0)
	    {
		yell("Encrypted message [%s] isn't multiple of 16! (is %d)", 
				orig_message, copylen);
		break;
	    }
	    if (copylen < 32)
	    {
		yell("Encrypted message [%s] doesn't contain message! "
				"(copylen is %d)", orig_message, copylen);
		break;
	    }

	    if (key->type == AES256CRYPT)
		type = EVP_aes_256_cbc();
	    else
		break;		/* Not supported */

	    if (!(outbuf = decipher_evp(key->key, 32, copy, copylen, type, 16)))
	    {
		yell("bummer");
		break;
	    }

	    bytes_to_trim = outbuf[copylen - 1] & 0x0F;
	    outbuf[copylen - bytes_to_trim - 1] = 0;
	    memmove(outbuf, outbuf + 16, copylen - 16);
#endif
	    new_free(&free_it);
	    return outbuf;
	}

	else
	{
		yell("HUH?");
		break;
	}
    }
    while (0);

	new_free(&free_it);
	return NULL;
}

#ifdef HAVE_SSL
static char *	decipher_evp (const char *key, int keylen, const char *ciphertext, int cipherlen, const EVP_CIPHER *type, int iv_size)
{
        unsigned char *outbuf;
        int     outlen = 0;
	unsigned char	*iv;
	unsigned long errcode;
        EVP_CIPHER_CTX a;
        EVP_CIPHER_CTX_init(&a);
	EVP_CIPHER_CTX_set_padding(&a, 0);

	iv = new_malloc(iv_size);
	outbuf = new_malloc(1024);
	memcpy(iv, ciphertext, iv_size);

        EVP_DecryptInit_ex(&a, type, NULL, NULL, iv);
	EVP_CIPHER_CTX_set_key_length(&a, keylen);
        EVP_DecryptInit_ex(&a, NULL, NULL, key, NULL);
        EVP_DecryptUpdate(&a, outbuf, &outlen, ciphertext, cipherlen);
        EVP_CIPHER_CTX_cleanup(&a);

	ERR_load_crypto_strings();
	while ((errcode = ERR_get_error()))
	{
	    char r[256];
	    ERR_error_string_n(errcode, r, 256);
	    yell("ERROR: %s", r);
	}

	new_free(&iv);
	outbuf[cipherlen] = 0;
	return outbuf;
}
#endif

char *	cipher_message (unsigned char *orig_message, size_t len, Crypt *key)
{
	unsigned char *	copy, *free_it;
	size_t	copylen = 0, decipher_len = 0;
	unsigned char *	outbuf;
	int	bytes_to_trim;
	int	retsize;

	if (key->type == CAST5CRYPT || key->type == BLOWFISHCRYPT)
	{
#ifdef HAVE_SSL
	    const EVP_CIPHER *type;

	    if (key->type == CAST5CRYPT)
		type = EVP_cast5_cbc();
	    else if (key->type == BLOWFISHCRYPT)
		type = EVP_bf_cbc();
	    else
		return NULL;	/* Not supported */

	    if (!(outbuf = cipher_evp(key->key, strlen(key->key), orig_message, 
					len, type, &retsize, 8)))
	    {
		yell("bummer");
		return NULL;
	    }
#endif
	}
	else if (key->type == AES256CRYPT)
	{
#ifdef HAVE_SSL
	    const EVP_CIPHER *type;

	    if (key->type == AES256CRYPT)
		type = EVP_aes_256_cbc();
	    else
		return NULL;	/* Not supported */

	    if (!(outbuf = cipher_evp(key->key, 32, orig_message, 
					len, type, &retsize, 16)))
	    {
		yell("bummer");
		return NULL;
	    }
#endif
	}
	else
	{
		yell("HUH?");
		return NULL;
	}

	return enquote_it(outbuf, retsize);
}

#ifdef HAVE_SSL
static char *	cipher_evp (const char *key, int keylen, const char *plaintext, int plaintextlen, const EVP_CIPHER *type, int *retsize, int ivsize)
{
        unsigned char *outbuf;
        int     outlen = 0;
	int	extralen = 0;
	unsigned char	*iv;
	unsigned long errcode;
	u_32int_t	randomval;
	int		iv_count;
        EVP_CIPHER_CTX a;
        EVP_CIPHER_CTX_init(&a);
	EVP_CIPHER_CTX_set_padding(&a, 0);

	iv = new_malloc(ivsize);
	for (iv_count = 0; iv_count < ivsize; iv_count += sizeof(u_32int_t))
	{
		randomval = arc4random();
		memmove(iv + iv_count, &randomval, sizeof(u_32int_t));
	}

	outbuf = new_malloc(1024);
	memcpy(outbuf, iv, ivsize);

        EVP_EncryptInit_ex(&a, type, NULL, NULL, iv);
	EVP_CIPHER_CTX_set_key_length(&a, keylen);
        EVP_EncryptInit_ex(&a, NULL, NULL, key, NULL);
        EVP_EncryptUpdate(&a, outbuf + ivsize, &outlen, plaintext, plaintextlen);
	EVP_EncryptFinal_ex(&a, outbuf + ivsize + outlen, &extralen);
        EVP_CIPHER_CTX_cleanup(&a);
	outlen += extralen;

	ERR_load_crypto_strings();
	while ((errcode = ERR_get_error()))
	{
	    char r[256];
	    ERR_error_string_n(errcode, r, 256);
	    yell("ERROR: %s", r);
	}

	*retsize = outlen + ivsize;
	return outbuf;
}
#endif

