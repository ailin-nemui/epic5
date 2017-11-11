/*
 * exec.c: handles exec'd process for IRCII 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1997, 2014 EPIC Software Labs
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

#include "all.h"

#ifdef NO_JOB_CONTROL
BUILT_IN_COMMAND(execcmd)
{ 
	say("Your system does not support job control, sorry.");
	return; 
}
int	get_child_exit (pid_t x)			{ return -1; }
void	clean_up_processes (void)			{ return; }
int	text_to_process (int x, const char *y, int z) 	
{ 
	say("Cannot send text to process without job control, sorry.");
	return 1; 
}
void	exec_server_delete (int x)			{ return; }
void	add_process_wait (int x, const char *y)		{ return; }
int	get_process_index (char **x)			{ return -1; }
int	is_valid_process (const char *x)		{ return -1; }
int	process_is_running (char *x)			{ return 0; }
#else

#include <sys/wait.h>
#include <sys/ioctl.h>
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif


/* Process: the structure that has all the info needed for each process */
typedef struct
{
	int	index;			/* Where in the proc array it is */
	char	*name;			/* full process name */
	char	*logical;
	pid_t	pid;			/* process-id of process */
	int	p_stdin;		/* stdin description for process */
	int	p_stdout;		/* stdout descriptor for process */
	int	p_stderr;		/* stderr descriptor for process */
	int	counter;		/* output line counter for process */
	char	*redirect;		/* redirection command (MSG, NOTICE) */
	unsigned refnum;		/* a window for output to go to */
	int	server;			/* the server to use for output */
	char	*who;			/* nickname used for redirection */
	int	exited;			/* true if process has exited */
	int	termsig;		/* The signal that terminated
					 * the process */
	int	retcode;		/* return code of process */
	List	*waitcmds;		/* commands queued by WAIT -CMD */
	int	dumb;			/* 0 if input still going, 1 if not */
	int	disowned;		/* 1 if we let it loose */
	char	*stdoutc;
	char	*stdoutpc;
	char	*stderrc;
	char	*stderrpc;
}	Process;


static	Process **process_list = NULL;
static	int	process_list_size = 0;

static 	void 	handle_filedesc 	(Process *, int *, int, int);
static 	void 	cleanup_dead_processes 	(void);
static 	void 	ignore_process 		(int);
static 	void 	exec_close_in		(int);
static 	void 	exec_close_out		(int);
static 	void 	kill_process 		(int, int);
static 	void 	kill_all_processes 	(int signo);
static 	int 	valid_process_index 	(int proccess);
static 	int 	is_logical_unique 	(char *logical);
static	int 	logical_to_index 	(const char *logical);
static	int	index_to_target 	(int, char *, size_t);
static	void 	do_exec 		(int fd);

/*
 * exec: the /EXEC command.  Does the whole shebang for ircII sub procceses
 * I melded in about half a dozen external functions, since they were only
 * ever used here.  Perhaps at some point theyll be broken out again, but
 * not until i regain control of this module.
 */
