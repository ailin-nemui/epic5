/*
 * exec.c: handles exec'd process for IRCII 
 *
 * Copyright (c) 1990 Michael Sandroff.
 * Copyright (c) 1991, 1992 Troy Rollo.
 * Copyright (c) 1992-1996 Matthew Green.
 * Copyright 1997, 2014, 2020 EPIC Software Labs
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
#include "dcc.h"
#include "exec.h"
#include "vars.h"
#include "ircaux.h"
#include "commands.h"
#include "window.h"
#include "screen.h"
#include "hook.h"
#include "input.h"
#include "server.h"
#include "output.h"
#include "parse.h"
#include "newio.h"
#include "ifcmd.h"
#include "functions.h"

#include <sys/wait.h>
#include <sys/ioctl.h>
#ifdef HAVE_SYS_FILIO_H
#include <sys/filio.h>
#endif

typedef struct WaitCmdStru
{
	struct WaitCmdStru *	next;
	char *			commands;
}	WaitCmd;

/* Process: the structure that has all the info needed for each process */
typedef struct
{
	int	refnum;			/* The logical refnum of an /EXECd process */
	char *	refnum_desc;		/* The %<refnum> */
	char *	logical;		/* The logical name supplied by the user  (-NAME) */
	char *	logical_desc;		/* The %<logical> */
	char *	commands;		/* The commands being executed */
	int	direct;			/* Whether to use a shell or not */

	pid_t	pid;			/* process-id of process */
	int	p_stdin;		/* fd through which we send to process's stdin */
	int	p_stdout;		/* fd through which we receive from process's stdout */
	int	p_stderr;		/* fd through which we receive from process's stderr */

	int	exited;			/* 0 if process is running, 1 if it exited or was killed */
	int	termsig;		/* The signal that killed it (if any) */
	int	retcode;		/* It's exit code (if any) */

	char *	redirect;		/* Either PRIVMSG or NOTICE (for send_to_target) */
	char *	who;			/* Who all output is sent to (for send_to_target) */
	unsigned  window_refnum;	/* A window all output shall be /echo'd to */
	int	server_refnum;		/* The server this /exec process is attached to */

	int	dumb;			/* 0 if we're handling output from it, 1 if we stopped listening */
	int	disowned;		/* NOT IMPLEMENTED - 1 if we disowned it for cleanup */

	char *	stdoutc;		/* Commands to run on each line of stdout (-LINE) */
	char *	stdoutpc;		/* Commands to run on each partial line of output (-LINEPART) */
	char *	stderrc;		/* Commands to run on each line of stderr (-ERROR) */
	char *	stderrpc;		/* Commands to run on each partial line of error (-ERRORPART) */
	char *	exitpc;			/* Commands to run on process cleanup (-EXIT) */
	WaitCmd *waitcmds;		/* Commands to run on process exit (-END or /WAIT -CMD) */

	Timeval	started_at;		/* When the process began */
	int	lines_recvd;		/* How many lines we've received from process */
	int	lines_sent;		/* How many lines we've sent */
	int	lines_limit;		/* How many lines till we give up */
}	Process;


static	Process **process_list = NULL;
static	int	process_list_size = 0;
static	int	next_process_refnum = 1;

static 	void 	handle_filedesc 	(Process *, int *, int, int);
static 	void 	cleanup_dead_processes 	(void);
static 	void 	ignore_process 		(Process *);
static 	void 	exec_close_in		(Process *);
static 	void 	exec_close_out		(Process *);
static 	void 	kill_process 		(Process *, int);
static 	void 	kill_all_processes 	(int signo);
static 	int 	is_logical_unique 	(const char *logical);
static	void 	do_exec 		(int fd);
static	Process *	get_process_by_refnum (int refnum);
static int 	get_process_refnum 	(const char *desc);

static	int	set_message_from_for_process (Process *proc)
{
	const char *logical_name; 
	int	l;

	if (proc->logical_desc)
		logical_name = proc->logical_desc;
	else
		logical_name = proc->refnum_desc;

	if (proc->window_refnum)
		l = message_setall(proc->window_refnum, logical_name, LEVEL_OTHER);
	else
		l = message_from(logical_name, LEVEL_OTHER);

	return l;
}


/*************************************************************/
/*
 * summarize_process
 */
static size_t	summarize_process (Process *proc, char *outbuf, size_t outbufsize)
{
	if (proc)
	{
		if (proc->logical)
			return snprintf(outbuf, outbufsize, 
					"%d (%s) (pid %d): %s", 
				        proc->refnum, proc->logical, proc->pid, 
					proc->commands);
		else
			return snprintf(outbuf, outbufsize,
					"%d (pid %d): %s", 
				        proc->refnum, proc->pid, proc->commands);
	}
	else
		return 0;
}

/*
 * output_process
 */
static void	output_process (Process *process)
{
}

/*
 * check_process_list
 * - This defrags the process list, moving all alive processes to 
 *   the lowest index, and if none are alive, deleting the list.
 */
static void	check_process_list (void)
{
}

/*************************************************************/
/*
 * execcmd_list_processes
 */
static void	execcmd_list_processes (void)
{
	int		i;
	Process	*	proc;
	char		outbuf[BIG_BUFFER_SIZE + 1];
	size_t		outbufsiz = sizeof(outbuf);

	if (process_list)
	{
		say("Process List:");
		for (i = 0; i < process_list_size; i++)
		{
			if ((proc = process_list[i]))
			{
				if (summarize_process(proc, outbuf, outbufsiz))
					say("\t%s", outbuf);
			}
		}
	}
	else
		say("No processes are running");
}


/*
 * do_processes: This is called from the main io() loop to handle any
 * pending /exec'd events.  All this does is call handle_filedesc() on
 * the two reading descriptors.  If an EOF is asserted on either, then they
 * are closed.  If EOF has been asserted on both, then  we mark the process
 * as being "dumb".  Once it is reaped (exited), it is expunged.
 */
