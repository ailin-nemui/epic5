/* $EPIC: mail.c,v 1.22 2006/06/17 04:04:02 jnelson Exp $ */
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
				int l = message_from(NULL, LEVEL_OTHER);
				say("You have new email.");
				pop_message_from(l);
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
			    int l = message_from(NULL, LEVEL_OTHER);
			    say("You have new email.");
			    pop_message_from(l);
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

/************************************************************************/
/*			MAILDIR SUPPORT					*/
/************************************************************************/
static	char *	maildir_path = (char *) 0;
static	time_t	maildir_last_changed = 0;


/*
 * init_maildir_checking:  Look for the user's maildir
 *
 * Return value:
 *	1 if a maildir was found.
 *	0 if a maildir was not found.
 */
static int	init_maildir_checking (void)
{
	Filename 	tmp_maildir_path;
	const char *	maildir;

	if ((maildir = getenv("MAILDIR")))
		maildir = getenv("MAIL");

	if (maildir == NULL || !file_exists(maildir) || !isdir(maildir))
	{
		if (maildir == NULL)
			say("Your MAIL environment variable is unset.");
		else if (!file_exists(maildir))
			say("The file in your MAIL environment variable "
				"does not exist.");
		else
			say("The file in your MAIL environment variable "
				"is not a directory.");
		return 0;
	}

	strlcpy(tmp_maildir_path, maildir, sizeof(Filename));
	strlcat(tmp_maildir_path, "/new", sizeof(Filename));
	if (!file_exists(tmp_maildir_path) || !isdir(tmp_maildir_path))
	{
		say("The directory in your MAIL environment variable "
			"does not contain a sub-directory called 'new'");
		return 0;
	}

	maildir_path = malloc_strdup(tmp_maildir_path);
	maildir_last_changed = -1;
	return 1;
}

static int	deinit_maildir_checking (void)
{
	new_free(&maildir_path);
	maildir_last_changed = -1;
	return 0;
}

/*
 * maildir_count -- return the number of emails in an maildir.
 *
 * Return value:
 *  The number of emails in an maildir, done by counting the files.
 */
static int	maildir_count (void)
{
	int	count = 0;
	DIR *	dir;

	if ((dir = opendir(maildir_path)))
	{
		while (readdir(dir) != NULL)
			count++;
		closedir(dir);
		count -= 2;	/* Don't count . or .. */
	}

	return count;
}

/*
 * poll_maildir_status -- See if a maildir has changed since last time.
 *
 * Returns 0 if there is no mail.
 * Returns 1 if there is mail but the maildir hasn't changed.
 * Returns 2 if there is mail and the maildir has changed (need to recount)
 */
static int	poll_maildir_status (void *ptr)
{
	Stat	sb;
	Stat *	stat_buf;

	if (ptr)
		stat_buf = (Stat *)ptr;
	else
		stat_buf = &sb;

	if (!maildir_path)
		if (!init_maildir_checking())
			return 0;		/* Can't find maildir */

	/* If there is no mailbox, there is no mail! */
	if (stat(maildir_path, stat_buf) == -1)
		return 0;

	/* 
	 * If the mailbox has been written to (either because new
	 * mail has been appended or old mail has been disposed of
	 */
	if (stat_buf->st_ctime > maildir_last_changed)
		return 2;

	/* There is mail, but it's not new. */
	return 1;
}

static void	update_mail_level1_maildir (void)
{
	int	status;
	Stat	stat_buf;

	status = poll_maildir_status(&stat_buf);

	/* There is no mail */
	if (status == 0)
	{
		if (mail_last_count_str)
			new_free(&mail_last_count_str);
	}
	/* Mailbox changed. */
	else if (status == 2)
	{
		int	count_new = maildir_count();

		/* There is no mail */
		if (count_new == 0)
		{
			if (mail_last_count_str)
				new_free(&mail_last_count_str);
		}

		/* Maildir changed. */
		if (count_new > mail_last_count)
		{
			/* Tell the user that they have new mail. */
			if (!mail_latch)
			{
				mail_latch++;
				if (do_hook(MAIL_LIST, "You have new email"))
				{
				    int l = message_from(NULL, LEVEL_OTHER);
				    say("You have new email.");
				    pop_message_from(l);
				}
				mail_latch--;
			}

			malloc_strcpy(&mail_last_count_str, empty_string);
		}

		/* 
		 * Mark the last time we checked mail, and revoke the
		 * "count", so it must be regened by /set mail 2.
		 */
		maildir_last_changed = stat_buf.st_ctime;
		mail_last_count = count_new;
	}
}

static void	update_mail_level2_maildir (void)
{
	Stat	stat_buf;
	int	status;
	int	count;

	status = poll_maildir_status(&stat_buf);

	/* There is no mail */
	if (status == 0)
	{
		mail_last_count = 0;
		if (mail_last_count_str)
			new_free(&mail_last_count_str);
	}

	/* If our count is invalid or there is new mail, recount mail */
	else if (status == 2 || (!mail_last_count_str || !*mail_last_count_str))
	{
	    /* So go ahead and recount the mails in maildir */
	    count = maildir_count();

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
			    int l = message_from(NULL, LEVEL_OTHER);
			    say("You have new email.");
			    pop_message_from(l);
			}
			mail_latch--;
		    }
		}

		maildir_last_changed = stat_buf.st_ctime;
		mail_last_count = count;
	    }
	}
}

static void	update_mail_level3_maildir (void)
{
	struct utimbuf	ts;
	Stat	stat_buf;
	int	status;

	status = poll_maildir_status(&stat_buf);

	update_mail_level2_maildir();
	if (status == 2)
	{
		/* XXX Ew.  Evil. Gross. */
		ts.actime = stat_buf.st_atime;
		ts.modtime = stat_buf.st_mtime;
		utime(maildir_path, &ts);	/* XXX Ick. Gross */
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
	{ "MAILDIR",	init_maildir_checking, 
			update_mail_level1_maildir,
			update_mail_level2_maildir,
			update_mail_level3_maildir,
			deinit_maildir_checking },
	{ NULL,		NULL, NULL, NULL, NULL, NULL }
};
struct mail_checker *checkmail = &mail_types[1];

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

void    set_mail_interval (void *stuff)
{
	update_system_timer(mail_timeref);
}

void	set_mail (void *stuff)
{
	VARIABLE *v;
	int	value;

	v = (VARIABLE *)stuff;
	value = v->integer;

	if (value < 0 || value > 3)
	{
		say("/SET MAIL must be 0, 1, 2, or 3");
		v->integer = value = 0;
	}
	if (value != 0)
	{
	    if (!checkmail->init())
		value = v->integer = 0;
	}
	if (value == 0)
		checkmail->deinit();

	update_system_timer(mail_timeref);
	update_all_status();
	cursor_to_input();
}

