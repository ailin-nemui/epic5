/*
 * crypto.c: SED/CAST5/BLOWFISH/AES encryption and decryption routines.
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
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
 *					data. The final byte tells you how many
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
 * mode or openssl will throw fits.  We handle the fill bytes ourself.
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
 *	-About AES-256
 *
 * EPIC supports AES-256-cbc in the same format as Cast5-CBC:
 *	PRIVMSG <target> :\001AES256-CBC <payload>\001\r\n
 * except that the <payload> is different:
 *	<Initialization Vector>		A 16-byte random IV (unused by AES)
 *	<Blocks>			Some number of 16 byte blocks
 *	<Final Block>			A final block with up to 15 bytes of
 *					data.  The final byte tells you how 
 *					many bytes should be ignored.
 *
 * The key for AES256 is always 256 bits (32 chars), no matter what.  EPIC 
 * fills short keys with nuls (ie, a password of 5 chars is padded with 27 
 * nuls to fill it out.)  Long keys are truncated at 32 chars.
 */

/*
 *	-About AES-SHA-256
 *
 * EPIC supports AES-SHA-256-cbc in the same format as AES256:
 *	PRIVMSG <target> :\001AESSHA256-CBC <payload>\001\r\n
 * except that the encryption key is generated by taking the sha256 of the
 * key instead of padding it out with nuls (as AES256 does).  This allows
 * an effectively unlimited key length.  
 *
 * Perhaps someday you'll be able to be able to key with a file instead of 
 * a passphrase.
 */

/*
 *	- About FiSH
 *
 * EPIC does not support FiSH yet.  Fish looks like this:
 *	PRIVMSG <target> :+OK <payload>\r\n
 * where <payload> are 64 byte blocks encoded with base64 into 12 characters.
 */

/*
 *	- About SED
 *
 * Some will look at these routines and recoil in horror with their insecurity.
 * It is true that SED ("Simple Encrypted Data", although maybe "Slightly 
 * Entropic Data" is closer to truth) is a XOR-CBC stream cipher that uses 
 * the same cipherkey for each message and is therefore subject to all the 
 * trivial stream cipher attacks.  So it doesn't provide security.
 *
 * But what SED does provide is an effective defense against server-side 
 * pattern matching.  This is useful for discretely sending /DCC offers.
 * Because a DCC SEND reveals not only your real IP address, but also a port
 * from which you will send a file to the first taker, and because the server
 * sees this information before anyone else does, the security of your DCCs
 * are only as secure as your server admin.  For those who use rogue networks,
 * using SED to send twiddle your DCC offers is better than nothing.
 *
 * Alas, CTCP-over-SED is only compatable with EPIC and BitchX.
 */

/*
 *	- About SED-SHA
 *
 * All XOR-CBC stream ciphers that re-use their key are subject to
 * substitution attacks (by man-in-the-middle who has two ciphertexts) and 
 * key recovery (by man-in-the-middle who has one plaintext).  SED-SHA does
 * not promise any better security than SED, but it does use SHA256() of your
 * cipherkey to make the key longer (32 bytes).
 *
 * The SED-SHA cipher is always available, since it doesn't depend on openssl.
 * Alas, SED-SHA is not going to be compatable with non-epic5 users.
 */


#include "irc.h"
#include "list.h"
#include "sedcrypt.h"
#include "ctcp.h"
#include "ircaux.h"
#include "output.h"
#include "vars.h"
#include "words.h"
#ifdef HAVE_SSL
#include <openssl/evp.h>
#include <openssl/err.h>
#endif

#ifdef HAVE_SSL
static char *	decipher_evp (const unsigned char *passwd, int passwdlen, const unsigned char *ciphertext, int cipherlen, const EVP_CIPHER *type, int *outlen, int ivsize);
#endif
static char *	decrypt_by_prog (const unsigned char *str, size_t *len, List *crypt);

#ifdef HAVE_SSL
static char *	cipher_evp (const unsigned char *passwd, int passwdlen, const unsigned char *plaintext, int plaintextlen, const EVP_CIPHER *type, int *retsize, int ivsize);
#endif
static char *	encrypt_by_prog (const unsigned char *str, size_t *len, List *crypt);