static void 		do_exec (int fd)
{
	int	i;
	int	global_limit, limit;

	if (!process_list)
		return;

	global_limit = get_int_var(SHELL_LIMIT_VAR);

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

		if (proc->lines_limit)
			limit = proc->lines_limit;
		else
			limit = global_limit;

		if (limit > 0 && proc->lines_recvd >= limit)
		{
			int l = set_message_from_for_process(proc);
			say("Ignoring process %d (reached output limit): %s", proc->refnum, proc->commands);
			ignore_process(proc);
			pop_message_from(l);
		}
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
	const char *logical_name;
	const char	*utf8_text;
	char *extra = NULL;

	/* 1 -> line buffering; used to be 0 -> No buffering */
	switch ((len = dgets(*fd, exec_buffer, IO_BUFFER_SIZE, 1))) 
	{
	    case -1:		/* Something died */
	    {
		*fd = new_close(*fd);
		if (proc->p_stdout == -1 && proc->p_stderr == -1)
			proc->dumb = 1;
		return;				/* PUNT! */
	    }

#if 1
	    /* When buffering is 1 (line buffering), case 0 means "incomplete line" */
	    case 0:
		return;
#else
	    /* When buffering is 0 (no buffering), case 0 means "incomplete line" */
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
		FALLTHROUGH
	    }
#endif

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
	from_server = proc->server_refnum;
	proc->lines_recvd++;

	while (len > 0 && (exec_buffer[len - 1] == '\n' ||
			   exec_buffer[len - 1] == '\r'))
	     exec_buffer[--len] = 0;

	if (proc->logical_desc)
		logical_name = proc->logical_desc;
	else
		logical_name = proc->refnum_desc;
	utf8_text = inbound_recode(logical_name, proc->server_refnum, empty_string, exec_buffer, &extra);

	l = set_message_from_for_process (proc);
	if (proc->redirect && proc->who) 
	     redirect_text(proc->server_refnum, proc->who, utf8_text, proc->redirect, 1);

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
	    if ((do_hook(hook, "%d %s", proc->refnum, utf8_text)))
		if (!proc->redirect)
		    put_it("%s", utf8_text);
	}
	pop_message_from(l);

	new_free(&extra);
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
				if (proc && proc->pid != -1 && proc->pid == pid)
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
 * process reference is not valid, an error is reported and 1 is returned.
 * Otherwise 0 is returned. 
 * Added show, to remove some bad recursion, phone, april 1993
 */
int 		text_to_process (const char *target, const char *text, int show)
{
	int	process_refnum;
	Process	*proc;
	char *	my_buffer;
	size_t	size;
	const char *logical_name;
	const char *recoded_text;
	char *	extra = NULL;
	int	l;

	if (!(process_refnum = get_process_refnum(target)))
	{
		say("Cannot send text to invalid process %s", target);
		return -1;
	}

	if (!(proc = get_process_by_refnum(process_refnum)))
	{
		say("Cannot send text to invalid process %s", target);
		return -1;
	}

	if (proc->pid == -1)
	{
		say("Cannot send text to unlaunched process %s", target);
		return -1;
	}

	if (proc->p_stdin == -1)
	{
		say("Cannot send text to -CLOSEd process %s", target);
		return -1;
	}

	if (proc->logical_desc)
		logical_name = proc->logical_desc;
	else
		logical_name = proc->refnum_desc;

	/* Say nothing, do nothing */
	if (!text)
		return -1;

	size = strlen(text) + 2;
	my_buffer = alloca(size);
	snprintf(my_buffer, size, "%s\n", text);
	recoded_text = outbound_recode(logical_name, proc->server_refnum, my_buffer, &extra);

	l = set_message_from_for_process (proc);
	if (write(proc->p_stdin, recoded_text, strlen(recoded_text)) <= 0)
		yell("Was unable to write text %s to process %s", text, target);
	new_free(&extra);

	if (show)
		if ((do_hook(SEND_EXEC_LIST, "%s %d %s", logical_name, process_refnum, text)))
			put_it("%s", text);

	pop_message_from(l);
	return (0);
}

/*
 * This adds a new /wait %proc -cmd   entry to a running process.
 */
void 		add_process_wait (const char *target, const char *cmd)
{
	Process *proc;
	WaitCmd *new_ewl, *posn;
	int	process_refnum;

	if (!(process_refnum = get_process_refnum(target)))
	{
		say("Cannot add wait command to invalid process %s", target);
		return;
	}

	if (!(proc = get_process_by_refnum(process_refnum)))
	{
		say("Cannot add wait command to invalid process %s", target);
		return;
	}

	new_ewl = new_malloc(sizeof(WaitCmd));
	new_ewl->next = NULL;
	new_ewl->commands = malloc_strdup(cmd);

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
	WaitCmd	*cmd, *next;
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
				deadproc->refnum, deadproc->termsig, deadproc->retcode);
		}

		from_server = deadproc->server_refnum;
		l = set_message_from_for_process(deadproc);

		/*
		 * First thing we do is run any /wait %proc -cmd commands
		 */
		next = deadproc->waitcmds;
		deadproc->waitcmds = NULL;
		while ((cmd = next))
		{
			next = cmd->next;
			call_lambda_command("WAITPROC", cmd->commands, exit_info);
			new_free(&cmd->commands);
			new_free((char **)&cmd);
		}

		if (proc->exitpc && *proc->exitpc)
			call_lambda_command("EXEC", proc->exitpc, exit_info);
		else
		{
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
					   deadproc->refnum, deadproc->commands,
					   get_signal_name(deadproc->termsig),
					   deadproc->termsig);

				}
				else if (deadproc->disowned)
				{
					say("Process %d (%s) disowned", 
					   deadproc->refnum, deadproc->commands);
				}
				else
				{
					say("Process %d (%s) terminated "
						"with return code %d", 
					   deadproc->refnum, deadproc->commands, 
					   deadproc->retcode);
				}
			    }
			}
		}
		pop_message_from(l);

		deadproc->p_stdin = new_close(deadproc->p_stdin);
		deadproc->p_stdout = new_close(deadproc->p_stdout);
		deadproc->p_stderr = new_close(deadproc->p_stderr);
		new_free(&deadproc->commands);
		new_free(&deadproc->logical);
		new_free(&deadproc->who);
		new_free(&deadproc->redirect);
		new_free(&deadproc->stdoutc);
		new_free(&deadproc->stdoutpc);
		new_free(&deadproc->stderrc);
		new_free(&deadproc->stderrpc);
		new_free(&deadproc->exitpc);
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
		RESIZE(process_list, Process *, process_list_size);
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
static void 	ignore_process (Process *proc)
{
	if (!proc)
		return;

	exec_close_in(proc);
	exec_close_out(proc);
}