BUILT_IN_COMMAND(execcmd)
{
	const char	*who = NULL;
	char		*logical = NULL;
	const char	*flag;
	const char 	*redirect = NULL;
	unsigned 	refnum = 0;
	size_t		len;
	int		sig,
			i,
			refnum_flag = 0,
			logical_flag = 0;
	int		direct = 0;
	Process		*proc;
	char		*stdoutc = NULL,
			*stderrc = NULL,
			*stdoutpc = NULL,
			*stderrpc = NULL,
			*endc = NULL;

	/*
	 * If no args are given, list all the processes.
	 */
	if (!*args)
	{
		if (!process_list)
		{
			say("No processes are running");
			return;
		}

		say("Process List:");
		for (i = 0; i < process_list_size; i++)
		{
			if (((proc = process_list[i])) == NULL)
				continue;

			if (proc->logical)
				say("\t%d (%s) (pid %d): %s", 
				    i, proc->logical, proc->pid, proc->name);
			else
				say("\t%d (pid %d): %s", 
				    i, proc->pid, proc->name);
		}
		return;
	}

	/*
	 * Walk through and parse out all the args
	 */
	while ((*args == '-') && (flag = next_arg(args, &args)))
	{
		flag++;
		len = strlen(flag);

		/*
		 * /EXEC -OUT redirects all output from the /exec to the
		 * current channel for the current window.
		 */
		if (my_strnicmp(flag, "OUT", len) == 0)
		{
			if (get_server_doing_privmsg(from_server))
				redirect = "NOTICE";
			else
				redirect = "PRIVMSG";

			if (!(who = get_target_by_refnum(0)))
			{
			     say("No query or channel in this window for -OUT");
			     return;
			}
		}

		/*
		 * /EXEC -NAME gives the /exec a logical name that can be
		 * refered to as %name 
		 */
		else if (my_strnicmp(flag, "NAME", len) == 0)
		{
			logical_flag = 1;
			if (!(logical = next_arg(args, &args)))
			{
				say("You must specify a logical name");
				return;
			}
		}

		/*
		 * /EXEC -WINDOW forces all output for an /exec to go to
		 * the window that is current at this time
		 */
		else if (my_strnicmp(flag, "WINDOW", len) == 0)
		{
			refnum_flag = 1;
			refnum = current_refnum();
		}

		/*
		 * /EXEC -MSG <target> redirects the output of an /exec 
		 * to the given target.
		 */
		else if (my_strnicmp(flag, "MSG", len) == 0)
		{
			if (get_server_doing_privmsg(from_server))
				redirect = "NOTICE";
			else
				redirect = "PRIVMSG";

			if (!(who = next_arg(args, &args)))
			{
				say("No nicknames specified");
				return;
			}
		}

		/*
		 * /EXEC -LINE  specifies the stdout callback
		 */
		else if (my_strnicmp(flag, "LINE", len) == 0)
		{
			if ((stdoutc = next_expr(&args, '{')) == NULL)
				say("Need {...} argument for -LINE flag");
		}

		/*
		 * /EXEC -LINEPART specifies the stdout partial line callback
		 */
		else if (my_strnicmp(flag, "LINEPART", len) == 0)
		{
			if ((stdoutpc = next_expr(&args, '{')) == NULL)
				say("Need {...} argument for -LINEPART flag");
		}

		/*
		 * /EXEC -ERROR specifies the stderr callback
		 */
		else if (my_strnicmp(flag, "ERROR", len) == 0)
		{
			if ((stderrc = next_expr(&args, '{')) == NULL)
				say("Need {...} argument for -ERROR flag");
		}

		/*
		 * /EXEC -ERRORPART specifies the stderr part line callback
		 */
		else if (my_strnicmp(flag, "ERRORPART", len) == 0)
		{
			if ((stderrpc = next_expr(&args, '{')) == NULL)
				say("Need {...} argument for -ERRORPART flag");
		}

		/*
		 * /EXEC -END  specifies the final collection callback
		 */
		else if (my_strnicmp(flag, "END", len) == 0)
		{
			if ((endc = next_expr(&args, '{')) == NULL)
				say("Need {...} argument for -END flag");
		}

		/*
		 * /EXEC -CLOSE forcibly closes all the fd's to a process,
		 * in the hope that it will take the hint.
		 */
		else if (my_strnicmp(flag, "CLOSE", len) == 0)
		{
			if ((i = get_process_index(&args)) == -1)
				return;

			ignore_process(i);
			return;
		}

		/*
		 * /EXEC -CLOSEIN close the processes STDIN,
		 * in the hope that it will take the hint.
		 */
		else if (my_strnicmp(flag, "CLOSEIN", len) == 0)
		{
			if ((i = get_process_index(&args)) == -1)
				return;

			exec_close_in(i);
			return;
		}

		/*
		 * /EXEC -CLOSEIN close the processes STDIN,
		 * in the hope that it will take the hint.
		 */
		else if (my_strnicmp(flag, "CLOSEOUT", len) == 0)
		{
			if ((i = get_process_index(&args)) == -1)
				return;

			exec_close_out(i);
			return;
		}

		/*
		 * /EXEC -NOTICE <target> redirects the output of an /exec 
		 * to a specified target
		 */
		else if (my_strnicmp(flag, "NOTICE", len) == 0)
		{
			redirect = "NOTICE";
			if (!(who = next_arg(args, &args)))
			{
				say("No nicknames specified");
				return;
			}
		}

		/*
		 * /EXEC -IN sends a line of text to a process
		 */
		else if (my_strnicmp(flag, "IN", len) == 0)
		{
			if ((i = get_process_index(&args)) == -1)
				return;

			text_to_process(i, args, 1);
			return;
		}

		/*
		 * /EXEC -DIRECT suppresses the use of a shell
		 */
		else if (my_strnicmp(flag, "DIRECT", len) == 0)
			direct = 1;


		/*
		 * All other - arguments are implied KILLs
		 */
		else
		{
			if (*args != '%')
			{
				say("%s is not a valid process", args);
				return;
			}

			/*
			 * Check for a process to kill
			 */
			if ((i = get_process_index(&args)) == -1)
				return;

			/*
			 * Handle /exec -<num> %<process>
			 */
			if ((sig = my_atol(flag)) > 0)
			{
				if ((sig > 0) && (sig < NSIG))
					kill_process(i, sig);
				else
					say("Signal number can be from 1 to %d", NSIG);
				return;
			}

			/*
			 * Handle /exec -<SIGNAME> %<process>
			 */
			for (sig = 1; sig < NSIG; sig++)
			{
				if (!get_signal_name(sig))
					continue;
				if (!my_strnicmp(get_signal_name(sig), flag, len))
				{
					kill_process(i, sig);
					return;
				}
			}

			/*
			 * Give up! =)
			 */
			say("No such signal: %s", flag);
			return;
		}
	}

	/*
	 * This handles the form:
	 *
	 *	/EXEC <flags> %process
	 *
	 * Where the user wants to redefine some options for %process.
	 */
	if (*args == '%')
	{
		int	l;

		/*
		 * Make sure the process is actually running
		 */
		if ((i = get_process_index(&args)) == -1)
			return;

		proc = process_list[i];
		l = message_setall(refnum, NULL, LEVEL_OTHER);

		/*
		 * Check to see if the user wants to change windows
		 */
		if (refnum_flag)
		{
			proc->refnum = refnum;
			if (refnum)
say("Output from process %d (%s) now going to this window", i, proc->name);
			else
say("Output from process %d (%s) not going to any window", i, proc->name);
		}

		/*
		 * Check to see if the user is changing the default target
		 */
		malloc_strcpy(&(proc->redirect), redirect);
		malloc_strcpy(&(proc->who), who);

		if (redirect)
say("Output from process %d (%s) now going to %s", i, proc->name, who);
		else
say("Output from process %d (%s) now going to you", i, proc->name);

		/*
		 * Check to see if the user changed the NAME of %proc.
		 */
		if (logical_flag)
		{
			if (is_logical_unique(logical))
			{
				malloc_strcpy(&proc->logical, logical);
				say("Process %d (%s) is now called %s",
					i, proc->name, proc->logical);
			} 
			else 
				say("The name %s is not unique!", logical);
		}

		/*
		 * Change the stdout and stderr lines _if_ they are given.
		 */
		if (stdoutc)
			malloc_strcpy(&proc->stdoutc, stdoutc);
		if (stdoutpc)
			malloc_strcpy(&proc->stdoutpc, stdoutpc);
		if (stderrc)
			malloc_strcpy(&proc->stderrc, stderrc);
		if (stderrpc)
			malloc_strcpy(&proc->stderrpc, stderrpc);

		pop_message_from(l);
	}

	/*
	 * The user is trying to fire up a new /exec, so pass the buck.
	 */
	else
	{
		int	p0[2], p1[2], p2[2],
			pid, cnt;
		char	*shell,
			*arg;
		char	*name;

		name = LOCAL_COPY(args);

		if (!is_logical_unique(logical))
		{
			say("The name %s is not unique!", logical);
			return;
		}

		p0[0] = p1[0] = p2[0] = -1;
		p0[1] = p1[1] = p2[1] = -1;

		/*
		 * Open up the communication pipes
		 */
		if (pipe(p0) || pipe(p1) || pipe(p2))
		{
			say("Unable to start new process: %s", strerror(errno));
			close(p0[0]);
			close(p0[1]);
			close(p1[0]);
			close(p1[1]);
			close(p2[0]);
			close(p2[1]);
			return;
		}

		switch ((pid = fork()))
		{
		case -1:
			say("Couldn't start new process!");
			close(p0[0]);
			close(p0[1]);
			close(p1[0]);
			close(p1[1]);
			close(p2[0]);
			close(p2[1]);
			break;

		/*
		 * CHILD: set up and exec the process
		 */
		case 0:
		{
			/*
			 * Fire up a new job control session,
			 * Sever all ties we had with the parent ircII process
			 */
			setsid();
			if (setgid(getgid()))
				_exit(0);
			if (setuid(getuid()))
				_exit(0);
			my_signal(SIGINT, SIG_IGN);
			my_signal(SIGQUIT, SIG_DFL);
			my_signal(SIGSEGV, SIG_DFL);
			my_signal(SIGBUS, SIG_DFL);

			dup2(p0[0], 0);
			dup2(p1[1], 1);
			dup2(p2[1], 2);
			for (i = 3; i < 256; i++)
				close(i);

			/*
			 * Pretend to be just a dumb terminal
			 */
			setenv("TERM", "tty", 1);

			/*
			 * Figure out what shell (if any) we're using
			 */
			shell = get_string_var(SHELL_VAR);

			/*
			 * If we're not using a shell, doovie up the exec args
			 * array and pass it off to execvp
			 */
			if (direct || !shell)
			{
				int	max;
				char **my_args;

				cnt = 0;
				max = 5;
				my_args = new_malloc(sizeof(char *) * max);
				while ((arg = new_next_arg(name, &name)))
				{
					my_args[cnt] = arg;

					if (++cnt >= max)
					{
						max += 5;
						RESIZE(my_args, char *, max);
					}
				}
				my_args[cnt] = NULL;
				execvp(my_args[0], my_args);

				printf("*** Error running program \"%s\": %s\n",
						args, strerror(errno));
			}

			/*
			 * If we're using a shell, let them have all the fun
			 */
			else
			{
				if (!(flag = get_string_var(SHELL_FLAGS_VAR)))
					flag = empty_string;

				execl(shell, shell, flag, name, NULL);

				printf("*** Error running program \"%s %s\": %s\n", 
						shell, args, strerror(errno));
			}

			/*
			 * Something really died if we got here
			 */
			_exit(-1);
			break;
		}

		/*
		 * PARENT: add the new process to the process table list
		 */
		default:
		{
			proc = new_malloc(sizeof(Process));
			close(p0[0]);
			close(p1[1]);
			close(p2[1]);

			/*
			 * Init the proc list if neccesary
			 */
			if (!process_list)
			{
			    process_list = new_malloc(sizeof(Process *));
			    process_list_size = 1;
			    process_list[0] = NULL;
			}

			/*
			 * Find the first empty proc entry
			 */
			for (i = 0; i < process_list_size; i++)
			{
				if (!process_list[i])
				{
					process_list[i] = proc;
					break;
				}
			}

			/*
			 * If there are no empty proc entries, make a new one.
			 */
			if (i == process_list_size)
			{
			    process_list_size++;
			    RESIZE(process_list, Process *, process_list_size);
			    process_list[i] = proc;
			}

			/*
			 * Fill in the new proc entry
			 */
			proc->name = malloc_strdup(name);
			if (logical)
				proc->logical = malloc_strdup(logical);
			else
				proc->logical = NULL;
			proc->index = i;
			proc->pid = pid;
			proc->p_stdin = p0[1];
			proc->p_stdout = p1[0];
			proc->p_stderr = p2[0];
			proc->refnum = refnum;
			proc->redirect = NULL;
			if (redirect)
				proc->redirect = malloc_strdup(redirect);
			proc->server = from_server;
			proc->counter = 0;
			proc->exited = 0;
			proc->termsig = 0;
			proc->retcode = 0;
			proc->waitcmds = NULL;
			proc->who = NULL;
			proc->disowned = 0;
			if (who)
				proc->who = malloc_strdup(who);
			proc->dumb = 0;

			if (stdoutc)
				proc->stdoutc = malloc_strdup(stdoutc);
			else
				proc->stdoutc = NULL;

			if (stdoutpc)
				proc->stdoutpc = malloc_strdup(stdoutpc);
			else
				proc->stdoutpc = NULL;

			if (stderrc)
				proc->stderrc = malloc_strdup(stderrc);
			else
				proc->stderrc = NULL;

			if (stderrpc)
				proc->stderrpc = malloc_strdup(stderrpc);
			else
				proc->stderrpc = NULL;

			if (endc)
				add_process_wait(proc->index, endc);

			new_open(proc->p_stdout, do_exec, NEWIO_READ, 1, proc->server);
			new_open(proc->p_stderr, do_exec, NEWIO_READ, 1, proc->server);
			break;
		}
		}
	}

	cleanup_dead_processes();
}

