/*
 * mail.c -- a gross simplification of mail checking.
 * Only unix maildrops are supported.
 *
 * Written by Jeremy Nelson
 * Copyright 1996 EPIC Software Labs
 */

#include "irc.h"
#include "mail.h"
#include "lastlog.h"
#include "hook.h"
#include "vars.h"
#include "ircaux.h"
#include "output.h"
#ifndef _POSIX_SOURCE
# define _POSIX_SOURCE
#endif
#include <sys/stat.h>
#include <utime.h>

static	char	*mail_path = (char *) 0;

/*
 * Returns 0 if there is no mail.
 * Returns 1 if the status of the mailbox hasnt changed since last time.
 * Returns 2 if the status of the mailbox has changed since last time.
 */
int	check_mail_status (void *ptr)
{
	static	time_t	old_stat = 0;
	struct stat	sb;
	struct stat *	stat_buf;

	if (ptr)
		stat_buf = (struct stat *)ptr;
	else
		stat_buf = &sb;

	if (!get_int_var(MAIL_VAR))
	{
		old_stat = 0;
		return 0;
	}

	if (!mail_path)
	{
		char *mail_path_list = "/var/spool/mail:/usr/spool/mail:/var/mail:/usr/mail";
		char *tmp_mail_path;

		if (!(tmp_mail_path = getenv("MAIL")))
			tmp_mail_path = path_search(username, mail_path_list);

		if (tmp_mail_path)
			mail_path = m_strdup(tmp_mail_path);

		else
			mail_path = m_strdup("<unknown>");
	}

	/* If there is no mailbox, there is no mail! */
	if (stat(mail_path, stat_buf) == -1)
		return 0;

	/* 
	 * If the mailbox has been written to (either because new
	 * mail has been appended or old mail has been disposed of
	 */
	if (stat_buf->st_ctime > old_stat)
	{
		old_stat = stat_buf->st_ctime;
		if (stat_buf->st_size)
			return 2;
	}

	/*
	 * If there is something in the mailbox
	 */
	if (stat_buf->st_size)
		return 1;

	/*
	 * The mailbox is empty.
	 */
	return 0;
}


/*
 * check_mail:  report on status of inbox.
 *
 * If /SET MAIL is 0:
 *	return NULL
 *
 * If /SET MAIL is 1, and your inbox is:
 *	empty -- returns NULL
 *	not empty -- returns the empty string
 * If mailbox is larger than it was last time we checked,
 *	throw an /ON MAIL event.
 *
 * If /SET MAIL is 2, and your inbox is:
 *	empty -- returns NULL
 *	not empty -- returns ": <no emails in inbox>"
 * If mailbox has more emails than it did last time we checked,
 *	throw an /ON MAIL event.
 */
char	*check_mail (void)
{
	struct stat 	stat_buf;
	int		state;

	switch ((state = get_int_var(MAIL_VAR)))
	{
		case 0:
			return NULL;
		case 1:
		{
			static	int	mail_latch = 0;

			if (check_mail_status(&stat_buf))
			{
			    if (!mail_latch)
			    {
				mail_latch = 1;
				if (do_hook(MAIL_LIST, "You have new email"))
				{
				    int lastlog_level = 
					set_lastlog_msg_level(LOG_CRAP);
				    say("You have new email.");
				    set_lastlog_msg_level(lastlog_level);
				}
			    }
			    return empty_string;
			}

			if (mail_latch)
				mail_latch = 0;

			return NULL;
		}
		case 2:
		case 3:
		{
			FILE *	mail;
			char 	buffer[255];
			int 	count = 0;
		static 	int 	old_count = 0;
		static	char 	ret_str[12];
		struct utimbuf	ts;

			switch (check_mail_status(&stat_buf))
			{
			  case 2:
			  {
			    if (!(mail = fopen(mail_path, "r")))
				return NULL;

			    while (fgets(buffer, 254, mail))
				if (!strncmp("From ", buffer, 5))
					count++;

			    fclose(mail);

			    if (state == 3)
			    {
				/* XXX Ew.  Evil. Gross. */
				ts.actime = stat_buf.st_atime;
				ts.modtime = stat_buf.st_mtime;
				utime(mail_path, &ts);	/* XXX Ick. Gross */
			    }

			    if (count > old_count)
			    {
				if (do_hook(MAIL_LIST, "%d %d", 
					count - old_count, count))
				{
				    int lastlog_level = 
					set_lastlog_msg_level(LOG_CRAP);
				    say("You have new email.");
				    set_lastlog_msg_level(lastlog_level);
				}
			    }

			    old_count = count;
			    sprintf(ret_str, "%d", old_count);
			    /* FALLTHROUGH */
			  }

			  case 1:
				return ret_str;

			  default:
				old_count = 0;
				return NULL;
			}
		}
	}
	return NULL;
}
