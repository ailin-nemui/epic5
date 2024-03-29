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
/*
	char *	nick;
*/
	char *	serv;
	char *	passwd;
	int	passwdlen;
	int	sed_type;
	char *	prog;
	int	refnum;
}	Crypt;

	BUILT_IN_COMMAND(encrypt_cmd);
	char *	crypt_msg 	(const char *, List *);
	char *	decrypt_msg 	(const char *, List *);
	List *	is_crypted 	(const char *, int serv, const char *ctcp_type);

	/* These are for internal use only -- do not call outside crypt.c */
	char *	decipher_message (const char *, size_t, List *, int *);
	char *	cipher_message	(const char *, size_t, List *, int *);
	char *  sha256str (const char *, size_t, char *);
	char *  sha256 (const char *, size_t, char *);

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

	void     encrypt_sed (char *, int, const char *, int);
	void     decrypt_sed (char *, int, const char *, int);

#endif /* _CRYPT_H_ */