/*
 * do_processes: This is called from the main io() loop to handle any
 * pending /exec'd events.  All this does is call handle_filedesc() on
 * the two reading descriptors.  If an EOF is asserted on either, then they
 * are closed.  If EOF has been asserted on both, then  we mark the process
 * as being "dumb".  Once it is reaped (exited), it is expunged.
 */
void 		do_exec (int fd)
{
	int	i;
	int	limit;

	if (!process_list)
		return;

	limit = get_int_var(SHELL_LIMIT_VAR);
	for (i = 0; i < process_list_size; i++)
	{
		Process *proc = process_list[i];

		if (!proc)
			continue;

		if (proc->p_stdout != -1 && proc->p_stdout == fd)
		{
			handle_filedesc(proc, &proc->p_stdout, 
					EXEC_PROMPT_LIST, EXEC_LIST);
		}

		if (proc->p_stderr != -1 && proc->p_stderr == fd)
		{
			handle_filedesc(proc, &proc->p_stderr,
					EXEC_PROMPT_LIST, EXEC_ERRORS_LIST);
		}

		if (limit && proc->counter >= limit)
			ignore_process(proc->index);
	}

	/* Clean up any (now) dead processes */
	cleanup_dead_processes();
}

/*
 * This is the back end to do_processes, saves some repeated code
 */
