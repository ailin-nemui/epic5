/*
 * crypt.h: header for crypt.c 
 *
 * Copyright 1990 Michael Sandrof
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __crypt_h__
#define __crypt_h__

/*
 * Crypt: the crypt list structure,  consists of the nickname, and the
 * encryption key 
 */
typedef struct	CryptStru
{
	struct	CryptStru *next;
	char	*nick;
	char	*key;
	char	*prog;
	int	filename;
}	Crypt;

	BUILT_IN_COMMAND(encrypt_cmd);
	char	*crypt_msg 	(char *, Crypt *);
	char	*decrypt_msg 	(char *, Crypt *);
	Crypt	*is_crypted 	(char *);
	void	my_decrypt 	(char *, int, char *);
	void	my_encrypt	(char *, int, char *);

#endif /* _CRYPT_H_ */