/*
 * close_in:  When we are finished with the process but still want the
 * rest of its output, we close its input, and hopefully it will get the
 * message and close up shop.
 */
static void 	exec_close_in (Process *proc)
{
	if (!proc)
		return;

	if (proc->p_stdin != -1)
		proc->p_stdin = new_close(proc->p_stdin);
}

/*
 * close_out:  When we are done sending to a process sometimes we have to 
 * close our stdout before they will do their thing and send us data back
 * to stdin.  
 */
static void 	exec_close_out (Process *proc)
{
	if (!proc)
		return;

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
static void 	kill_process (Process *proc, int sig)
{
	pid_t	pgid;
	int	old_from_server, l;

	if (!proc)
		return;

	old_from_server = from_server;
	from_server = proc->server_refnum;
	l = set_message_from_for_process(proc);

	say("Sending signal %s (%d) to process %d: %s", 
		get_signal_name(sig), sig, proc->refnum, proc->commands);

	pop_message_from(l);
	from_server = old_from_server;

	/* Administrative kill of unlaunched process */
	if (proc->pid == -1)
	{
		proc->exited = 1;
		proc->disowned = 1;
		proc->termsig = sig;
		return;
	}

#ifdef HAVE_GETPGID
	pgid = getpgid(proc->pid);
#else
	pgid = proc->pid;	/* Oh well.... */
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
	kill(proc->pid, sig);
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

	tmp = swap_window_display(0);
	for (i = 0; i < process_list_size; i++)
	{
		if (process_list[i])
		{
			ignore_process(process_list[i]);
			kill_process(process_list[i], signo);
		}
	}
	swap_window_display(tmp);
}




/* * * * * * logical stuff * * * * * * */
static int 	is_logical_unique (const char *logical)
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


static	Process *	get_process_by_refnum (int refnum)
{
	int	i;

	for (i = 0; i < process_list_size; i++)
	{
		if (process_list[i] && process_list[i]->refnum == refnum)
			return process_list[i];
	}
	return NULL;
}

/*
 * get_process_refnum: parses out a process index or logical name from the
 * given string 
 */
static int 	get_process_refnum (const char *desc)
{
	int	i;

	if (!desc || *desc != '%')
		return 0;

	for (i = 0; i < process_list_size; i++)
	{
		if (process_list[i])
		{
			if (process_list[i]->refnum_desc && !my_stricmp(process_list[i]->refnum_desc, desc))
				return process_list[i]->refnum;
			else if (process_list[i]->logical_desc && !my_stricmp(process_list[i]->logical_desc, desc))
				return process_list[i]->refnum;
		}
	}

	return 0;
}

static  Process *       get_process_by_description (const char *desc)
{
       	int	i;

	if (!desc || *desc != '%')
		return NULL;

	for (i = 0; i < process_list_size; i++)
	{
		if (process_list[i])
		{
			if (process_list[i]->refnum_desc && !my_stricmp(process_list[i]->refnum_desc, desc))
				return process_list[i];
			else if (process_list[i]->logical_desc && !my_stricmp(process_list[i]->logical_desc, desc))
				return process_list[i];
		}
	}
	return NULL;
}


/*
 * is_valid_process: convert a %procdesc into a process index.
 * running or still has not closed its pipes, or both.
 */
int	is_valid_process (const char *desc)
{
	if (get_process_refnum(desc))
		return 1;
	return 0;
}

static Process *	new_process (const char *commands)
{
	Process *proc;
	int	i;

	proc = new_malloc(sizeof(Process));

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

	proc->refnum = next_process_refnum++;
	proc->refnum_desc = NULL;
	malloc_sprintf(&proc->refnum_desc, "%%%d", proc->refnum);
	proc->logical = NULL;
	proc->logical_desc = NULL;
	proc->commands = malloc_strdup(commands);
	proc->direct = 0;

	proc->pid = -1;
	proc->p_stdin = -1;
	proc->p_stdout = -1;
	proc->p_stderr = -1;

	proc->exited = 0;
	proc->termsig = 0;
	proc->retcode = 0;

	proc->redirect = NULL;
	proc->who = NULL;
	proc->window_refnum = 0;
	proc->server_refnum = -1;

	proc->dumb = 0;
	proc->disowned = 0;

	proc->stdoutc = NULL;
	proc->stdoutpc = NULL;
	proc->stderrc = NULL;
	proc->stderrpc = NULL;
	proc->exitpc = NULL;
	proc->waitcmds = NULL;

	get_time(&proc->started_at);
	proc->lines_recvd = 0;
	proc->lines_sent = 0;

	return proc;
}

static int	start_process (Process *proc)
{
	int	p0[2], p1[2], p2[2],
		pid, cnt;
	const char *shell;
	char	*arg;
	char *	commands;

	if (proc->commands == NULL)
		return -1;

	commands = LOCAL_COPY(proc->commands);

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
		return -1;
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
		return -1;

	/*
	 * CHILD: set up and exec the process
	 */
	case 0:
	{
		int	i;

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
		if (proc->direct || !shell)
		{
			int	max;
			char **my_args;

			cnt = 0;
			max = 5;
			my_args = new_malloc(sizeof(char *) * max);
			while ((arg = new_next_arg(commands, &commands)))
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
					proc->commands, strerror(errno));
		}

		/*
		 * If we're using a shell, let them have all the fun
		 */
		else
		{
			const char *flag;

			if (!(flag = get_string_var(SHELL_FLAGS_VAR)))
				flag = empty_string;

			execl(shell, shell, flag, proc->commands, NULL);

			printf("*** Error running program \"%s %s %s\": %s\n", 
					shell, flag, proc->commands, strerror(errno));
		}

		/*
		 * Something really died if we got here
		 */
		_exit(-1);
		break;
	}

	default:
	{
		proc->pid = pid;
		proc->server_refnum = from_server;
		get_time(&proc->started_at);

		close(p0[0]);
		close(p1[1]);
		close(p2[1]);
		proc->p_stdin = p0[1];
		proc->p_stdout = p1[0];
		proc->p_stderr = p2[0];

		proc->lines_recvd = 0;
		proc->exited = 0;
		proc->termsig = 0;
		proc->retcode = 0;
		proc->disowned = 0;
		proc->dumb = 0;

		new_open(proc->p_stdout, do_exec, NEWIO_READ, 1, proc->server_refnum);
		new_open(proc->p_stderr, do_exec, NEWIO_READ, 1, proc->server_refnum);
		break;
	}
	}

	return 0;
}
/********************************/
/*
 * execcmd: The /EXEC command - run sub processes underneath the client.
 *
 * The unified general syntax of /EXEC:
 *	/EXEC [-flag [param]]* commands
 *	/EXEC [-flag [param]]* %proc extra-args
 *	/EXEC [-flag [param]]* (commands) extra-args
 * Double quotes ar enot supported anywhere.
 *
 * Each /EXEC command must have a "Process Context"
 *    1.) A %proc of an already running process
 *    2.) A parenthesis-surrounded command line to run
 *    3.) A command line to run 
 *
 * For backwards compatability, it was never possible to specify
 * both commands and flags that required "extra args" (such as -IN).
 * To allow you to specify both commands and extra-args, you may 
 * surround the commands with parenthesis.
 *
 * You can string together as many -flag's as you want, and they will be
 * evaluated in the order you provide.
 *
 * Here is the canonical list of /EXEC flags:
 *	-CLOSE
 *		Close the process's stdin, stdout, and stderr (ie, EOF)
 * 		Many programs will shut down cleanly when they get an EOF on stdin
 *		No further output from the program will be received
 *	-CLOSEIN
 *		Close the process's stdin (ie, EOF)
 *	-CLOSEOUT
 *		Close the process's stdout and stderr (ie, EOF)
 *		No further output from the program will be received
 *	-IN (requires extra-args)
 *		Send a message to a process.
 *		This is equivalent to /MSG %proc extra-args
 *
 *	-SIGNAL number
 *	-SIGNAL signame
 *	-number
 *	-signame
 *		Send a signal to the process ("kill").
 *		The signal may either be a number of its short name (ie, HUP)
 *
 *	-NAME local-name
 *		Set the process's logical name (ie, for %procname)
 *	-OUT 
 *		Directs all output from the process to the current window's current channel.
 *		If you change the current channel, it will keep sending to the original 
 *		   current channel.
 *	-WINDOW
 *		Directs all output from the process to be displayed in the current window.
 *		Normally, output from the process goes to the OTHER window level.
 *	-WINTARGET windesc
 *		Directs all output from the process to be displayed in the specified window
 *		"windesc" can be either a window refnum or window name.
 *		This is handy if you are using a centralized window for /EXEC output.
 *	-MSG target
 *		Directs all output from the process to be sent to the "target"
 *		Target is anything you can send a message to: a nick or channel on irc,
 *		a DCC Chat, a window, a logfile, or even another exec process.
 *		If sent over IRC, it will be sent as a PRIVMSG.
 *	-NOTICE target
 *		Directs all output from the process to be sent to the "target"
 *		If sent over IRC, it will be sent as a NOTICE.
 *
 *	-LINE {block}
 *		Run {block} for each complete line of stdout from the process as $*
 *		The curly braces are required
 *	-LINEPART {block}
 *		Run {block} for each incomplete line of stdout from the process as $*
 *		"Incomplete line" means it didn't end with a newline
 *		The curly braces are required
 *	-ERROR {block}
 *		Run {block} for each complete line of stderr from the process as $*
 *		The curly braces are required
 *	-ERRORPART {block}
 *		Run {block} for each incomplete line of stdout from the process as $*
 *		"Incomplete line" means it didn't end with a newline
 *		The curly braces are required
 *	-END {block}
 *		Run {block} when the process exits.
 *		This does the same thing as /WAIT %proc -CMD {code}
 *		The curly braces are required
 *	-EXIT {block}
 *		Run {block} after the process exits
 *		This runs INSTEAD of /on exec_exit or /set notify_on_termination
 *		The curly braces are required
 *
 *	-DIRECT
 *		Run the command directly with execvp(), do not use a shell
 *		-> double quoted words are honored and the double quotes are removed
 *		-> everything else is treated literally
 *	-LIMIT
 *		Limit the number of lines to receive from the process before doing an implicit -CLOSE
 *		This overrules /SET SHELL_LIMIT for this process only.
 *		The value 0 means "unlimited"
 *
 * Ordinarily (unless you use -DIRECT) your commands are run by a shell, which allows you to use
 * filename wildcard matching, shell variable expansion, etc.  The /SET SHELL (default: "/bin/sh")
 * value is the shell that will be used, and /SET SHELL_FLAGS (default: "-c") is the shell's flag
 * to tell it that the rest of the arguments are commands to run.
 *
 * It gets run something like this:
 *		/bin/sh -c you_commands_here
 */