static void 	handle_filedesc (Process *proc, int *fd, int hook_nonl, int hook_nl)
{
	char 	exec_buffer[IO_BUFFER_SIZE + 1];
	ssize_t	len;
	int	ofs;
	const char *callback = NULL;
	int	hook = -1;
	int	l;
	char	logical_name[1024];
	const char	*utf8_text;
	char *extra = NULL;

	/* No buffering! */
	switch ((len = dgets(*fd, exec_buffer, IO_BUFFER_SIZE, 0))) 
	{
	    case -1:		/* Something died */
	    {
		*fd = new_close(*fd);
		if (proc->p_stdout == -1 && proc->p_stderr == -1)
			proc->dumb = 1;
		return;				/* PUNT! */
	    }

	    case 0:		/* We didnt get a full line */
	    {
		/* 
		 * XXX This is a hack.  dgets() can return 0 for a line
		 * containing solely a newline, as well as a line that didn't
		 * have a newline.  So we have to check to see if the line 
		 * contains only a newline!
		 */
		if (exec_buffer[0] != '\n')
		{
		    if (hook_nl == EXEC_LIST)
		    {
			if (proc->stdoutpc && *proc->stdoutpc)
			    callback = proc->stdoutpc;
		    }
		    else if (hook_nl == EXEC_ERRORS_LIST)
		    {
			if (proc->stderrpc && *proc->stderrpc)
			    callback = proc->stderrpc;
		    }
		    hook = hook_nonl;
		    break;
		}

		/* XXX HACK -- Line contains only a newline.  */
		*exec_buffer = 0;
		/* FALLTHROUGH */
	    }

	    default:		/* We got a full line */
	    {
		if (hook_nl == EXEC_LIST)
		{
		    if (proc->stdoutc && *proc->stdoutc)
			callback = proc->stdoutc;
		}
		else if (hook_nl == EXEC_ERRORS_LIST)
		{
		    if (proc->stderrc && *proc->stderrc)
			callback = proc->stderrc;
		}
		hook = hook_nl;
		break;
	    }
	}

	ofs = from_server;
	from_server = proc->server;
	if (proc->refnum)
		l = message_setall(proc->refnum, NULL, LEVEL_OTHER);
	else
		l = message_from(NULL, LEVEL_OTHER);

	proc->counter++;

	while (len > 0 && (exec_buffer[len - 1] == '\n' ||
			   exec_buffer[len - 1] == '\r'))
	     exec_buffer[--len] = 0;

	index_to_target(proc->index, logical_name, sizeof(logical_name));
	utf8_text = inbound_recode(logical_name, proc->server, empty_string, exec_buffer, &extra);

	if (proc->redirect) 
	     redirect_text(proc->server, proc->who, 
				utf8_text, proc->redirect, 1);

	if (callback)
	    call_lambda_command("EXEC", callback, utf8_text);
	else if (proc->logical)
	{
	     if ((do_hook(hook, "%s %s", proc->logical, utf8_text)))
		if (!proc->redirect)
		    put_it("%s", utf8_text);
	}
	else
	{
	    if ((do_hook(hook, "%d %s", proc->index, utf8_text)))
		if (!proc->redirect)
		    put_it("%s", utf8_text);
	}

	new_free(&extra);
	pop_message_from(l);
	from_server = ofs;
}

