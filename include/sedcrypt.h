/*
 * crypt.h: header for crypt.c 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __crypt_h__
#define __crypt_h__

#define	SEDCRYPT	1
#define CAST5CRYPT	2
#define EXTCRYPT	3
#define BLOWFISHCRYPT	4

/*
 * Crypt: the crypt list structure,  consists of the nickname, and the
 * encryption key 
 */
typedef struct	CryptStru
{
	struct	CryptStru *next;
	char	*nick;
	char	*key;
	int	type;
	char	*prog;
	int	filename;
}	Crypt;

	BUILT_IN_COMMAND(encrypt_cmd);
	char	*crypt_msg 	(char *, Crypt *);
	char	*decrypt_msg 	(char *, Crypt *);
	char	*do_crypt	(char *, Crypt *, int);
	Crypt	*is_crypted 	(const char *, int type);
	void	my_decrypt 	(char *, int, char *);
	void	my_encrypt	(char *, int, char *);

	char *	decipher_message (unsigned char *, Crypt *);
	char *	cipher_message	(unsigned char *, size_t, Crypt *);

#endif /* _CRYPT_H_ */