static void	execcmd_push_arg (const char **arg_list, size_t arg_list_size, int arg_list_idx, const char *flag)
{
	if (arg_list_idx >= 0 && (unsigned long)arg_list_idx < arg_list_size - 1)
	{
		/* yell("Adding [%d] [%s] argument to list", arg_list_idx, flag); */
		arg_list[arg_list_idx] = flag;
	}
	else
		/* yell("Not adding [%s] argument to list - overflow", flag) */ (void) 0;
}

static const char **	execcmd_tokenize_arguments (const char *args, Process **process, char **free_ptr, char **extra_args, int *numargs, int *flags_size)
{
	const char **	arg_list;
	int	arg_list_size;
	int	arg_list_idx;
	char *	args_copy;
	char *	flag;
	size_t	len;
	Process	*proc = NULL;

	args_copy = malloc_strdup(args);
	*free_ptr = args_copy;

	/* XXX hardcoding 64 here is bogus */
	arg_list_size = 64;
	arg_list = (const char **)new_malloc(sizeof(const char *) * arg_list_size);
	arg_list_idx = 0;

	while ((args_copy && *args_copy == '-')  && (flag = next_arg(args_copy, &args_copy)))
	{
		len = strlen(flag);

		if (my_strnicmp(flag, "-CLOSE", len) == 0)
			execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag);
		else if (my_strnicmp(flag, "-CLOSEIN", len) == 0)
			execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag);
		else if (my_strnicmp(flag, "-CLOSEOUT", len) == 0)
			execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag);
		else if (my_strnicmp(flag, "-IN", len) == 0)
			execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag);
		else if (my_strnicmp(flag, "-NAME", len) == 0)
		{
			char *arg = next_arg(args_copy, &args_copy);
			execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag);
			execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, arg);
		}
		else if (my_strnicmp(flag, "-OUT", len) == 0)
			execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag);
		else if (my_strnicmp(flag, "-WINDOW", len) == 0)
			execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag);
		else if (my_strnicmp(flag, "-WINTARGET", len) == 0)
		{
			char *arg = next_arg(args_copy, &args_copy);
			if (arg && *arg)
			{
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag);
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, arg);
			}
			else
				say("EXEC: Error: -WINTARGET requires an argument");
		}
		else if (my_strnicmp(flag, "-MSG", len) == 0)
		{
			char *arg = next_arg(args_copy, &args_copy);
			if (arg && *arg)
			{
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag);
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, arg);
			}
			else
				say("EXEC: Error: -MSG requires a nick or channel argument");
		}
		else if (my_strnicmp(flag, "-NOTICE", len) == 0)
		{
			char *arg = next_arg(args_copy, &args_copy);
			if (arg && *arg)
			{
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag);
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, arg);
			}
			else
				say("EXEC: Error: -MSG requires a nick or channel argument");
		}
		else if (my_strnicmp(flag, "-LINE", len) == 0)
		{
			char *arg = next_expr(&args_copy, '{');
			if (arg && *arg)
			{
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag);
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, arg);
			}
			else
				say("EXEC: Error: Need {...} argument for -LINE flag");
		}
		else if (my_strnicmp(flag, "-LINEPART", len) == 0)
		{
			char *arg = next_expr(&args_copy, '{');
			if (arg && *arg)
			{
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag);
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, arg);
			}
			else
				say("EXEC: Error: Need {...} argument for -ERRORPART flag");
		}
		else if (my_strnicmp(flag, "-ERROR", len) == 0)
		{
			char *arg = next_expr(&args_copy, '{');
			if (arg && *arg)
			{
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag);
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, arg);
			}
			else
				say("EXEC: Error: Need {...} argument for -ERROR flag");
		}
		else if (my_strnicmp(flag, "-ERRORPART", len) == 0)
		{
			char *arg = next_expr(&args_copy, '{');
			if (arg && *arg)
			{
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag);
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, arg);
			}
			else
				say("EXEC: Error: Need {...} argument for -ERRORPART flag");
		}
		else if (my_strnicmp(flag, "-END", len) == 0)
		{
			char *arg = next_expr(&args_copy, '{');
			if (arg && *arg)
			{
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag);
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, arg);
			}
		}
		else if (my_strnicmp(flag, "-EXIT", len) == 0)
		{
			char *arg = next_expr(&args_copy, '{');
			if (arg && *arg)
			{
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag);
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, arg);
			}
		}
		else if (my_strnicmp(flag, "-DIRECT", len) == 0)
			execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag);
		else if (my_strnicmp(flag, "-LIMIT", len) == 0)
		{
			char *arg = next_arg(args_copy, &args_copy);
			if (arg && *arg)
			{
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag);
				execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, arg);
			}
		}
		else if (my_strnicmp(flag, "-NOLAUNCH", len) == 0)
			execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag);

		else if (my_atol(flag + 1) > 0)
		{
			execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, "-SIGNAL");
			execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag + 1);
		}
		else if (get_signal_by_name(flag + 1) > 0)
		{
			execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, "-SIGNAL");
			execcmd_push_arg(arg_list, arg_list_size, arg_list_idx++, flag + 1);
		}
		else
			continue;		/* Do we handle errors, or not? */
	}

	execcmd_push_arg(arg_list, arg_list_size, arg_list_idx, NULL);

	if (args_copy && *args_copy)
	{
		if (*args_copy == '%')
		{
			const char *proc_desc;
			int	refnum;

			/* yell("Looking up existing process %s", args_copy); */
			proc_desc = next_arg(args_copy, &args_copy);
			refnum = get_process_refnum(proc_desc);

			if (!(proc = get_process_by_refnum(refnum)))
			{
				say("EXEC: Error: The process %s is not a valid process", proc_desc);
				return NULL;
			}
		}
		else if (*args_copy == '(')
		{
			const char *cmds = next_expr(&args_copy, '(');
			/* yell("STarting up new process from parethensis"); */
			proc = new_process(cmds);
		}
		else
		{
			/* yell("Starting up new naked process from [%s]", args_copy); */
			proc = new_process(args_copy);
			args_copy = NULL;
		}
	}

	if (proc == NULL)
	{
		say("EXEC: Error: After all flags, there should be either a %%proc or commands to run");
		return NULL;
	}

	*process = proc;
	*extra_args = args_copy;
	*numargs = arg_list_idx;
	*flags_size = arg_list_size;
	return arg_list;
}