/*
 * get_child_exit: This looks for dead child processes of the client.
 * There are two main sources of dead children:  Either an /exec'd process
 * has exited, or the client has attempted to fork() off a helper process
 * (such as wserv or gzip) and that process has choked on itself.
 *
 * When SIGCHLD is recieved, the global variable 'dead_children_processes'
 * is incremented.  When this function is called, we go through and call
 * waitpid() on all of the outstanding zombies, conditionally stopping when
 * we reach a specific wanted sub-process.
 *
 * If you want to stop reaping children when a specific subprocess is 
 * reached, specify the process in 'wanted'.  If all youre doing is cleaning
 * up after zombies and /exec's, then 'wanted' should be -1.
 */
int 		get_child_exit (pid_t wanted)
{
	Process	*proc;
	pid_t	pid;
	int	status, i;

	/*
	 * Iterate until we've reaped all of the dead processes
	 * or we've found the one asked for.
	 */
	if (dead_children_processes)
	{
	    block_signal(SIGCHLD);
	    while ((pid = waitpid(wanted, &status, WNOHANG)) > 0)
	    {
		/*
		 * First thing we do is look to see if the process we're
		 * working on is the one that was asked for.  If it is,
		 * then we get its exit status information and return it.
		 */
		if (wanted != -1 && pid == wanted)
		{
			/* 
			 * We do not clear 'dead_children_processes' here
			 * because we do not know if we've reaped all of
			 * the children yet!  Leaving it set means this 
			 * function is called again, and then if there are
			 * no more left, it is cleared (below).
			 */
		        unblock_signal(SIGCHLD);

			if (WIFEXITED(status))
				return WEXITSTATUS(status);
			if (WIFSTOPPED(status))
				return -(WSTOPSIG(status));
			if (WIFSIGNALED(status)) 
				return -(WTERMSIG(status));
		}

		/*
		 * If it wasnt the process asked for, then we've probably
		 * stumbled across a dead /exec'd process.  Look for the
		 * corresponding child process, and mark it as being dead.
		 */
		else
		{
			for (i = 0; i < process_list_size; i++)
			{
				proc = process_list[i];
				if (proc && proc->pid == pid)
				{
					proc->exited = 1;
					proc->termsig = WTERMSIG(status);
					proc->retcode = WEXITSTATUS(status);
					break;
				}
			}
		}
	    }
	    dead_children_processes = 0;
	    unblock_signal(SIGCHLD);
	}

	/*
	 * Now we may have reaped some /exec'd processes that were previously
	 * dumb and have now exited.  So we call cleanup_dead_processes() to 
	 * find and delete any such processes.
	 */
	cleanup_dead_processes();

	/*
	 * If 'wanted' is not -1, then we didnt find that process, and
	 * if 'wanted' is -1, then you should ignore the retval anyhow. ;-)
	 */
	return -1;
}


