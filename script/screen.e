if (word(2 $loadinfo()) != [pf]) { load -pf $word(1 $loadinfo());return };

# This script is supposed to work on both epic4 and epic5.
#
#   Mix between altchan and screen. I didn't know about the screen script,
#   so I kinda remade it - but with a slightly different touch
#
#  Script gives the user the possibility to do various tasks using
#  the ^w key (create, destroy, swap windows), and swap windows
#  using meta-1234567890 (esc-..., alt-...)
#
#  ^w tries to mimic screen.
#
#   - howl, 2007 (howl@epicsol.org)


package E.SCREEN;
alias screen.window_swap_or_last (win) {
	^window ${(winnum($win) == winnum()) ? [swap last] : [refnum_or_swap $win]};
};
alias screen.new_window {
	^window new hide swap last;
};
alias screen.kill_window {
	input_char "Really kill window $winnum()${winnam() != [] ? [ \[$winnam()\]] : []}? [y/N]"
		\{if \([\$0\] == [y]\) \{window $winnum() kill\}\};
};
alias screen.quit_client {
	input_char "Really quit client? [y/N]"
		\{if \([\$0\] == [y]\) \{quit User ipressed ^W^D\}\};
};
alias screen.window_xon {
	^window hold_mode on;
};
alias screen.window_xoff {
	^window hold_mode off;
};
alias screen.window_hide {
	^window hide;
};
alias screen.window_hide_others {
	^window hide_others;
};
alias screen.split_window {
	for w in ($winrefs()) {
		if (windowctl(GET $w visible) == 0) {
			^window show $w swap last;
			return;
		};
	};
	xecho -w No window to split to!;
};
alias screen.open_prompt {
	input : {
		switch ($0) {
			('*)
				{sendline / $after(' $*)};
			(//*) (/*)
				{sendline $*};
			(*)
				{sendline /$*};
		};
	};
};
alias screen.search_back {
	input "/" {
		^local pos;
		@ pos = windowctl(get 0 scrollback_distance);
		window search_back "$*";
		if (pos == windowctl(get 0 scrollback_distance)) {
		
		};
	};
};
alias screen.window_swappable_toggle {
	window swappable toggle;
};
alias screen.window_skip_toggle {
	window skip toggle;
};

# These functions should permit you to move away from and 
# swap windows from windows that aren't swappable.
alias screen.my_swap_previous_window {
	window prev;
};
alias screen.my_swap_next_window {
	window next;
};
alias screen.my_swap_last_window {
	window swap last;
};

alias screen.init {
	^local key,num,k;
	@ key = [^w];
	@ bindctl(FUNCTION WINDOW_SWAP_OR_LAST CREATE screen.window_swap_or_last);
	@ bindctl(FUNCTION NEW_WINDOW CREATE screen.new_window);
	@ bindctl(FUNCTION KILL_WINDOW CREATE screen.kill_window);
	@ bindctl(FUNCTION WINDOW_XON CREATE screen.window_xon);
	@ bindctl(FUNCTION WINDOW_XOFF CREATE screen.window_xoff);
	@ bindctl(FUNCTION WINDOW_HIDE CREATE screen.window_hide);
	@ bindctl(FUNCTION WINDOW_HIDE_OTHERS CREATE screen.window_hide_others);
	@ bindctl(FUNCTION SPLIT_WINDOW CREATE screen.split_window);
	@ bindctl(FUNCTION OPEN_PROMPT CREATE screen.open_prompt);
	@ bindctl(FUNCTION SEARCH_BACK CREATE screen.search_back);
	@ bindctl(FUNCTION QUIT_CLIENT CREATE screen.quit_client);
	@ bindctl(FUNCTION MY_SWAP_NEXT_WINDOW CREATE screen.my_swap_next_window);
	@ bindctl(FUNCTION MY_SWAP_PREVIOUS_WINDOW CREATE screen.my_swap_previous_window);
	@ bindctl(FUNCTION MY_SWAP_LAST_WINDOW CREATE screen.my_swap_last_window);
	@ bindctl(FUNCTION WINDOW_SWAPPABLE_TOGGLE CREATE screen.window_swappable_toggle);
	@ bindctl(FUNCTION WINDOW_SKIP_TOGGLE CREATE screen.window_skip_toggle);

	for num in (1 2 3 4 5 6 7 8 9 0 + \\) {
		@ k = (num == 0 ? 10 : num);
		@ k = (num == [+] ? 11 : k);
		@ k = (num == [\\] ? 12 : k);
		bind ^[$num window_swap_or_last $k;
		bind ${key}${num} window_swap_or_last $k;
	};

	@ bindctl(sequence "$key " set my_swap_next_window);
	@ bindctl(sequence "^@" set my_swap_next_window);
	@ bindctl(sequence "$key^@" set my_swap_next_window);
	@ bindctl(sequence "$key^n" set my_swap_next_window);
	@ bindctl(sequence "$key>" set my_swap_next_window);
	@ bindctl(sequence "^_" set my_swap_previous_window);
	@ bindctl(sequence "$key^_" set my_swap_previous_window);
	@ bindctl(sequence "$key^?" set my_swap_previous_window);
	@ bindctl(sequence "$key<" set my_swap_previous_window);
	@ bindctl(sequence "$key$key" set my_swap_last_window);
	@ bindctl(sequence "$key^I" set next_window);
	@ bindctl(sequence "${key}c" set new_window);
	@ bindctl(sequence "${key}l" set refresh_screen);
	@ bindctl(sequence "${key}L" set window_swappable_toggle);
	@ bindctl(sequence "${key}^l" set window_skip_toggle);
	@ bindctl(sequence "${key}s" set window_xon);
	@ bindctl(sequence "${key}S" set split_window);
	@ bindctl(sequence "${key}K" set kill_window);
	@ bindctl(sequence "${key}^h" set window_hide);
	@ bindctl(sequence "${key}Q" set window_hide_others);
	@ bindctl(sequence "${key}q" set window_xoff);
	@ bindctl(sequence "${key}v" set quote_character);
	@ bindctl(sequence "${key}:" set open_prompt);
	@ bindctl(sequence "${key}/" set search_back);
	@ bindctl(sequence "${key}^D" set quit_client);
	@ bindctl(sequence "${key}d" set quit_client);
	alias winmove {return screen.winmove($*);};
	^alias -screen.init;
};

alias screen.winswap (from,to, ...)
{
	^local tmp;
	@ tmp = [];
	if (!isnumber($from) || !isnumber($to))
	{
		xecho -say -banner WinSwap requires numeric arguments;
		return;
	};
	if (windowctl(GET $from REFNUM) == [])
	{
		xecho -say -banner No such window: $from;
		return;
	};
	if (windowctl(GET $to REFNUM) == [])
	{
		xecho -say -banner No such window: $to;
		return;
	};
	@ tmp = word(0 $revw($numsort($winrefs())));
	@ tmp++;
	screen.winmove $from $tmp;
	screen.winmove $to $from;
	screen.winmove $tmp $to;
};

alias screen.winmove (from,to, ...)
{
	^local tmp;
	@ tmp = [];
	
	if (!isnumber($from) || !isnumber($to))
	{
		xecho -say -banner WinMove requires numeric arguments;
		return;
	};
	
	if (windowctl(get $from refnum) == [])
	{
		xecho -say -banner No such window: $from;
		return -1;
	};
	if (windowctl(get $to refnum) != [])
	{
		xecho -say -banner Window already exists: $from;
		return -1;
	};
	if (chanwin($from) != [])
	{
		^window new hide;
		@ tmp = word(1 $winrefs());
		if (tmp == to)
		{
			^window new hide;
			^window $tmp kill;
			@ tmp = word(1 $winrefs());
		}
		^window $tmp server $windowctl(GET $from SERVER);
		while (chanwin($from) != [])
		{
			^window $tmp channel "$chanwin($from)";
		}
	};

	window $from number $to;
	window refnum_or_swap $to;
	if (tmp != [])
	{
		while (chanwin($tmp) != [])
		{
			^window $to channel "$chanwin($tmp)";
		};
		^window $tmp kill;
	};
	return $to;
};




screen.init;


# vim: filetype=
