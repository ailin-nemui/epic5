/*
 * names.h: Header for names.c
 *
 * Copyright 1997 EPIC Software Labs
 * See the COPYRIGHT file, or do a HELP IRCII COPYRIGHT 
 */

#ifndef __names_h__
#define __names_h__

struct WindowStru;

#ifdef Char
#undef Char
#endif
#define Char const char

	void	add_channel		(Char *, int);
	void	remove_channel		(Char *, int);
	void	add_to_channel		(Char *, Char *, int, int, int, int, int);
	void	add_userhost_to_channel	(Char *, Char *, int, Char *);
	void	remove_from_channel	(Char *, Char *, int);
	void	rename_nick		(Char *, Char *, int);
	int	im_on_channel		(Char *, int);
	int	is_on_channel		(Char *, Char *);
	int	is_chanop		(Char *, Char *);
	int	is_chanvoice		(Char *, Char *);
	int	is_halfop		(Char *, Char *);
	int	number_on_channel	(Char *, int);
	char *	create_nick_list	(Char *, int);
	char *	create_chops_list	(Char *, int);
	char *	create_nochops_list	(Char *, int);
	void	update_channel_mode	(Char *, Char *);
	Char *	get_channel_key		(Char *, int);
	char *	get_channel_mode	(Char *, int);
	int	is_channel_private	(Char *, int);
	int	is_channel_nomsgs	(Char *, int);
	void	list_channels		(void);
	void	switch_channels		(char, char *);
	void	change_server_channels	(int, int);
	void	destroy_waiting_channels	(int);
	void	destroy_server_channels	(int);
	void	reconnect_all_channels	(void);
	Char *	what_channel		(Char *);
	Char *	walk_channels		(int, Char *);
	Char *	fetch_userhost		(int, Char *);
	int	get_channel_oper	(Char *, int);
	int	get_channel_voice	(Char *, int);
struct WindowStru *get_channel_window	(Char *, int);
	void	set_channel_window	(struct WindowStru *, Char *);
	char *	create_channel_list	(int);
	void	channel_server_delete	(int);
	void	save_channels		(int);
	void	remove_from_mode_list	(Char *);
	int	auto_rejoin_callback	(void *);
	void	swap_channel_win_ptr	(struct WindowStru *, struct WindowStru *);
	void	reassign_window_channels	(struct WindowStru *);
	void	move_channel_to_window	(const char *, struct WindowStru *, struct WindowStru *);
	void	unset_window_current_channel (struct WindowStru *, 
						struct WindowStru *);
	void	reset_window_current_channel (struct WindowStru *);
	char *	scan_channel		(char *);
	char *	check_channel_type	(char *);
	int	channel_is_syncing	(const char *, int);
	void	channel_not_waiting	(const char *, int); 
	void	channel_check_windows	(void);
	void	cant_join_channel	(const char *, int);

#endif /* _NAMES_H_ */