/*
 * clean_up_processes: In effect, we want to tell all of our sub processes
 * that we're going away.  We cant be 100% sure that theyre all dead by
 * the time this function returns, but we can be 100% sure that they will
 * be killed off next time they come up to run.  This is the only thing that
 * can be guaranteed, and is in fact all we really need to know.
 */
void 		clean_up_processes (void)
{
	if (process_list_size)
	{
		say("Closing all left over exec's");
		kill_all_processes(SIGTERM);
		sleep(2);
		get_child_exit(-1);
		kill_all_processes(SIGKILL);
	}
}


/*
 * text_to_process: sends the given text to the given process.  If the given
 * process index is not valid, an error is reported and 1 is returned.
 * Otherwise 0 is returned. 
 * Added show, to remove some bad recursion, phone, april 1993
 */
int 		text_to_process (int proc_index, const char *text, int show)
{
	Process	*proc;
	char *	my_buffer;
	size_t	size;
	char	logical_name[1024];
	const char *recoded_text;
	char *	extra = NULL;

	if (valid_process_index(proc_index) == 0)
		return 1;

	proc = process_list[proc_index];

	if (show)
	{
		int	l = message_setall(proc->refnum, NULL, LEVEL_OTHER);
		put_it("%s%s", get_prompt_by_refnum(proc->refnum), text);
		pop_message_from(l);
	}

	size = strlen(text) + 2;
	my_buffer = alloca(size);
	snprintf(my_buffer, size, "%s\n", text);

	index_to_target(proc_index, logical_name, sizeof(logical_name));
	recoded_text = outbound_recode(logical_name, proc->server, my_buffer, &extra);
	if (write(proc->p_stdin, recoded_text, strlen(recoded_text)) <= 0)
	{
		yell("Was unable to write text %s to process %d",
			text, proc_index);
	}
	new_free(&extra);

	set_prompt_by_refnum(proc->refnum, empty_string);
	return (0);
}

/*
 * This adds a new /wait %proc -cmd   entry to a running process.
 */
void 		add_process_wait (int proc_index, const char *cmd)
{
	Process	*proc = process_list[proc_index];
	List	*new_ewl, *posn;

	new_ewl = new_malloc(sizeof(List));
	new_ewl->next = NULL;
	new_ewl->name = malloc_strdup(cmd);

	if ((posn = proc->waitcmds))
	{
		while (posn->next)
			posn = posn->next;
		posn->next = new_ewl;
	}
	else
		proc->waitcmds = new_ewl;

}



/* - - - - - - - - -  - - - - - - - - - - - - - - - - - */

/*
 * A process must go through two stages to be completely obliterated from
 * the client.  Either stage may happen first, but until both are completed
 * we keep the process around.
 *
 *	1) We must recieve an EOF on both stdin and stderr, or we must
 *	   have closed stdin and stderr already (handled by do_processes)
 *	2) The process must have died (handled by get_child_exit)
 *
 * The reason why both must happen is becuase the process can die (and
 * we would get an async signal) before we read all of its output on the
 * pipe, and if we simply deleted the process when it dies, we could lose
 * some of its output.  The reason why we cant delete a process that has
 * asserted EOF on its output is because it could still be running (duh! ;-)
 * So we wait for both to happen.
 */

/*
 * This function is called by the three places that can effect a change
 * on the state of a running process:
 * 	1) get_child_exit, which can mark a process as exited
 *	2) do_processes, which can mark a child as being done with I/O
 *	3) execcmd, which can mark a child as being done with I/O
 *
 * Any processes that are found to have both exited and having completed
 * their I/O will be summarily destroyed.
 */
