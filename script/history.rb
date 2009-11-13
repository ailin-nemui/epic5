#
# # # # # # # # # # # # # # #  # # # # # # # # # # 
# # # # # This is just a proof of concept! # # # # 
# # # # # DOES NOT WORK! DO NOT USE! # # # # # # #
# # # # # # # # # # # # # # #  # # # # # # # # # # 
return;

# Copyright 2007 EPIC Software Labs
#
# This is my attempt to port BlackJac's history script to ruby.
# Just for fun!
#
# Settings:
#	set history [0|<positive integer>]
#		Controls the maximum number of entries to be stored in
#		the client's history buffer. Setting it to 0 purges the
#		history buffer and disables command history.
#	set history_circleq [on|off|toggle]
#		Controls the behavior of history browsing when you reach
#		the beginning or end of the buffer. If set to on, when
#		you reach the beginning of the buffer and press the
#		backward_history keybinding (defaulted to the up arrow)
#		again, the history will circle back to the end of the
#		buffer. In addition, when you reach the end of the
#		buffer and press the forward_history keybinding
#		(defaulted to the down arrow) again, the history will
#		circle back to the beginning of the buffer.
#	set history_persistent [on|off|toggle]
#		Controls whether the current session's history buffer
#		will be saved on exit and read back into memory the next
#		time the script is loaded.
#	set history_remove_dupes [on|off|toggle]
#		Controls whether history buffer entries that are exact
#		duplicates of the most recent entry are removed from the
#		buffer.
#	set history_save_file <filename>
#		Default filename to use when saving the current history
#		buffer or reading a previous buffer and adding it to the
#		current buffer.
#	set history_save_position [on|off|toggle]
#		Controls whether the backward_history or forward_history
#		keybindings will start scrolling through the history
#		buffer from the spot where a previous
#		/!<indexnum|pattern> match was found, or from the most
#		recent history buffer entry.
#	set history_timestamp [on|off|toggle]
#		Controls whether /history displays the timestamp of each
#		command.
#
# Commands:
#	/history [<indexnum>]
#		Returns a list of all commands in the history buffer, or
#		all commands up to <indexnum>.
#	/!<indexnum|pattern>
#		Retrieves history buffer entries and outputs them to the
#		input line. Use /!<indexnum> to retrieve <indexnum> or
#		/!<pattern> to retrieve the most recent entry matching
#		<pattern>.
#
# Functions:
#	$historyctl(add <text>)
#		Adds <text> to the next available index number in the
#		history buffer. 
#		Returns 0 if the history buffer is disabled.
#		Returns 1 if successful.
#	$historyctl(delete <indexnum>)
#		Removes <indexnum> from the history buffer.
#		Returns -2 if the history buffer is disabled.
#		Returns -1 if <indexnum> does not exist.
#		Returns 0 if successful.
#	$historyctl(get <indexnum>)
#		Returns the history buffer item <indexnum> if it exists,
#		or nothing.
#	$historyctl(index <indexnum>)
#		Moves the history buffer scrollback pointer to item
#		<indexnum>.
#		Returns 0 if <indexnum> does not exist.
#		Returns 1 if successful.
#	$historyctl(read [<filename>])
#		Reads into the history buffer the contents of <file> or
#		HISTORY_SAVE_FILE if none is specified.
#		Returns nothing if the read failed.
#		Returns > 0 if the read succeeds.
#	$historyctl(reset)
#		Clears the entire history buffer.
#		Returns -1 if the history buffer does not exist.
#		Returns 0 if successful.
#	$historyctl(save [<filename>])
#		Writes the current history buffer to <file> or
#		HISTORY_SAVE_FILE if none is specified.
#		Returns nothing if the write failed.
#		Returns > 0 if the write succeeds.
#	$historyctl(set <buffer size>)
#		Sets the size of the history buffer. Setting it to 0
#		purges the history buffer and disables command history.
#
BEGIN {

class HistoryItem
	attr_reader :timestamp, :stuff
	attr_writer :timestamp, :stuff

	def initialize (stuff)
		@timestamp = Time.new
		@stuff = stuff
	end
end

class History
  def History.cmd(args)
	EPIC.say("Command History:")
	History.items.each_with_index { |x, index|
		str = "#{index}"
		if (EPIC.expr("history_timestamp") == 'on')
			str += " [#{x.timestamp}] "
		else
			str += ": "
		end
		str += x.stuff
		EPIC.echo(str)
	}
  end

  def History.add(str)
	if str.length == 0
		return
	end

	if (EPIC.expr("history_remove_dupes") == 'on')
		History.items.reject! {|x| x == str}
		while (History.items.size >= EPIC.expr("history"))
			History.items.shift
		end

		History.items.push(str)
		History.index = nil;
	end
  end

  def History.erase
	History.index = nil;
	EPIC.cmd("parsekey reset_line")
  end

  def History.get_backward
	input = EPIC.expr("L")
	if (input.length > 0 && History.index == nil)
		History.add(input)
		History.index = History.items.length - 1
	end

	if (History.index == 0)
		if (EPIC.expr("history.circleq") == 'on')
			History.index = History.items.length - 1
		end
	else
		History.index = History.index - 1
	end

	History.show(History.index)
  end

  def History.get_forward
	input = EPIC.expr("L")
	if (input.length > 0 && History.index == nil)
		History.add(input)
	end

	if (History.index == History.items.length - 1 || History.index == nil)
		if (EPIC.expr("history.circleq") == 'on')
			History.show(0) 		# Oldest entry!
		else
			History.erase			# A blank line
		end
	elsif (History.index < History.items.length && History.index != nil)
		History.index = History.index + 1
		History.show(History.index)
	end
  end

  def History.shove
	str = EPIC.expr("L")
	History.add(str)
	EPIC.cmd("parsekey reset_line")
  end

  def History.show(index)
	if (index == nil)
		return
	end

	History.index = index
	EPIC.cmd("parsekey reset_line #{History.items[History.index].stuff}")
  end

  def History.historyctl(args)
	[action, rest] = args.split(' ', 2)
	case $action
	    when "add"
		if (History.max <= 0)
			return 0
		end
		History.add(rest)
		return 1
	    when "delete"
		index = rest.to_i
		History.items.delete_at(index)
		return 1
	    when "get"
		index = rest.to_i
		return History.items[index].stuff
	    when "index"
		index = rest.to_i
		History.index = index
		return 1
	    when "read"
		if (rest.length != 0)
			filename = rest
		else
			filename = save_file
		end
		File.foreach(filename) {|x|
			History.add(x)
		}
	    when "reset"
		History.index = nil
		History.items.clear
		return 1
	    when "save"
		if (rest.length != 0)
			filename = rest
		else
			filename = save_file
		end
		f = File.open(filename, "w")
		History.items.each {|x|
			f.puts("#{x.timestamp} #{x.stuff}")
		}
		f.close
		return 1
	    when "set"
		History.sethistory(rest)
		return 1
	    else
	end
  end

  def History.sendline(str)
	if (str.length == 0)
		return
	end
	History.add(str)
	EPIC.cmd("//sendline #{str}")
  end

  def History.banghandler(str)
>>> TODO >>>
	@ :find = after(! $0);
	if (isnumber($find)) {
		if (:found = getitem(array.history $find)) {
			xtype -l $restw(1 $found)${*1 ? [$1-] : []};
		} else {
			xecho -b -c No such history entry: $find;
		};
	} else if (:found = [$getmatches(array.history % /$find*) $getmatches(array.history % $find*)]) {
		@ :index = rightw(1 $numsort($found));
		if (history_save_position == [on]) {
			@ history.index = index;
		};
		xtype -l $restw(1 $getitem(array.history $index))${*1 ? [ $1-] : []};
	} else {
		xecho -b -c No match;
	};
<<<< TODO <<<<
  end

  def History.sethistory(numstr)
	limit = numstr.to_i
	if (limit == 0)
		History.index = nil;
		History.items.clear
		EPIC.eval("^bind ^] nothing;");
		EPIC.eval("^on #input 2 -*");
		EPIC.eval{"^on #input 2 -/!*");
	else
		while (limit > History.items.length)
			History.trim
		end
		EPIC.eval("^bind ^] shove_to_history")
		EPIC.eval("^on #-input 2 * {history.add $*}")
		EPIC.eval("^on #-input 2 /!* #")
	end
  end

  def History.setpersistent(boolstr)
	case boolstr
		when 'on'
			EPIC.eval("on #-exit 2 * {@ historyctl(save $history_save_file)}")
		when 'off'
			EPIC.eval("on #exit 2 -*")
		else
			EPIC.yell("Huh?")
	end
   end
end
	dummy = <<IRCII
}

