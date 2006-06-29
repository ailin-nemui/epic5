/*
 * crypt.h: header for crypt.c 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __crypt_h__
#define __crypt_h__

#define ANYCRYPT	-1
#define PROGCRYPT	0
#define	SEDCRYPT	1
#define CAST5CRYPT	2
#define BLOWFISHCRYPT	3
#define AES256CRYPT	4
#define AESSHA256CRYPT	5

/*
 * Crypt: the crypt list structure,  consists of the nickname, and the
 * encryption key 
 */
typedef struct	CryptStru
{
	struct	CryptStru *next;
	char *	nick;
	char *	key;
	int	type;
	char *	prog;
}	Crypt;

	BUILT_IN_COMMAND(encrypt_cmd);
	char *	crypt_msg 	(const char *, Crypt *);
	char *	decrypt_msg 	(const char *, Crypt *);
	Crypt *	is_crypted 	(const char *, int type);

	/* These are for internal use only -- do not call outside crypt.c */
	unsigned char *	decipher_message (const unsigned char *, size_t, Crypt *, int *);
	unsigned char *	cipher_message	(const unsigned char *, size_t, Crypt *, int *);
	char *  sha256str (const char *, size_t, char *);
	char *  sha256 (const char *, size_t, char *);

#endif /* _CRYPT_H_ */