static void 	cleanup_dead_processes (void)
{
	int	i;
	List	*cmd,
		*next;
	Process *deadproc, *proc;
	char	*exit_info;
	int	old_from_server, l;

	if (!process_list)
		return;		/* Nothing to do */

	old_from_server = from_server;
	for (i = 0; i < process_list_size; i++)
	{
		if (!(proc = process_list[i]))
			continue;

		/*
		 * We do not parse the process if it has not 
		 * both exited and finished its io, UNLESS
		 * it has been disowned.
		 */
		if ((!proc->exited || !proc->dumb) && !proc->disowned)
			continue;		/* Not really dead yet */

		deadproc = process_list[i];
		process_list[i] = NULL;

		/*
		 * First thing to do is fill out the exit information
		 */
		if (deadproc->logical)
		{
			size_t	len = strlen(deadproc->logical) + 25;

			exit_info = alloca(len);
			snprintf(exit_info, len, "%s %d %d", 
					deadproc->logical, deadproc->termsig,
					deadproc->retcode);
		}
		else
		{
			exit_info = alloca(40);
			snprintf(exit_info, 32, "%d %d %d",
				deadproc->index, deadproc->termsig, deadproc->retcode);
		}

		from_server = deadproc->server;
		l = message_from(NULL, LEVEL_OTHER);

		/*
		 * First thing we do is run any /wait %proc -cmd commands
		 */
		next = deadproc->waitcmds;
		deadproc->waitcmds = NULL;
		while ((cmd = next))
		{
			next = cmd->next;
			call_lambda_command("WAITPROC", cmd->name, exit_info);
			new_free(&cmd->name);
			new_free((char **)&cmd);
		}

		/*
		 * Throw /on exec_exit
		 */
		if (do_hook(EXEC_EXIT_LIST, "%s", exit_info))
		{
		    if (get_int_var(NOTIFY_ON_TERMINATION_VAR))
		    {
			if (deadproc->termsig > 0 && deadproc->termsig < NSIG)
			{
				say("Process %d (%s) terminated "
					"with signal %s (%d)", 
				   deadproc->index, deadproc->name, 
				   get_signal_name(deadproc->termsig),
				   deadproc->termsig);

			}
			else if (deadproc->disowned)
			{
				say("Process %d (%s) disowned", 
				   deadproc->index, deadproc->name);
			}
			else
			{
				say("Process %d (%s) terminated "
					"with return code %d", 
				   deadproc->index, deadproc->name, 
				   deadproc->retcode);
			}
		    }
		}
		pop_message_from(l);

		deadproc->p_stdin = new_close(deadproc->p_stdin);
		deadproc->p_stdout = new_close(deadproc->p_stdout);
		deadproc->p_stderr = new_close(deadproc->p_stderr);
		new_free(&deadproc->name);
		new_free(&deadproc->logical);
		new_free(&deadproc->who);
		new_free(&deadproc->redirect);
		new_free(&deadproc->stdoutc);
		new_free(&deadproc->stdoutpc);
		new_free(&deadproc->stderrc);
		new_free(&deadproc->stderrpc);
		new_free((char **)&deadproc);
	}

	/*
	 * Resize away any dead processes at the end
	 */
	for (i = process_list_size - 1; i >= 0; i--)
	{
		if (process_list[i])
			break;
	}

	if (process_list_size != i + 1)
	{
		process_list_size = i + 1;
		RESIZE(process_list, Process, process_list_size);
	}

	from_server = old_from_server;
}



/*
 * ignore_process: When we no longer want to communicate with the process
 * any longer, we call here.  It continues execution until it is done, but
 * we are oblivious to any output it sends.  Now, it will get an EOF 
 * condition on its output fd, so it will probably either take the hint, or
 * its output will go the bit bucket (which we want to happen)
 */
static void 	ignore_process (int idx)
{
	exec_close_in(idx);
	exec_close_out(idx);
}

/*
 * close_in:  When we are finished with the process but still want the
 * rest of its output, we close its input, and hopefully it will get the
 * message and close up shop.
 */
static void 	exec_close_in (int idx)
{
	Process *proc;

	if (valid_process_index(idx) == 0)
		return;

	proc = process_list[idx];
	if (proc->p_stdin != -1)
		proc->p_stdin = new_close(proc->p_stdin);
}

/*
 * close_out:  When we are done sending to a process sometimes we have to 
 * close our stdout before they will do their thing and send us data back
 * to stdin.  
 */
static void 	exec_close_out (int idx)
{
	Process *proc;

	if (valid_process_index(idx) == 0)
		return;

	proc = process_list[idx];
	if (proc->p_stdout != -1)
		proc->p_stdout = new_close(proc->p_stdout);
	if (proc->p_stderr != -1)
		proc->p_stderr = new_close(proc->p_stderr);

	proc->dumb = 1;
}


/*
 * kill_process: sends the given signal to the specified process.  It does
 * not delete the process from the process table or anything like that, it
 * only is for sending a signal to a sub process (most likely in an attempt
 * to kill it.)  The actual reaping of the children will take place async
 * on the next parsing run.
 */