BUILT_IN_COMMAND(execcmd)
{
	char *	free_ptr;
	Process *process;
	const char **	flags;
	int	flags_size;
	char *	extra_args;
	int	numargs;
	int	i, l;
	int	launch = 1;

	if (!args || !*args)
	{
		execcmd_list_processes();
		return;
	}

	if (!(flags = execcmd_tokenize_arguments(args, &process, &free_ptr, &extra_args, &numargs, &flags_size)))
		return;

	l = set_message_from_for_process(process);

	for (i = 0; i < numargs; i++)
	{
		const char *flag;
		size_t	len;

		flag = flags[i];
		len = strlen(flag);

		if (my_strnicmp(flag, "-CLOSE", len) == 0)
			ignore_process(process);
		else if (my_strnicmp(flag, "-CLOSEIN", len) == 0)
			exec_close_in(process);
		else if (my_strnicmp(flag, "-CLOSEOUT", len) == 0)
			exec_close_out(process);
		else if (my_strnicmp(flag, "-IN", len) == 0)
			text_to_process(process->logical_desc, extra_args, 1);

		else if (my_strnicmp(flag, "-NAME", len) == 0)
		{
			const char *new_name = flags[++i];

			if (is_logical_unique(new_name))
			{
				malloc_strcpy(&process->logical, new_name);
				malloc_sprintf(&process->logical_desc, "%%%s", new_name);
				say("Process %d (%s) is now called %s",
					process->refnum, process->commands, process->logical);
			} 
			else 
				say("The name %s is not unique!", new_name);
		}
		else if (my_strnicmp(flag, "-OUT", len) == 0)
		{
			const char *who;

			if (!(who = get_window_target(0)))
				say("No query or channel in this window for -OUT");
			else
			{
				malloc_strcpy(&process->who, who);
				malloc_strcpy(&process->redirect, "PRIVMSG");
			}

		}
		else if (my_strnicmp(flag, "-WINDOW", len) == 0)
		{
			pop_message_from(l);
			l = set_message_from_for_process(process);
			process->window_refnum = get_window_refnum(0);
			say("Output from process %d (%s) now going to window %d", 
					process->refnum, process->commands, process->window_refnum);
		}
		else if (my_strnicmp(flag, "-WINTARGET", len) == 0)
		{
			const char *desc = flags[++i];
			int	w;

			if (!desc || (w = lookup_window(desc)) < 1)
				say("Target window not found");
			else
			{
				process->window_refnum = w;
				pop_message_from(l);
				l = set_message_from_for_process(process);
				say("Output from process %d (%s) now going to window %d", 
					process->refnum, process->commands, process->window_refnum);
			}
		}
		else if (my_strnicmp(flag, "-MSG", len) == 0)
		{
			const char *arg = flags[++i];

			if (arg && *arg)
			{
				malloc_strcpy(&process->redirect, "PRIVMSG");
				malloc_strcpy(&process->who, arg);
			}
			else
				say("EXEC: Error: -MSG requires a nick or channel argument");
		}
		else if (my_strnicmp(flag, "-NOTICE", len) == 0)
		{
			const char *arg = flags[++i];

			if (arg && *arg)
			{
				malloc_strcpy(&process->redirect, "NOTICE");
				malloc_strcpy(&process->who, arg);
			}
			else
				say("EXEC: Error: -NOTICE requires a nick or channel argument");
		}

		else if (my_strnicmp(flag, "-LINE", len) == 0)
		{
			const char *arg = flags[++i];

			if (arg && *arg)
				malloc_strcpy(&process->stdoutc, arg);
			else
				say("EXEC: Error: Need {...} argument for -LINE flag");
		}
		else if (my_strnicmp(flag, "-LINEPART", len) == 0)
		{
			const char *arg = flags[++i];

			if (arg && *arg)
				malloc_strcpy(&process->stdoutpc, arg);
			else
				say("EXEC: Error: Need {...} argument for -LINEPART flag");
		}
		else if (my_strnicmp(flag, "-ERROR", len) == 0)
		{
			const char *arg = flags[++i];

			if (arg && *arg)
				malloc_strcpy(&process->stderrc, arg);
			else
				say("EXEC: Error: Need {...} argument for -ERROR flag");
		}
		else if (my_strnicmp(flag, "-ERRORPART", len) == 0)
		{
			const char *arg = flags[++i];

			if (arg && *arg)
				malloc_strcpy(&process->stderrpc, arg);
			else
				say("EXEC: Error: Need {...} argument for -ERRORPART flag");
		}
		else if (my_strnicmp(flag, "-END", len) == 0)
		{
			const char *arg = flags[++i];

			if (arg && *arg)
				add_process_wait(process->refnum_desc, arg);
			else
				say("EXEC: Error: Need {...} argument for -END flag");
		}
		else if (my_strnicmp(flag, "-EXIT", len) == 0)
		{
			const char *arg = flags[++i];

			if (arg && *arg)
				malloc_strcpy(&process->exitpc, arg);
			else
				say("EXEC: Error: Need {...} argument for -EXIT flag");
		}
		else if (my_strnicmp(flag, "-DIRECT", len) == 0)
			process->direct = 1;
		else if (my_strnicmp(flag, "-LIMIT", len) == 0)
		{
			const char *arg = flags[++i];
			if (is_number(arg))
				process->lines_limit = my_atol(arg);
		}
		else if (my_strnicmp(flag, "-NOLAUNCH", len) == 0)
			launch = 0;

		else if (my_strnicmp(flag, "-SIGNAL", len) == 0)
		{
			const char *arg = flags[++i];
			int	signum;

			if (is_number(arg))
				signum = my_atol(arg);
			else
				signum = get_signal_by_name(arg);

			if (signum > 0 && signum < NSIG)
				kill_process(process, signum);
			else
				say("No such signal: %s", arg);
		}

		else
			continue;
	}

	if (process->pid == -1 && launch)
		start_process(process);

	pop_message_from(l);

	new_free((char **)&flags);
	new_free((char **)&free_ptr);
}