/* This function is used by CTCP handling, but not by xform! */
unsigned char *	decipher_message (const unsigned char *ciphertext, size_t len, List *cryptl, int *retlen)
{
    do
    {
	Crypt *crypti = (Crypt *)(cryptl->d);

	if (crypti->sed_type == CAST5CRYPT || crypti->sed_type == BLOWFISHCRYPT ||
	    crypti->sed_type == AES256CRYPT || crypti->sed_type == AESSHA256CRYPT ||
	    crypti->sed_type == FISHCRYPT)
	{
	    unsigned char *	outbuf = NULL;
#ifdef HAVE_SSL
	    const EVP_CIPHER *type;
	    int	bytes_to_trim;
	    int ivsize, blocksize;

	    if (crypti->sed_type == CAST5CRYPT)
	    {
		ivsize = 8, blocksize = 8;
	    }
	    else if (crypti->sed_type == BLOWFISHCRYPT)
	    {
		ivsize = 8, blocksize = 8;
	    }
	    else if (crypti->sed_type == FISHCRYPT)
	    {
		ivsize = 0, blocksize = 8;
	    }
	    else if (crypti->sed_type == AES256CRYPT || crypti->sed_type == AESSHA256CRYPT)
	    {
		ivsize = 16, blocksize = 16;
	    }
	    else
		return NULL;

	    if (blocksize > 0 && len % blocksize != 0)
	    {
		/* 
		 * XXX Sigh, you can't portably printf() a size_t, since 
		 * maybe it's an int, maybe it's a long, and any printf
		 * format that DTRT is too new to trust.  bleh.
		 */
		yell("Encrypted message [%s] isn't multiple of %d! (is %ld)", 
				ciphertext, blocksize, (long)len);
		break;
	    }
	    if ((int)len < blocksize + ivsize)
	    {
		yell("Encrypted message [%s] doesn't contain message! "
				"(len is %ld)", ciphertext, (long)len);
		break;
	    }

	    if (crypti->sed_type == CAST5CRYPT)
		type = EVP_cast5_cbc();
	    else if (crypti->sed_type == BLOWFISHCRYPT)
		type = EVP_bf_cbc();
	    else if (crypti->sed_type == FISHCRYPT)
		type = EVP_bf_ecb();
	    else if (crypti->sed_type == AES256CRYPT || crypti->sed_type == AESSHA256CRYPT)
		type = EVP_aes_256_cbc();
	    else
		break;		/* Not supported */

	    if (!(outbuf = decipher_evp(crypti->passwd, crypti->passwdlen,
					ciphertext, len, 
					type, retlen, ivsize)))
	    {
		yell("bummer");
		break;
	    }

	    bytes_to_trim = outbuf[len - 1] & (blocksize - 1);
	    /* outbuf[len - bytes_to_trim - 1] = 0; */
	    outbuf[len - bytes_to_trim] = 0; 
	    memmove(outbuf, outbuf + ivsize, len - ivsize);
#endif
	    return outbuf;
	}
	else if (crypti->sed_type == SEDCRYPT || crypti->sed_type == SEDSHACRYPT)
	{
		unsigned char *	text;

		text = new_malloc(len + 1);
		memmove(text, ciphertext, len);
		decrypt_sed(text, len, crypti->passwd, crypti->passwdlen);
		*retlen = len;
		return text;
	}
	else if (crypti->sed_type == PROGCRYPT)
	{
		unsigned char *retval;

		retval = decrypt_by_prog(ciphertext, &len, cryptl);
		*retlen = len;
		return retval;
	}
	else
		panic(1, "decipher_message: crypti->sed_type %d is not valid", crypti->sed_type);
    }
    while (0);

	return NULL;
}

