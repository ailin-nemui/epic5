/*
 *  queue.c - The queue command
 *
 *  Queues allow for future batch processing
 *
 *  Syntax:  /QUEUE -DO -SHOW -LIST -NO_FLUSH -DELETE -FLUSH <name> {commands}
 *
 *  Written by Jeremy Nelson
 *  Copyright 1993 Jeremy Nelson
 *  See the COPYRIGHT file for more information
 */

#include "irc.h"
#include "alias.h"
#include "commands.h"
#include "if.h"
#include "ircaux.h"
#include "queue.h"
#include "output.h"

struct CmdListT;
typedef struct  QueueT {
        struct QueueT   *next;
        struct CmdListT *first;
        char     *name;
} Queue;

typedef struct  CmdListT {
        struct CmdListT *next;
        char     *what;
} CmdList;

static	Queue   *lookup_queue 			(Queue *, const char *);
static	int     add_commands_to_queue 		(Queue *, const char *, const char *);
static	void    display_all_queues 		(Queue *);
static	CmdList *walk_commands			(Queue *);
static	Queue   *make_new_queue 		(Queue *, const char *);
static	int     delete_commands_from_queue 	(Queue *, int);
static	void    flush_queue 			(Queue *);
static	void    print_queue 			(Queue *);
static	int     num_entries 			(Queue *);
static	Queue   *remove_a_queue 		(Queue *);
static	Queue   *do_queue 			(Queue *, int);

BUILT_IN_COMMAND(queuecmd)
{
        Queue   *tmp;
	char	*arg 		= (char *) 0,
		*name 		= (char *) 0,
        	*startcmds 	= (char *) 0,
                *cmds           = (char *) 0;
	int	noflush 	= 0, 		runit 			= 0, 
		list 		= 0,
		flush 		= 0, 		remove_by_number 	= 0,
		commands 	= 1,            number                  = 0;
        static  Queue   *Queuelist;
	
	/* 
	 * If the queue list is empty, make an entry
	 */
	if (!Queuelist)
		Queuelist = make_new_queue(NULL, "Top");

        if (!(startcmds = strchr(args, '{')))
                commands = 0;
        else
                startcmds[-1] = 0;

	while ((arg = upper(next_arg(args, &args))))
        {
		if (*arg == '-' || *arg == '/')
		{
			*arg++ = '\0';
			if (!strcmp(arg, "NO_FLUSH"))
				noflush = 1;
			else if (!strcmp(arg, "SHOW")) 
                        {
				display_all_queues(Queuelist);
                                return;
                        }
			else if (!strcmp(arg, "LIST"))
				list = 1;
			else if (!strcmp(arg, "DO"))
				runit = 1;
			else if (!strcmp(arg, "DELETE"))
				remove_by_number = 1;
			else if (!strcmp(arg, "FLUSH"))
				flush = 1;
		}
		else
		{
			if (name)
				number = atoi(arg);
			else
				name = arg;
		}
	}

	if (!name)
                return;

	/* 
	 * Find the queue based upon the previous queue 
	 */
        tmp = lookup_queue(Queuelist, name);

	/* 
	 * if the next queue is empty, then we need to see if 
	 * we should make it or output an error 
	 */
	if (!tmp->next)
        {
                if (commands)
                	tmp->next = make_new_queue(NULL, name);
		else 
                {
                	say("QUEUE: (%s) no such queue",name);
                	return;
		}
        }

	if (remove_by_number == 1) 
		if (delete_commands_from_queue(tmp->next,number))
                        tmp->next = remove_a_queue(tmp->next);

	if (list == 1) 
		print_queue(tmp->next);
	if (runit == 1) 
		tmp->next = do_queue(tmp->next, noflush);
	if (flush == 1) 
		tmp->next = remove_a_queue(tmp->next);

	if (startcmds) 
        {
		int booya;

		if (!(cmds = next_expr(&startcmds, '{')))
                {
			say("QUEUE: missing closing brace");
			return;
		}
		booya = add_commands_to_queue (tmp->next, cmds, subargs);
		say("QUEUED: %s now has %d entries",name, booya);
	}
}

/*
 * returns the queue BEFORE the queue we are looking for
 * returns the last queue if no match
 */
static Queue *	lookup_queue (Queue *queue, const char *what)
{
	Queue	*tmp = queue;

	while (tmp->next)
	{
		if (!my_stricmp(tmp->next->name, what))
			return tmp;
		else
                        if (tmp->next)
			        tmp = tmp->next;
                        else
                                break;
	}
        return tmp;
}

