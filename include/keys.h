/*
 * keys.h: header for keys.c 
 *
 * Copyright 2002 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __keys_h__
#define __keys_h__

/* This is a typedef for a function used with the /BIND system.  The
 * functions all live in input.c.  Following it is a macro to quickly define
 * functions to handle keybindings. */
typedef void (*BindFunction) (char, char *);
#define BUILT_IN_BINDING(x) void x (char key, char *string)

/* This is the structure used to hold binding functions. */
struct Binding {
    struct Binding *next;   /* linked-list stuff. :) */

    char    *name;	    /* the name of this binding */
    BindFunction func;	    /* function to use ... */
    char    *alias;	    /* OR alias to call.  one or the other. */
    char    *filename;	    /* the package which added this binding */
};
extern struct Binding *binding_list; /* list head for keybindings */

struct Binding	*add_binding	(char *, BindFunction, char *);
void		remove_binding	(char *);
struct Binding *find_binding	(char *);
void		init_binds	(void);

#define KEYMAP_SIZE 256
struct Key {
    unsigned char val;	/* the key value */
    unsigned char changed; /* set if this binding was changed post-startup */
    struct Binding *bound; /* the function we're bound to. */
    struct Key *map;    /* a map of subkeys (may be NULL) */
    char    *stuff;     /* 'stuff' associated with our binding */
    char    *filename;  /* the package which added this binding */
};

extern struct Key *head_keymap; /* the head keymap.  the root of the keys.  */

struct Key     *handle_keypress	(struct Key *, struct timeval,
				 unsigned char);
struct Key     *timeout_keypress(struct Key *, struct timeval);

BUILT_IN_COMMAND(bindcmd);
BUILT_IN_COMMAND(rbindcmd);
BUILT_IN_COMMAND(parsekeycmd);

void		init_keys 	(void);
void		init_termkeys 	(void);
void		set_key_interval(int);
void		save_bindings 	(FILE *, int);
void		remove_bindings	(void);
void		unload_bindings	(const char *);
void		do_stack_bind	(int, char *);
#endif /* __keys_h_ */
