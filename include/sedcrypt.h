/*
 * crypt.h: header for crypt.c 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __crypt_h__
#define __crypt_h__

#define NOCRYPT		-2
#define ANYCRYPT	-1
#define PROGCRYPT	0
#define	SEDCRYPT	1
#define SEDSHACRYPT	2
#define CAST5CRYPT	3
#define BLOWFISHCRYPT	4
#define AES256CRYPT	5
#define AESSHA256CRYPT	6
#define FISHCRYPT	7

/*
 * Crypt: the crypt list structure,  consists of the nickname, and the
 * encryption key 
 */
typedef struct	CryptStru
{
	struct	CryptStru *next;
	char *	nick;
	char *	serv;
	char *	passwd;
	int	passwdlen;
	int	sed_type;
	char *	prog;
	int	refnum;
}	MAY_ALIAS Crypt;

	BUILT_IN_COMMAND(encrypt_cmd);
	char *	crypt_msg 	(const unsigned char *, Crypt *);
	char *	decrypt_msg 	(const unsigned char *, Crypt *);
	Crypt *	is_crypted 	(const char *, int serv, const char *ctcp_type);

	/* These are for internal use only -- do not call outside crypt.c */
	unsigned char *	decipher_message (const unsigned char *, size_t, Crypt *, int *);
	unsigned char *	cipher_message	(const unsigned char *, size_t, Crypt *, int *);
	char *  sha256str (const char *, size_t, char *);
	char *  sha256 (const char *, size_t, char *);

#ifdef HAVE_SSL
	ssize_t blowfish_encoder (const char *, size_t, const void *, size_t, char *, size_t);
	ssize_t blowfish_decoder (const char *, size_t, const void *, size_t, char *, size_t);
	ssize_t cast5_encoder (const char *, size_t, const void *, size_t, char *, size_t);
	ssize_t cast5_decoder (const char *, size_t, const void *, size_t, char *, size_t);
	ssize_t aes_encoder (const char *, size_t, const void *, size_t, char *, size_t);
	ssize_t aes_decoder (const char *, size_t, const void *, size_t, char *, size_t);
	ssize_t aessha_encoder (const char *, size_t, const void *, size_t, char *, size_t);
	ssize_t aessha_decoder (const char *, size_t, const void *, size_t, char *, size_t);
	ssize_t fish_encoder (const char *, size_t, const void *, size_t, char *, size_t);
	ssize_t fish_decoder (const char *, size_t, const void *, size_t, char *, size_t);
#endif

	void     encrypt_sed (unsigned char *, int, const unsigned char *, int);
	void     decrypt_sed (unsigned char *, int, const unsigned char *, int);

#endif /* _CRYPT_H_ */