/*
 * Here's the plan:
 *
 * $execctl(REFNUM <description>)
 * $execctl(REFNUMS)
 * $execctl(NEW <commands>)
 * $execctl(LAUNCH <refnum>)
 * $execctl(CLOSEIN <refnum>)
 * $execctl(CLOSEOUT <refnum>)
 * $execctl(SIGNAL <signal> %<refnum>)
 *
 * $execctl(GET <refnum> [FIELD])
 * $execclt(SET <refnum> [FIELD] [VALUE])
 *
 * [FIELD] is one of the following:
 *	REFNUM		The process's unique integer refnum
 *	REFNUM_DESC	The user-exposed version of REFNUM (ie, prefixed with %)
 *	LOGICAL		The process's logical name (provided by user)
 *	LOGICAL_DESC	LOGICAL, prefixed with a %
 *	COMMANDS	The commands htat will be run. -- (* cannot be SET after launch)
 *	DIRECT		Whether to run <commands> in a shell -- (*)
 *	REDIRECT	Either PRIVMSG or NOTICE
 *	WHO		Who to send all output from process to
 *	WINDOW_REFNUM	Which window to send all exec output to
 *	SERVER_REFNUM	Which server to use for redirects
 *	STARTED_AT	The time when the process was created or launched
 *	STDOUTC		Code to execute when a complete line of stdout happens
 *	STDOUTPC	Code to execute when an incomplete line of stdout happens
 *	STDERRC		Code to execute when a complete line of stderr happens
 *	STDERRPC	Code to execute when an incomplete line of stderr happens
 *	EXITPC		Code to execute when program is cleaned up 
 *
 * All of the following [LIST] values may be GET but not SET.
 * I make absolutely no guarantees these things won't change. use at your own risk.
 *
 *	PID		The process's PID, if it has been launched -- (** Cannot be SET)
 *	P_STDIN		The fd attached to the process's stdin (**)
 *	P_STDOUT	The fd attached to the process's stdout (**)
 *	P_STDERR	The fd attached to the process's stdout (**)
 *	EXITED		1 if the process has exited, 0 if not yet (**)
 *	TERMSIG		If the process died of a signal, what signal killed it
 *	RETCODE		If the process exited cleanly, what its exit code was
 *	DUMB		0 if we're handling output from process, 1 if we're ignoring it
 *	LINES_RECVD
 *	LINES_SENT
 *	LINES_LIMIT
 *	DISOWNED	0 - not implemented
 *	WAITCMDS	empty-string - not implemented
 */
