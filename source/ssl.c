#include "irc.h"
#include "ircaux.h"

#ifdef HAVE_SSL
#include "ssl.h"

// struct	cipher_info	cipher_info;
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
		panic("SSL_FD_init critical error in SSL_new");
	}
	SSL_set_fd(ssl, des);
	SSL_connect(ssl);
	return(ssl);
}

/*
struct cipher_info *
SSL_get_cipher_info (SSL * ssl)
{
	SSL_CIPHER *c;

	c = SSL_get_current_cipher(ssl);
	strncpy(cipher_info.version, SSL_CIPHER_get_version(c),
		sizeof (cipher_info.version));
	strncpy(cipher_info.cipher, SSL_CIPHER_get_name(c),
		sizeof (chiper_info.chiper));
	SSL_CIPHER_get_bits(c, &cipher_info.cipher_bits);
	return(&cipher_info);
}
*/

#endif