#ifdef HAVE_SSL
static char *	decipher_evp (const unsigned char *passwd, int passwdlen, const unsigned char *ciphertext, int cipherlen, const EVP_CIPHER *type, int *outlen, int ivsize)
{
        unsigned char *outbuf;
	unsigned char	*iv = NULL;
	unsigned long errcode;
	int	outlen2;
        EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();

	if(context == NULL) {
		yell("ERROR: Could not generate cipher context");
		return NULL;
	}

	EVP_CIPHER_CTX_set_padding(context, 0);

	if (ivsize > 0)
		iv = new_malloc(ivsize);
	outbuf = new_malloc(cipherlen + 1024);
	if (ivsize > 0)
		memcpy(iv, ciphertext, ivsize);

        EVP_DecryptInit_ex(context, type, NULL, NULL, iv);
	EVP_CIPHER_CTX_set_key_length(context, passwdlen);
	EVP_CIPHER_CTX_set_padding(context, 0);
        EVP_DecryptInit_ex(context, NULL, NULL, passwd, NULL);

        if (EVP_DecryptUpdate(context, outbuf, outlen, ciphertext, cipherlen) != 1)
		yell("EVP_DecryptUpdate died.");
	if (EVP_DecryptFinal_ex(context, outbuf + (*outlen), &outlen2) != 1)
		yell("EVP_DecryptFinal_Ex died.");
	*outlen += outlen2;

        EVP_CIPHER_CTX_free(context);

	ERR_load_crypto_strings();
	while ((errcode = ERR_get_error()))
	{
	    char r[256];
	    ERR_error_string_n(errcode, r, 256);
	    yell("ERROR: %s", r);
	}

	if (ivsize > 0)
		new_free(&iv);
	return outbuf;
}
#endif

void     decrypt_sed (unsigned char *str, int len, const unsigned char *passwd, int passwdlen)
{
        int	passwd_pos,
                i;
        char    mix,
                tmp;

        if (!passwd)
                return;                 /* no decryption */

        passwdlen = strlen(passwd);
        passwd_pos = 0;
        mix = 0;

        for (i = 0; i < len; i++)
        {
                tmp = mix ^ str[i] ^ passwd[passwd_pos]; 
                str[i] = tmp;
                mix ^= tmp;
                passwd_pos = (passwd_pos + 1) % passwdlen;
        }
        str[i] = (char) 0;
}

static char *	decrypt_by_prog (const unsigned char *str, size_t *len, List *cryptl)
{
        char    *ret = NULL, *input;
        char *  args[3];
        int     iplen;
	Crypt *	crypti = (Crypt *)(cryptl->d);

        args[0] = malloc_strdup(crypti->prog);
        args[1] = malloc_strdup("decrypt");
        args[2] = NULL;
        input = malloc_strdup2(crypti->passwd, "\n");

        iplen = strlen(input);
        new_realloc((void**)&input, *len + iplen);
        memmove(input + iplen, str, *len);

        *len += iplen;
        ret = exec_pipe(crypti->prog, input, len, args);

        new_free(&args[0]);
        new_free(&args[1]);
        new_free((char**)&input);

        new_realloc((void**)&ret, 1+*len);
        ret[*len] = 0;
        return ret;
}

/*************************************************************************/
unsigned char *	cipher_message (const unsigned char *orig_message, size_t len, List *cryptl, int *retlen)
{
	Crypt *	crypti = (Crypt *)(cryptl->d);

	if (retlen)
		*retlen = 0;
	if (!orig_message || !crypti || !retlen)
		return NULL;

	if (crypti->sed_type == CAST5CRYPT || crypti->sed_type == BLOWFISHCRYPT ||
	    crypti->sed_type == FISHCRYPT ||
	    crypti->sed_type == AES256CRYPT || crypti->sed_type == AESSHA256CRYPT)
	{
	    unsigned char *ciphertext = NULL;
#ifdef HAVE_SSL
	    size_t	ivlen;
	    const EVP_CIPHER *type;

	    if (crypti->sed_type == CAST5CRYPT)
	    {
		type = EVP_cast5_cbc();
		ivlen = 8;
	    }
	    else if (crypti->sed_type == BLOWFISHCRYPT)
	    {
		type = EVP_bf_cbc();
		ivlen = 8;
	    }
	    else if (crypti->sed_type == FISHCRYPT)
	    {
		type = EVP_bf_ecb();
		ivlen = 0;		/* XXX Sigh */
	    }
	    else if (crypti->sed_type == AES256CRYPT || crypti->sed_type == AESSHA256CRYPT)
	    {
		type = EVP_aes_256_cbc();
		ivlen = 16;
	    }
	    else
		return NULL;	/* Not supported */

	    if (!(ciphertext = cipher_evp(crypti->passwd, crypti->passwdlen,
				      orig_message, len, 
				      type, retlen, ivlen)))
	    {
		yell("bummer");
		return NULL;
	    }
#endif
	    return ciphertext;
	}
	else if (crypti->sed_type == SEDCRYPT || crypti->sed_type == SEDSHACRYPT)
	{
		unsigned char *	ciphertext;

		ciphertext = new_malloc(len + 1);
		memmove(ciphertext, orig_message, len);
		encrypt_sed(ciphertext, len, crypti->passwd, strlen(crypti->passwd));
		*retlen = len;
		return ciphertext;
	}
	else if (crypti->sed_type == PROGCRYPT)
	{
		unsigned char *ciphertext;

		ciphertext = encrypt_by_prog(orig_message, &len, cryptl);
		*retlen = len;
		return ciphertext;
	}
	else
		panic(1, "cipher_message: crypti->sed_type %d is not valid", crypti->sed_type);

	return NULL;
}