char *  execctl (char *input)
{
        char *  listc;
        int     len;
	int	refnum;
	char *	field;
	Process *proc;
	char *	retval = NULL;

        GET_FUNC_ARG(listc, input);
        len = strlen(listc);
        if (!my_strnicmp(listc, "REFNUM", len)) {
		RETURN_INT(get_process_refnum(input));
        } else if (!my_strnicmp(listc, "REFNUMS", len)) {
		int	i;

		for (i = 0; i < process_list_size; i++)
			if ((proc = process_list[i]))
				malloc_strcat_word(&retval, space, proc->refnum_desc, DWORD_NO);
		RETURN_MSTR(retval);
        } else if (!my_strnicmp(listc, "NEW", len)) {
		proc = new_process(input);
		RETURN_INT(proc->refnum);
        } else if (!my_strnicmp(listc, "LAUNCH", len)) {
		GET_INT_ARG(refnum, input);
		if (!(proc = get_process_by_refnum(refnum)))
			RETURN_EMPTY;

		if (proc->pid == -1)
			start_process(proc);
        } else if (!my_strnicmp(listc, "CLOSEIN", len)) {
		GET_INT_ARG(refnum, input);
		if (!(proc = get_process_by_refnum(refnum)))
			RETURN_EMPTY;

		exec_close_in(proc);
        } else if (!my_strnicmp(listc, "CLOSEOUT", len)) {
		GET_INT_ARG(refnum, input);
		if (!(proc = get_process_by_refnum(refnum)))
			RETURN_EMPTY;

		exec_close_out(proc);
        } else if (!my_strnicmp(listc, "SIGNAL", len)) {
		int	signum;

		GET_INT_ARG(refnum, input);
		if (!(proc = get_process_by_refnum(refnum)))
			RETURN_EMPTY;

		if (is_number(input))
			signum = my_atol(input);
		else
			signum = get_signal_by_name(input);

		if (signum > 0 && signum < NSIG)
		{
			kill_process(proc, signum);
			RETURN_INT(1);
		}
		RETURN_INT(0);
        } else if (!my_strnicmp(listc, "GET", len)) {
		GET_INT_ARG(refnum, input);
		GET_FUNC_ARG(field, input);

		if (!(proc = get_process_by_refnum(refnum)))
			RETURN_EMPTY;

		if (!my_stricmp(field, "REFNUM")) {
			RETURN_INT(proc->refnum);
		} else if (!my_stricmp(field, "REFNUM_DESC")) {
			RETURN_STR(proc->refnum_desc);
		} else if (!my_stricmp(field, "LOGICAL")) {
			RETURN_STR(proc->logical);
		} else if (!my_stricmp(field, "LOGICAL_DESC")) {
			RETURN_STR(proc->logical_desc);
		} else if (!my_stricmp(field, "COMMANDS")) {
			RETURN_STR(proc->commands);
		} else if (!my_stricmp(field, "DIRECT")) {
			RETURN_INT(proc->direct);
		} else if (!my_stricmp(field, "REDIRECT")) {
			RETURN_STR(proc->redirect);
		} else if (!my_stricmp(field, "WHO")) {
			RETURN_STR(proc->who);
		} else if (!my_stricmp(field, "WINDOW_REFNUM")) {
			RETURN_INT(proc->window_refnum);
		} else if (!my_stricmp(field, "SERVER_REFNUM")) {
			RETURN_INT(proc->server_refnum);
		} else if (!my_stricmp(field, "LINES_LIMIT")) {
			RETURN_INT(proc->lines_limit);
		} else if (!my_stricmp(field, "STDOUTC")) {
			RETURN_STR(proc->stdoutc);
		} else if (!my_stricmp(field, "STDOUTPC")) {
			RETURN_STR(proc->stdoutpc);
		} else if (!my_stricmp(field, "STDERRC")) {
			RETURN_STR(proc->stderrc);
		} else if (!my_stricmp(field, "STDERRPC")) {
			RETURN_STR(proc->stderrpc);
		} else if (!my_stricmp(field, "EXITPC")) {
			RETURN_STR(proc->exitpc);
		} else if (!my_stricmp(field, "PID")) {
			RETURN_INT(proc->pid);
		} else if (!my_stricmp(field, "STARTED_AT")) {
                        return malloc_sprintf(&retval, "%ld %ld",
                                (long) proc->started_at.tv_sec,
                                (long) proc->started_at.tv_usec);
		} else if (!my_stricmp(field, "P_STDIN")) {
			RETURN_INT(proc->p_stdin);
		} else if (!my_stricmp(field, "P_STDOUT")) {
			RETURN_INT(proc->p_stdout);
		} else if (!my_stricmp(field, "P_STDERR")) {
			RETURN_INT(proc->p_stderr);
		} else if (!my_stricmp(field, "LINES_RECVD")) {
			RETURN_INT(proc->lines_recvd);
		} else if (!my_stricmp(field, "LINES_SENT")) {
			RETURN_INT(proc->lines_sent);
		} else if (!my_stricmp(field, "EXITED")) {
			RETURN_INT(proc->exited);
		} else if (!my_stricmp(field, "TERMSIG")) {
			RETURN_INT(proc->termsig);
		} else if (!my_stricmp(field, "RETCODE")) {
			RETURN_INT(proc->retcode);
		} else if (!my_stricmp(field, "DUMB")) {
			RETURN_INT(proc->dumb);
		} else if (!my_stricmp(field, "DISOWNED")) {
			RETURN_INT(proc->disowned);
		}

        } else if (!my_strnicmp(listc, "SET", len)) {
		GET_INT_ARG(refnum, input);
		GET_FUNC_ARG(field, input);

		if (!(proc = get_process_by_refnum(refnum)))
			RETURN_EMPTY;

		if (!my_stricmp(field, "LOGICAL")) {
			char *new_name;
			GET_FUNC_ARG(new_name, input);

			if (is_logical_unique(new_name))
			{
				malloc_strcpy(&proc->logical, new_name);
				malloc_sprintf(&proc->logical_desc, "%%%s", new_name);
				RETURN_INT(1);
			} 
			else
				RETURN_INT(0);
		} else if (!my_stricmp(field, "COMMANDS")) {
			if (proc->pid != -1)
				RETURN_INT(0);
			malloc_strcpy(&proc->commands, input);
			RETURN_INT(1);
		} else if (!my_stricmp(field, "DIRECT")) {
			int	newval;

			GET_INT_ARG(newval, input);
			if (proc->pid != -1)
				RETURN_INT(0);
			if (newval == 0 || newval == 1)
			{
				proc->direct = newval;
				RETURN_INT(1);
			}
			RETURN_INT(0);
		} else if (!my_stricmp(field, "REDIRECT")) {
			char *	newval;

			GET_FUNC_ARG(newval, input);
			if (!strcmp(newval, "PRIVMSG") || !strcmp(newval, "NOTICE"))
			{
				malloc_strcpy(&proc->redirect, newval);
				RETURN_INT(1);
			}
			RETURN_INT(0);
		} else if (!my_stricmp(field, "WHO")) {
			char *	newval;

			GET_FUNC_ARG(newval, input);
			malloc_strcpy(&proc->who, newval);
			RETURN_INT(1);
		} else if (!my_stricmp(field, "WINDOW_REFNUM")) {
			int	newval;

			GET_INT_ARG(newval, input);
			if (newval > 0)
			{
				proc->window_refnum = newval;
				RETURN_INT(1);
			}
			RETURN_INT(0);
		} else if (!my_stricmp(field, "SERVER_REFNUM")) {
			int	newval;

			GET_INT_ARG(newval, input);
			if (newval >= 0)
			{
				proc->server_refnum = newval;
				RETURN_INT(1);
			}
			RETURN_INT(0);
		} else if (!my_stricmp(field, "STDOUTC")) {
			malloc_strcpy(&proc->stdoutc, input);
			RETURN_INT(1);
		} else if (!my_stricmp(field, "STDOUTPC")) {
			malloc_strcpy(&proc->stdoutpc, input);
			RETURN_INT(1);
		} else if (!my_stricmp(field, "STDERRC")) {
			malloc_strcpy(&proc->stderrc, input);
			RETURN_INT(1);
		} else if (!my_stricmp(field, "STDERRPC")) {
			malloc_strcpy(&proc->stderrpc, input);
			RETURN_INT(1);
		} else if (!my_stricmp(field, "EXITPC")) {
			malloc_strcpy(&proc->exitpc, input);
			RETURN_INT(1);
		} else if (!my_stricmp(field, "LINES_LIMIT")) {
			int	newval;

			GET_INT_ARG(newval, input);
			if (newval >= 0)
			{
				proc->lines_limit = newval;
				RETURN_INT(1);
			}
			RETURN_INT(0);
		}
	}

	RETURN_EMPTY;
}

