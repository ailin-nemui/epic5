/*
 *  queue.c - The queue command
 *
 *  Queues allow for future batch processing
 *
 *  Syntax:  /QUEUE -SHOW
 *  Syntax:  /QUEUE -DO -LIST -NO_FLUSH -DELETE -FLUSH <name>
 *  Syntax:  /QUEUE <name> <commands>
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

typedef struct  CmdListT {
        struct CmdListT *next;
        char     *what;
	char	 *subargs;
} CmdList;

typedef struct  QueueT {
        struct QueueT   *next;
        struct CmdListT *first;
        char     *name;
} Queue;

static  Queue   *queue_list = NULL;


static void 	display_all_queues (Queue **);
static int	queue_exists (Queue **, const char *);
static void	add_queue (Queue **, const char *);
static void	delete_queue (Queue **, const char *);
static void	list_queue (Queue **, const char *);
static void	list_all_queues (Queue **);
static void	run_queue (Queue **, const char *);
static int	add_to_queue (Queue **, const char *, const char *, const char *);
static void	delete_from_queue (Queue **, const char *, int);

BUILT_IN_COMMAND(queuecmd)
{
	char	*arg 		= NULL,
		*name 		= NULL,
                *body           = NULL,
		*orig;
	int	no_flush 	= 0,
		run 		= 0,
		list 		= 0,
		flush 		= 0,
		delete		= 0,
		number          = -1;

	if (!*args) {
		list_all_queues(&queue_list);
		return;
	}

	orig = LOCAL_COPY(args);
	while (args && *args && *args != '{') 
	{
	    arg = next_arg(args, &args);
	    if (*arg == '-' || *arg == '/')
	    {
		if (!my_strnicmp(arg + 1, "NO_FLUSH", 1))
		    no_flush = 1;
		else if (!my_strnicmp(arg + 1, "SHOW", 1)) {
		    display_all_queues(&queue_list);
		    return;
		} else if (!my_strnicmp(arg + 1, "LIST", 1))
		    list = 1;
		else if (!my_strnicmp(arg + 1, "DO", 2))
		    run = 1;
		else if (!my_strnicmp(arg + 1, "DELETE", 2))
		    delete = 1;
		else if (!my_strnicmp(arg + 1, "FLUSH", 1))
		    flush = 1;
		else if (!my_strnicmp(arg + 1, "HELP", 1)) {
			say("Usage: /QUEUE -SHOW");
			say("       /QUEUE -DO [-NO_FLUSH] <name>");
			say("       /QUEUE [-LIST] <name>");
			say("       /QUEUE [-FLUSH] <name>");
			say("       /QUEUE [-DELETE] <name> <number>");
			say("       /QUEUE <name> { <commands> }");
			return;
		}
		else {
		    yell("In '/QUEUE %s', I don't recognize '%s'. Use /QUEUE -HELP for syntax info.", orig, arg);
		    return;
		}
	    }
	    else if (name == NULL)
		name = arg;
	    else if (is_number(arg))
		number = atoi(arg);
	    else
		yell("In /QUEUE %s, I was expecting '%s' to be a number.", orig, arg);

	    if (number != -1)
		break;		/* Stop right here! */
	}

	if (name == NULL) {
	    yell("In /QUEUE %s, I couldn't find the queue name.", orig);
	    return;
	}

	body = args;
	if (body && *body)
        {
		const char *cmds;
		int	cnt;

		/*
		 * Find the queue based upon the previous queue
		 * Create a new queue if necessary.
		 */
		if (!queue_exists(&queue_list, name))
			add_queue(&queue_list, name);

		if (*body == '{') 
		{
			if (!(cmds = next_expr(&body, '{')))
			{
				say("QUEUE: I could not find a } to tell me where the commands end.");
				return;
			}
		} 
		else
		{
			say("QUEUE: The command body needs to be surrounded by curly braces");
			return;
		}

		if ((cnt = add_to_queue(&queue_list, name, cmds, subargs)))
			say("QUEUED: The queue '%s' now has %d entries", name, cnt);
	}

	if (run && no_flush == 0)
		flush = 1;

	if (run)
		run_queue(&queue_list, name);
	if (delete)
		delete_from_queue(&queue_list, name, number);
	if (list)
		list_queue(&queue_list, name);
	if (flush)
		delete_queue(&queue_list, name);
}

