/* $EPIC: mail.c,v 1.13 2003/09/25 03:24:20 jnelson Exp $ */
/*
 * mail.c -- a gross simplification of mail checking.
 * Only unix maildrops (``mbox'') are supported.
 *
 * Copyright © 1996, 2003 EPIC Software Labs.
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

#include "irc.h"
#include "mail.h"
#include "lastlog.h"
#include "hook.h"
#include "vars.h"
#include "ircaux.h"
#include "output.h"
#include "clock.h"
#include "timer.h"
#include "window.h"
#include "input.h"
#ifndef _POSIX_SOURCE
# define _POSIX_SOURCE
#endif
#include <utime.h>

static	int	mail_last_count = -1;
static	char *	mail_last_count_str = NULL;
static	int	mail_latch = 0;

/************************************************************************/
/*				MBOX SUPPORT				*/
/************************************************************************/
static	char *	mbox_path = (char *) 0;
static	time_t	mbox_last_changed = 0;
static	off_t	mbox_last_size = 0;


/*
 * init_mbox_checking:  Look for the user's mbox
 *
 * Look for an mbox-style mailbox.  If the user sets the MAIL environment
 * variable, we use that.  Otherwise, we look for a file by the user's name
 * in the usual directories (/var/spool/mail, /usr/spool/mail, /var/mail, 
 * and /usr/mail).  If we cannot find an mbox anywhere, we forcibly turn off
 * mail checking.  This will defeat the mail timer and save the user trouble.
 *
 * Return value:
 *	1 if an mbox was found.
 *	0 if an mbox was not found.
 */
static int	init_mbox_checking (void)
{
	const char *mbox_path_list = "/var/spool/mail:/usr/spool/mail:"
					"/var/mail:/usr/mail";
	Filename tmp_mbox_path;

	if (getenv("MAIL") && file_exists(getenv("MAIL")))
		mbox_path = malloc_strdup(getenv("MAIL"));
	else if (!path_search(username, mbox_path_list, tmp_mbox_path))
		mbox_path = malloc_strdup(tmp_mbox_path);
	else
	{
		say("I can't find your mailbox.");
		set_int_var(MAIL_VAR, 0);
		return 0;
	}

	return 1;
}

static int	deinit_mbox_checking (void)
{
	new_free(&mbox_path);
	mbox_last_changed = -1;
	mbox_last_size = 0;
	return 0;
}

/*
 * mbox_count -- return the number of emails in an mbox.
 * Ok.  This is lame, but so what?
 *
 * Return value:
 *  The number of emails in an mbox, done by counting the lines that start 
 *  with "From".  This probably wouldn't work on Content-length mboxes or 
 *  mboxes that don't put a > before lines that start with "From".
 */
static int	mbox_count (void)
{
	int	count = 0;
	FILE *	mail;
	char	buffer[256];

	if (!(mail = fopen(mbox_path, "r")))
		return 0;

	while (fgets(buffer, sizeof buffer, mail))
		if (!strncmp("From ", buffer, 5))
			count++;

	fclose(mail);
	return count;
}

/*
 * poll_mbox_status -- See if an mbox has changed since last time.
 *
 * There are a couple of global

 * Returns 0 if there is no mail.
 * Returns 1 if there is mail but the mbox hasn't changed.
 * Returns 2 if there is mail and the mbox has changed (need to recount)
 */
static int	poll_mbox_status (void *ptr)
{
	Stat	sb;
	Stat *	stat_buf;

	if (ptr)
		stat_buf = (Stat *)ptr;
	else
		stat_buf = &sb;

	if (!mbox_path)
		if (!init_mbox_checking())
			return 0;		/* Can't find mbox */

	/* If there is no mailbox, there is no mail! */
	if (stat(mbox_path, stat_buf) == -1)
		return 0;

	/* There is no mail. */
	if (stat_buf->st_size == 0)
		return 0;

	/* 
	 * If the mailbox has been written to (either because new
	 * mail has been appended or old mail has been disposed of
	 */
	if (stat_buf->st_ctime > mbox_last_changed)
		return 2;

	/* There is mail, but it's not new. */
	return 1;
}