static void 	kill_process (int kill_index, int sig)
{
	pid_t	pgid;
	int	old_from_server, l;

	if (!process_list || kill_index > process_list_size || 
			!process_list[kill_index])
	{
		say("There is no such process %d", kill_index);
		return;
	}

	old_from_server = from_server;
	from_server = process_list[kill_index]->server;
	l = message_from(NULL, LEVEL_OTHER);

	say("Sending signal %s (%d) to process %d: %s", 
		get_signal_name(sig), sig, kill_index, 
		process_list[kill_index]->name);

	pop_message_from(l);
	from_server = old_from_server;

#ifdef HAVE_GETPGID
	pgid = getpgid(process_list[kill_index]->pid);
#else
#  ifndef GETPGRP_VOID
	pgid = getpgrp(process_list[kill_index]->pid);
#  else
	pgid = process_list[kill_index]->pid;
#  endif
#endif

#ifndef HAVE_KILLPG
# define killpg(pg, sig) kill(-(pg), (sig))
#endif

	/* The exec'd process shouldn't be in our process group */
	if (pgid == getpid())
	{
		yell("--- exec'd process is in my job control session!  Something is hosed ---");
		return;
	}

	killpg(pgid, sig);
	kill(process_list[kill_index]->pid, sig);
}


/*
 * This kills (sends a signal, *NOT* ``make it stop running'') all of the
 * currently running subprocesses with the given signal.  Presumably this
 * is because you want them to die.
 *
 * Remember that UNIX signals are asynchronous.  At best, you should assume
 * that they have an advisory effect.  You can tell a process that it should
 * die, but you cannot tell it *when* it will die -- that is up to the system.
 * That means that it is pointless to assume the condition of any of the 
 * kill()ed processes after the kill().  They may indeed be dead, or they may
 * be ``sleeping but runnable'', or they might even be waiting for a hardware
 * condition (such as a swap in).  You do not know when the process will
 * actually die.  It could be 15 ns, it could be 15 minutes, it could be
 * 15 years.  Its also useful to note that we, as the parent process, will not
 * recieve the SIGCHLD signal until after the child dies.  That means it is
 * pointless to try to reap any children processes here.  The main io()
 * loop handles reaping children (by calling get_child_exit()).
 */
static void 	kill_all_processes (int signo)
{
	int	i;
	int	tmp;

	tmp = window_display;
	window_display = 0;
	for (i = 0; i < process_list_size; i++)
	{
		if (process_list[i])
		{
			ignore_process(i);
			kill_process(i, signo);
		}
	}
	window_display = tmp;
}




/* * * * * * logical stuff * * * * * * */
/*
 * valid_process_index: checks to see if index refers to a valid running
 * process and returns true if this is the case.  Returns false otherwise 
 */
static int 	valid_process_index (int process)
{
	if ((process < 0) || (process >= process_list_size) || 
			!process_list[process])
	{
		say("No such process number %d", process);
		return (0);
	}

	return (1);
}

static int 	is_logical_unique (char *logical)
{
	Process	*proc;
	int	i;

	if (!logical)
		return 1;

	for (i = 0; i < process_list_size; i++)
	{
		if (!(proc = process_list[i]) || !proc->logical)
			continue;

		if (!my_stricmp(proc->logical, logical))
			return 0;
	}

	return 1;
}


/*
 * logical_to_index: converts a logical process name to it's approriate index
 * in the process list, or -1 if not found 
 */
static	int 	logical_to_index (const char *logical)
{
	Process	*proc;
	int	i;

	for (i = 0; i < process_list_size; i++)
	{
		if (!(proc = process_list[i]) || !proc->logical)
			continue;

		if (!my_stricmp(proc->logical, logical))
			return i;
	}

	return -1;
}

static	int	index_to_target (int process, char *buf, size_t bufsiz)
{
	if ((process < 0) || (process >= process_list_size) ||
			!process_list[process])
	{
		snprintf(buf, bufsiz, "(Process %d does not exist)", process);
		return -1;
	}

	/* /EXEC targets are always preceded by a percent sign */
	if (process_list[process]->logical)
		snprintf(buf, bufsiz, "%%%s", process_list[process]->logical);
	else 
		snprintf(buf, bufsiz, "%%%d", process);

	return 0;
}

/*
 * get_process_index: parses out a process index or logical name from the
 * given string 
 */
int 		get_process_index (char **args)
{
	char	*s = next_arg(*args, args);
	return is_valid_process(s);
}

/*
 * is_valid_process: tells me if the spec is a process that is either
 * running or still has not closed its pipes, or both.
 */
int		is_valid_process (const char *arg)
{
	if (!arg || *arg != '%')
		return -1;

	arg++;
	if (is_number(arg) && valid_process_index(my_atol(arg)))
		return my_atol(arg);
	else
		return logical_to_index(arg);

	return -1;
}

int		process_is_running (char *arg)
{
	int idx = is_valid_process(arg);

	if (idx == -1)
		return 0;
	else
		return 1;
}

#endif