/*****************************************************************************/
static Queue *	lookup_queue (Queue **list, const char *name)
{
	Queue *q;

	for (q = *list; q; q = q->next)
	{
		if (!my_stricmp(q->name, name))
			return q;
	}
        return q;
}

static int	queue_exists (Queue **list, const char *name)
{
	if (lookup_queue(list, name))
		return 1;
	else
		return 0;
}

static int	queue_size (Queue *q)
{
	CmdList *c;
	int x = 0;

	for (c = q->first; c; c = c->next)
		x++;
	return x;
}

static void	list_one_queue (Queue *q)
{
	CmdList *c;
	int 	x = 0;
	
	for (c = q->first; c; c = c->next)
	{
	    if (c->what)
		say("<%s:%2d> %s", q->name, ++x, c->what);
	}

	say("<%s> End of queue", q->name);
}

static void    display_all_queues (Queue **list)
{
	Queue *	q;

	if (!*list) 
	{
		say("QUEUE: The are no queues pending.");
		return;
	}

	for (q = *list; q; q = q->next)
		list_one_queue(q);
}

static void	list_queue (Queue **list, const char *name)
{
	Queue *q;

	if (!(q = lookup_queue(list, name)))
	{
		say("QUEUE: The queue '%s' is not in use.", name);
		return;
	}

	list_one_queue(q);
}

static void	list_all_queues (Queue **list)
{
	Queue *q;
	int	size;

	if (!*list) 
	{
		say("QUEUE: There are no queues pending.");
		return;
	}

	for (q = *list; q; q = q->next)
	{
	    if ((size = queue_size(q)) == 1)
		say("Queue '%s' has '%d' entry", q->name, size);
	    else
		say("Queue '%s' has '%d' entries", q->name, size);
	}
}

static void	add_queue (Queue **list, const char *name)
{
	Queue *q, *newq;

	newq = (Queue *)new_malloc(sizeof(Queue));
	newq->next = NULL;
	newq->first = NULL;
	newq->name = m_strdup(name);

	for (q = *list; q && q->next; q = q->next)
		;

	if (q)
		q->next = newq;
	else
		*list = newq;
}

static void	delete_queue (Queue **list, const char *name)
{
	Queue *q, *p;
	CmdList *c, *n;

	/* Don't complain if it doesn't exist.  Why bother the user? */
	if (!(q = lookup_queue(list, name)))
		return;

	for (c = q->first; c; c = n)
	{
		n = c->next;
		new_free(&c->subargs);
		new_free(&c->what);
		new_free((char **)&c);
	}

	if (*list == q)
		*list = (*list)->next;
	else
	{
		for (p = *list; p->next != q; p = p->next)
			p->next = q->next;
	}

	new_free((char **)&q);
	return;
}

static void	run_queue (Queue **list, const char *name)
{
	Queue *q;
	CmdList	*c;

	if (!(q = lookup_queue(list, name)))
	{
		say("QUEUE: The queue '%s' is not in use.", name);
		return;
	}

	for (c = q->first; c; c = c->next)
	{
	    if (c->what)
		parse_line("QUEUE", c->what, c->subargs, 0, 0);
	}
}

static int	add_to_queue (Queue **list, const char *name, const char *what, const char *subargs)
{
	Queue *q;
	CmdList *c, *p;

	if (!(q = lookup_queue(list, name))) 
	{
		say("QUEUE: The queue '%s' is not in use.", name);
		return 0;
	}

	c = (CmdList *)new_malloc(sizeof(CmdList));
	c->what = m_strdup(what);
	c->subargs = m_strdup(subargs);
	c->next = NULL;

	if (!q->first)
		q->first = c;
	else
	{
		for (p = q->first; p->next;)
			p = p->next;
		p->next = c;
	}

	return queue_size(q);
}

static void	delete_from_queue (Queue **list, const char *name, int which)
{
	Queue *q;
	CmdList *c, *p;
	int	x;

	if (!(q = lookup_queue(list, name))) 
	{
		say("QUEUE: The queue '%s' is not in use.", name);
		return;
	}

	if (which == 0 || !q->first) {
		return;
	} else if (which == 1) {
		c = q->first;
		q->first = c->next;
	} else {
		p = q->first;
		for (x = 2; p && x < which; x++)
			p = p->next;
		if (!p)
			return;
		c = p->next;
		p->next = c->next;
        }

	new_free(&c->subargs);
	new_free(&c->what);
	new_free((char **)&c);
}

