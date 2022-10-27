/*
 * ssl.h -- header file for ssl.c
 *
 * Original framework written by Juraj Bednar
 * Modified by B. Thomas Frazier
 *
 * Copyright 2000, 2002 EPIC Software Labs
 *
 */

#ifndef __ssl_h__
#define __ssl_h__

#define MAX_ONELINE 256

typedef	struct	ssl_cert_error {
	struct ssl_cert_error *next;
	int	err;
	int	depth;
	char 	oneline[MAX_ONELINE];
} ssl_cert_error;

#if 0
typedef struct ssl_metadata {
	int	vfd;
	int	verify_result;
	char *	pem;
	char *	cert_hash;
	int	pkey_bits;
	char *	subject;
	char *	u_cert_subject;
	char *	issuer;
	char *	u_cert_issuer;
	char *	ssl_version;
} ssl_metadata;
#endif

	void	set_ssl_root_certs_location (void *);

	int     ssl_startup (int vfd, int channel, const char *hostname, const char *cert);
	int	ssl_connect (int nfd, int quiet);
	int	ssl_connected (int nfd);
	int	ssl_write (int nfd, const void *, size_t);
	int	ssl_read (int nfd, int quiet);
	int	ssl_shutdown (int nfd);

	int	is_fd_ssl_enabled (int nfd);
	int	client_ssl_enabled (void);

	const char *	get_ssl_cipher (int nfd);
	const char *	get_ssl_pem (int vfd);
	const char *	get_ssl_cert_hash (int vfd);
	int		get_ssl_pkey_bits (int vfd);
	const char *	get_ssl_subject (int vfd);
	const char *	get_ssl_u_cert_subject (int vfd);
	const char *	get_ssl_issuer (int vfd);
	const char *	get_ssl_u_cert_issuer (int vfd);
	const char *	get_ssl_ssl_version (int vfd);
	int     	get_ssl_strict_status (int vfd, int *retval);
	const char *	get_ssl_sans (int vfd);
	int		get_ssl_verify_error (int vfd); 
	int		get_ssl_checkhost_error (int vfd);
	int		get_ssl_self_signed_error (int vfd);
	int		get_ssl_other_error (int vfd);
	int		get_ssl_most_serious_error (int vfd);

#endif