##########################################################
ruby {load '$word(1 $loadinfo())'}

package history
load addset

alias historyctl (action, ...) {return $ruby(History.historyctl '$*')}
alias sendline (...) {ruby {History.sendline '$*'}}
@ bindctl(function BACKWARD_HISTORY create "ruby {History.get_backward}")
@ bindctl(function ERASE_HISTORY create "ruby {History.erase}")
@ bindctl(function FORWARD_HISTORY create "ruby {History.get_forward}")
@ bindctl(function SHOVE_TO_HISTORY create "ruby {History.shove}")

fe (N [OB [[B) hh {@ bindctl(sequence ^$hh set forward_history)}
fe (P [OA [[A) hh {@ bindctl(sequence ^$hh set backward_history)}
@ bindctl(sequence ^U set erase_history)

^on ^input "/!*" {ruby {History.banghandler '$*'}}

addset history int {ruby {History.sethistory '$*'}}
set history 150

addset history_circleq bool;
set history_circleq on

addset history_persistent bool {ruby {History.setpersistent '$*'}}
set history_persistent off

addset history_save_file str
set history_save_file ~/.epic_history

addset history_remove_dupes bool
set history_remove_dupes off

addset history_save_position bool;
set history_save_position on

addset history_timestamp bool
set history_timestamp off

if (history_persistent == [on] && fexist($history_save_file) == 1) {
        @ historyctl(read $history_save_file)
}

: {
IRCII
}

