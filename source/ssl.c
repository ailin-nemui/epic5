/*
 * ssl.c: SSL connection functions
 *
 * Original framework written by Juraj Bednar
 * Modified by B. Thomas Frazier
 *
 * Copyright 2000, 2002 EPIC Software Labs
 *
 */

#include "irc.h"
#include "ircaux.h"

#ifdef HAVE_SSL
#include "ssl.h"

char	err_buf[256];

SSL_CTX	*SSL_CTX_init (int server)
{
	SSL_CTX	*ctx;
	
	SSLeay_add_ssl_algorithms();
	SSL_load_error_strings();
	ctx = SSL_CTX_new(server ? SSLv3_server_method() : SSLv3_client_method());
	SSL_CTX_set_session_cache_mode(ctx, SSL_SESS_CACHE_BOTH);
	SSL_CTX_set_timeout(ctx, 300);
	return(ctx);
}

SSL *SSL_FD_init (SSL_CTX *ctx, int des)
{
	SSL	*ssl;
	if (!(ssl = SSL_new(ctx)))
	{
		return NULL;
		panic("SSL_FD_init() critical error in SSL_new()");
	}
	SSL_set_fd(ssl, des);
	SSL_connect(ssl);
	return(ssl);
}

#endif