#ifdef HAVE_SSL
static char *	cipher_evp (const unsigned char *passwd, int passwdlen, const unsigned char *plaintext, int plaintextlen, const EVP_CIPHER *type, int *retsize, int ivsize)
{
        unsigned char *outbuf;
        int     outlen = 0;
	int	extralen = 0;
	unsigned char	*iv = NULL;
	unsigned long errcode;
	u_32int_t	randomval;
	int		iv_count;
        EVP_CIPHER_CTX *context = EVP_CIPHER_CTX_new();

	if(context == NULL) {
		yell("ERROR: Could not generate cipher context");
		return NULL;
	}

	EVP_CIPHER_CTX_set_padding(context, 0);

	if (ivsize < 0)
		ivsize = 0;		/* Shenanigans! */

	if (ivsize > 0)
	{
	    if (ivsize % sizeof(u_32int_t) != 0)
		panic(1, "The IV size for a crypto type you're using is %d "
			"which is not a multiple of %ld", 
			ivsize, (long)sizeof(u_32int_t));

	    iv = new_malloc(ivsize);
	    for (iv_count = 0; iv_count < ivsize; iv_count += sizeof(u_32int_t))
	    {
		randomval = arc4random();  
		memmove(iv + iv_count, &randomval, sizeof(u_32int_t));
	    }
	}

	outbuf = new_malloc(plaintextlen + 100);
	if (iv)
		memcpy(outbuf, iv, ivsize);

        EVP_EncryptInit_ex(context, type, NULL, NULL, iv);
	EVP_CIPHER_CTX_set_key_length(context, passwdlen);
        EVP_EncryptInit_ex(context, NULL, NULL, passwd, NULL);
        EVP_EncryptUpdate(context, outbuf + ivsize, &outlen, plaintext, plaintextlen);
	EVP_EncryptFinal_ex(context, outbuf + ivsize + outlen, &extralen);
        EVP_CIPHER_CTX_free(context);
	outlen += extralen;

	ERR_load_crypto_strings();
	while ((errcode = ERR_get_error()))
	{
	    char r[256];
	    ERR_error_string_n(errcode, r, 256);
	    yell("ERROR: %s", r);
	}

	*retsize = outlen + ivsize;
	if (iv)
		new_free(&iv);		/* XXX Is this correct? */
	return outbuf;
}
#endif

void     encrypt_sed (unsigned char *str, int len, const unsigned char *passwd, int passwd_len)
{
        int     passwd_pos,
                i;
        char    mix, 
                tmp;

        if (!passwd)
                return;                 /* no encryption */

        passwd_len = strlen(passwd);
        passwd_pos = 0;
        mix = 0;

        for (i = 0; i < len; i++)
        {
                tmp = str[i];
                str[i] = mix ^ tmp ^ passwd[passwd_pos];
                mix ^= tmp;
                passwd_pos = (passwd_pos + 1) % passwd_len;
        }
        str[i] = (char) 0;
}

static char *	encrypt_by_prog (const unsigned char *str, size_t *len, List *cryptl)
{
        char    *ret = NULL, *input;
        char *  args[3];
        int     iplen;
	Crypt *	crypti = (Crypt *)(cryptl->d);

        args[0] = malloc_strdup(crypti->prog);
        args[1] = malloc_strdup("encrypt");
        args[2] = NULL;
        input = malloc_strdup2(crypti->passwd, "\n");

        iplen = strlen(input);
        new_realloc((void**)&input, *len + iplen);
        memmove(input + iplen, str, *len);

        *len += iplen;
        ret = exec_pipe(crypti->prog, input, len, args);

        new_free(&args[0]);
        new_free(&args[1]);
        new_free((char**)&input);

        new_realloc((void**)&ret, 1+*len);
        ret[*len] = 0;
        return ret;
}