static void	update_mail_level1_mbox (void)
{
	Stat	stat_buf;
	int	status;

	status = poll_mbox_status(&stat_buf);

	/* There is no mail */
	if (status == 0)
	{
		if (mail_last_count_str)
			new_free(&mail_last_count_str);
	}
	/* Mailbox changed. */
	else if (status == 2)
	{
	    /* If the mbox grew in size, new mail! */
	    if (stat_buf.st_size > mbox_last_size)
	    {
		/* Tell the user that they have new mail. */
		if (!mail_latch)
		{
			mail_latch++;
			if (do_hook(MAIL_LIST, "You have new email"))
			{
				int lastlog_level = 
				set_lastlog_msg_level(LOG_CRAP);
				say("You have new email.");
				set_lastlog_msg_level(lastlog_level);
			}
			mail_latch--;
		}

	        malloc_strcpy(&mail_last_count_str, empty_string);
	    }

	    /* 
	     * Mark the last time we checked mail, and revoke the
	     * "count", so it must be regened by /set mail 2.
	     */
	    mbox_last_changed = stat_buf.st_ctime;
	    mbox_last_size = stat_buf.st_size;
	    mail_last_count = -1;
	}
}

static void	update_mail_level2_mbox (void)
{
	Stat	stat_buf;
	int	status;
	int	count;

	status = poll_mbox_status(&stat_buf);

	/* There is no mail */
	if (status == 0)
	{
		mail_last_count = 0;
		if (mail_last_count_str)
			new_free(&mail_last_count_str);
	}

	/* If our count is invalid or there is new mail, recount mail */
	else if (mail_last_count == -1 || status == 2)
	{
	    /* So go ahead and recount the mails in mbox */
	    count = mbox_count();

	    if (count == 0)
		new_free(&mail_last_count_str);
	    else
	    {
		malloc_sprintf(&mail_last_count_str, "%d", count);

		/* 
		 * If there is new mail, or if we're switching to 
		 * /set mail 2, tell the user how many emails there are.
		 */
		if (count > mail_last_count)
		{
		    /* This is to avoid $0 in /on mail being wrong. */
		    if (mail_last_count < 0)
			mail_last_count = 0;

		    if (!mail_latch)
		    {
			mail_latch++;
			if (do_hook(MAIL_LIST, "%d %d", 
					count - mail_last_count, count))
			{
			    int lastlog_level = 
					set_lastlog_msg_level(LOG_CRAP);
			    say("You have new email.");
			    set_lastlog_msg_level(lastlog_level);
			}
			mail_latch--;
		    }
		}

		mbox_last_changed = stat_buf.st_ctime;
	        mbox_last_size = stat_buf.st_size;
		mail_last_count = count;
	    }
	}
}

static void	update_mail_level3_mbox (void)
{
	struct utimbuf	ts;
	Stat	stat_buf;
	int	status;

	status = poll_mbox_status(&stat_buf);

	update_mail_level2_mbox();
	if (status == 2)
	{
		/* XXX Ew.  Evil. Gross. */
		ts.actime = stat_buf.st_atime;
		ts.modtime = stat_buf.st_mtime;
		utime(mbox_path, &ts);	/* XXX Ick. Gross */
	}
}

/**************************************************************************/
struct mail_checker {
	const char *	name;
	int	(*init) (void);
	void	(*level1) (void);
	void	(*level2) (void);
	void	(*level3) (void);
	int	(*deinit) (void);
};

struct mail_checker mail_types[] = {
	{ "MBOX",	init_mbox_checking, 
			update_mail_level1_mbox,
			update_mail_level2_mbox,
			update_mail_level3_mbox,
			deinit_mbox_checking },
	{ NULL,		NULL, NULL, NULL, NULL, NULL }
};
struct mail_checker *checkmail = &mail_types[0];

/**************************************************************************/
const char *	check_mail (void)
{
	return mail_last_count_str;
}

char 	mail_timeref[] = "MAILTIM";

void	mail_systimer (void)
{
	int	x;

	switch ((x = get_int_var(MAIL_VAR)))
	{
		case 1:
			checkmail->level1();
			break;
		case 2:
			checkmail->level2();
			break;
		case 3:
			checkmail->level3();
			break;
		default:
			panic("mail_systimer called with set mail %d", x);
	}

	update_all_status();
	cursor_to_input();
	return;
}

void    set_mail_interval (const void *stuff)
{
}

void	set_mail (const void *stuff)
{
	int	value = *(const int *)stuff;

	if (value < 0 || value > 3)
	{
		say("/SET MAIL must be 0, 1, 2, or 3");
		set_int_var(MAIL_VAR, 0);
		return;
	}
	else if (value == 0)
		checkmail->deinit();
	else
	{
		if (!checkmail->init())
			return;
	}

	update_system_timer(mail_timeref);
	update_all_status();
	cursor_to_input();
}