/* returns the last CmdList in a queue, useful for appending commands */
static CmdList *walk_commands (Queue *queue)
{
	CmdList *ctmp = queue->first;
	
        if (ctmp) 
        {
	        while (ctmp->next)
	                ctmp = ctmp->next;
	        return ctmp;
        }
        return (CmdList *) 0;
}

/*----------------------------------------------------------------*/
/* Make a new queue, link it in, and return it. */
static Queue *make_new_queue (Queue *afterqueue, const char *arg_name)
{
        Queue *tmp = (Queue *)new_malloc(sizeof(Queue));

        tmp->next = afterqueue;
        tmp->first = (CmdList *) 0;
	tmp->name = m_strdup(arg_name);
        return tmp;
}
        
/* add a command to a queue, at the end of the list */
/* expands the whole thing once and stores it */
static int	add_commands_to_queue (Queue *queue, const char *what, const char *subargs)
{
	CmdList *	ctmp = walk_commands(queue);
	char *		list = NULL;
	const char *	sa;
	int 		args_flag = 0;
	
        sa = subargs ? subargs : " ";
	list = expand_alias(what, sa, &args_flag, (char **) 0);
        if (!ctmp) 
        {
                queue->first = (CmdList *)new_malloc(sizeof(CmdList));
		ctmp = queue->first;
	}
        else 
        {
	        ctmp->next = (CmdList *)new_malloc(sizeof(CmdList));
		ctmp = ctmp->next;
	}
	ctmp->what = m_strdup(list);
	ctmp->next = (CmdList *) 0;
	return num_entries(queue);
}


/* remove the Xth command from the queue */
static int	delete_commands_from_queue (Queue *queue, int which)
{
        CmdList *ctmp = queue->first;
        CmdList *blah;
        int x;

        if (which == 1)
                queue->first = ctmp->next;
        else 
        {
                for (x=1;x<which-1;x++) 
                {
                        if (ctmp->next) 
                                ctmp = ctmp->next;
                        else 
                                return 0;
                }
                blah = ctmp->next;
                ctmp->next = ctmp->next->next;
                ctmp = blah;
        }
        new_free(&ctmp->what);
        new_free((char **)&ctmp);
        if (queue->first == (CmdList *) 0)
                return 1;
        else
                return 0;
}

/*-------------------------------------------------------------------*/
/* flush a queue, deallocate the memory, and return the next in line */
static Queue	*remove_a_queue (Queue *queue)
{
	Queue *tmp;
        tmp = queue->next;
	flush_queue(queue);
	new_free((char **)&queue);
	return tmp;
}

/* walk through a queue, deallocating the entries */
static void	flush_queue (Queue *queue)
{
	CmdList *tmp, *tmp2;
	tmp = queue->first;

	while (tmp != (CmdList *) 0)
	{
		tmp2 = tmp;
		tmp = tmp2->next;
                if (tmp2->what != (char *) 0)
		        new_free(&tmp2->what);
                if (tmp2)
	        	new_free((char **)&tmp2);
	}
}

/*------------------------------------------------------------------------*/
/* run the queue, and if noflush, then return the queue, else return the
   next queue */
static Queue  	*do_queue (Queue *queue, int noflush)
{
	CmdList	*tmp;
	
        tmp = queue->first;
        
        do
	{
		if (tmp->what != (char *) 0)
			parse_line("QUEUE", tmp->what, empty_string, 0, 0);
		tmp = tmp->next;
	}
        while (tmp != (CmdList *) 0);

	if (!noflush) 
		return remove_a_queue(queue);
	else
                return queue;
}

/* ---------------------------------------------------------------------- */
/* output the contents of all the queues to the screen */
static void    display_all_queues (Queue *queue)
{
        Queue *tmp = queue->next;
        while (tmp) 
        {
                print_queue(tmp);
                if (tmp->next == (Queue *) 0)
                        return;
                else
                        tmp = tmp->next;
        }
        say("QUEUE: No more queues");
}

/* output the contents of a queue to the screen */
static void	print_queue (Queue *queue)
{
	CmdList *tmp;
	int 	x = 0;
	
        tmp = queue->first;
	while (tmp != (CmdList *) 0) 
        {
		if (tmp->what)
			say("<%s:%2d> %s",queue->name,++x,tmp->what);
		tmp = tmp->next;
	}
        say("<%s> End of queue",queue->name);
}

/* return the number of entries in a queue */
static int	num_entries (Queue *queue)
{
	int x = 1;
	CmdList *tmp;

        if ((tmp = queue->first) == (CmdList *) 0) 
                return 0;
	while (tmp->next) 
        {
                x++;
		tmp = tmp->next;
	}
	return x;
}