/**************************************************************************/
#ifdef HAVE_SSL
static void	ext256_passwd (const char *orig, size_t orig_len, char **passwd, size_t *passwdlen)
{
	size_t	len;

	if (orig_len < 32)
		len = orig_len;
	else
		len = 32;

	*passwd = new_malloc(32);
	memset(*passwd, 0, 32);
	memcpy(*passwd, orig, len);
	*passwdlen = 32;
}

static void	sha256_passwd (const char *orig, size_t orig_len, char **passwd, size_t *passwdlen)
{
	*passwd = new_malloc(32);
	sha256(orig, orig_len, *passwd);
	*passwdlen = 32;
}

static void	copy_passwd (const char *orig, size_t orig_len, char **passwd, size_t *passwdlen)
{
	*passwd = malloc_strdup(orig);
	*passwdlen = orig_len;
}

/*
 * These are helper functions for $xform() to do SSL strong crypto.
 * XXX These are cut and pasted from decipher_message. 
 */
#define CRYPTO_HELPER_FUNCTIONS(x, y, blocksize, ivsize, make_passwd, trim) \
ssize_t	x ## _encoder (const char *orig, size_t orig_len, const void *meta, size_t meta_len, char *dest, size_t dest_len) \
{ 									\
	int	retsize = 0; 						\
	char *	retval; 						\
	char *	realpasswd;						\
	size_t	realpasswdlen;						\
									\
	if (orig_len == 0) 						\
	{ 								\
		*dest = 0; 						\
		return 0; 						\
	} 								\
									\
	make_passwd (meta, meta_len, &realpasswd, &realpasswdlen);	\
	retval = cipher_evp(realpasswd, realpasswdlen, orig, orig_len,	\
				y (), &retsize, ivsize); 		\
	if (retval && retsize > 0) 					\
	{ 								\
		size_t	numb; 						\
		if (dest_len < (size_t)retsize) 			\
			numb = dest_len; 				\
		else 							\
			numb = retsize; 				\
		memcpy(dest, retval, numb); 				\
		dest[numb] = 0; 					\
		new_free(&retval); 					\
		return numb; 						\
	} 								\
									\
	if (retval) 							\
		new_free(&retval); 					\
	new_free(&realpasswd);						\
	return 0; 							\
}									\
									\
ssize_t	x ## _decoder (const char *ciphertext, size_t len, const void *meta, size_t meta_len, char *dest, size_t dest_len) \
{ 									\
	unsigned char *	outbuf = NULL; 					\
	int	bytes_to_trim = 0;					\
	int 	retlen = 0; 						\
	char *	realpasswd;						\
	size_t	realpasswdlen;						\
									\
	if (len == 0)							\
	{								\
		*dest = 0; 						\
		return 0; 						\
	}								\
									\
	make_passwd (meta, meta_len, &realpasswd, &realpasswdlen);	\
	if (!(outbuf = decipher_evp(realpasswd, realpasswdlen, ciphertext, len, \
				y (), &retlen, ivsize))) 		\
	{ 								\
		yell("bummer"); 					\
		return -1; 						\
	} 								\
									\
	if ( trim )							\
	{								\
		bytes_to_trim = outbuf[len - 1]; 			\
		if (bytes_to_trim > blocksize)				\
		{							\
			yell("Wants to trim %d byts with block size of %d",\
				bytes_to_trim, blocksize);		\
			bytes_to_trim = blocksize - 1;			\
		}							\
		outbuf[len - bytes_to_trim] = 0;  			\
		memmove(outbuf, outbuf + ivsize, len - bytes_to_trim); 	\
	}								\
									\
	memcpy(dest, outbuf, retlen); 					\
	dest[retlen] = 0; 						\
	new_free(&outbuf);						\
	return retlen;							\
}

CRYPTO_HELPER_FUNCTIONS(blowfish, EVP_bf_cbc, 8, 8, copy_passwd, 1)
CRYPTO_HELPER_FUNCTIONS(fish, EVP_bf_ecb, 8, 0, copy_passwd, 1)
CRYPTO_HELPER_FUNCTIONS(cast5, EVP_cast5_cbc, 8, 8, copy_passwd, 1)
CRYPTO_HELPER_FUNCTIONS(aes, EVP_aes_256_cbc, 16, 16, ext256_passwd, 1)
CRYPTO_HELPER_FUNCTIONS(aessha, EVP_aes_256_cbc, 16, 16, sha256_passwd, 1)
#endif


