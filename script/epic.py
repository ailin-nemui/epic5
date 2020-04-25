"""High-level interface to the EPIC python integration.

This module exposes functions and classes that make interfacing your python
code with the EPIC irc client more pythonic.

Getting Started
===============

Before you can load your script the environment must be initialized. We have
provided a script for this purpose that works for the most common cases. You
can run it by typing "/load python" at the EPIC prompt. If you wish to have
the python environment initilized each time you start EPIC you can add
"load python" to your `~/.epicrc` file.

EPIC For The Python Programmer
==============================

EPIC is a fork of the ircii client <https://en.wikipedia.org/wiki/IrcII>. It
has been actively developed since 1993. It is stable and mature and offers
many benefits for both background (bot) usage and as an IRC client.

As a client, EPIC offers a robust and well tested TUI. The flexibility of the
client allows you to customize the interface in almost every way. Through the
use of script packs a user can make her client behave according to a number of
different paradigms.

As a daemon, EPIC offers a robust and well tested event framework. All you 
need to worry about is what events you want to react to. EPIC handles the 
differences between IRC networks and presents them to you in a coherent way.
If you run your bot inside screen or tmux you can interactively examine and
interact with your code as it's running, making debugging easier.

There are two primary ways that your python code will be executed by EPIC:

* User typed commands
* Event Triggers

User Typed Commands
-------------------

Much like your shell, a command typed at the EPIC prompt has the full power 
of the ircii language available to it. This allows for some powerful 
constructs:

    > /fe (#epic #python) channel { join $channel }

You can create your own user-typed commands using the @alias() decorator
(explained below.) You can poke around the alias system yourself by typing
"/alias", or "/alias <alias_name>" at the EPIC prompt.

Event Triggers
--------------

An event trigger (aka "on" or "hook") is a piece of code that is executed
when an event happens. There are event triggers available for any event that
might happen within EPIC. Some of the more common events include connecting 
to a server, joining a channel, or a message being sent to the user or a 
channel the user has joined.

You can create your own event triggers using the @on() decorator (explained
below.) You can look at the existing event triggers by typing "/on" at the
EPIC prompt.

Decorators
==========

This module comes with two decorators.

* @alias(): Register function as a command that can be typed at the prompt
* @on(): Register function to be executed when an event happens

@alias()
--------

When a function is decorated with @alias(), it will be executed when the
user types the name of your alias. Anything your function returns will be
ignored. If you wish to convey information back to the user you will need
to make use of the xecho() function.

Alias functions will be called with exactly one argument- a string 
containing any arguments typed after the command.

Example:

    @alias('hello')
    def hello(args)
        xecho('Hello, %s!' % args)

When the user types "/hello Python World" it will translate to
`hello('Python World')`.

All python aliases have a small ircii shim installed. You can examine
this shim by typing "/alias hello" at the EPIC prompt.

@on()
-----

When an event happens, EPIC will call any registered event handlers. Events
are thrown for basically everything that happens. By the time your client
has connected to a server, before it has even joined a channel, dozens of
events have been thrown.

You use the @on() decorator to register your event handler. An event handler
is a function that takes a single argument. When called, it will be passed
a string containing one or more words corresponding to the event. For now
parsing that string is up to each event handler.

Example:

    @on('privmsg', '*enemy ships*', NOISE_QUIET)
    def enemy_ships(args):
        xecho("It's a trap!")

In this example anytime someone sends a message with the phrase "enemy ships"
their client will print out a warning:

    *** It's a trap!

All python event triggers have a small ircii shim installed. You can examine
this shim by typing "/on privmsg" at the EPIC prompt.

Calling EPIC From Python
========================

If you want EPIC to do something for you, there are 3 primary ways to do so.

xecho()
-------

You can use xecho() to output text to one or more of EPIC's windows. Your
text is prepended with the default banner by default, but this can be 
overridden. There are a lot of options that control where (or even if) your
text will be output. See the docstring for xecho() for more details.

Example:

    xecho("It's a trap!")

command() and evaluate()
------------------------

If you want to execute a command as if the user had typed it at the EPIC
prompt you can use either command() or evaluate(). The primary difference 
between the two is whether EPIC will parse the line and expand any variables 
or ircii functions it contains.

Most of the time you want to use command(), especially if you are passing in 
untrusted input.

These functions will always return None.

expression()
------------

When you need to get a piece of data from EPIC you can use expression().
EPIC expressions are roughly analogous to python expressions. Within
an expression you don't use $ to indicate an expando.

Example, check to see if they have logging turned on:

    expression('LOG') == 'ON'

Example, get the channel operators for a channel:

    chanops = expression('chops(#epic)')

Example, get the list of servers you're connected to:

    servers = expression('myservers()')

Calling Python from EPIC
========================

There are two primary ways to call Python from within EPIC. You can execute
a statement using /python. To execute an expression you must use $python().

/python
-------

You can use /python to execute a python statement from the top-level 
(__main__) namespace. You can not execute an expression using /python,
if you attempt to do so a stack trace will be output to the currently
active window.

$python()
---------

You can use $python() to execute a python expression and get the resulting
object back. Strings will be passed as bare strings, and any other object
type will be passed back as a repr() string.

Helpful Aliases
---------------

There are a couple helpful aliases you can use when developing.

* pyecho: Calls $python() and echos the output to your window
* pyload: Imports a python module
* pyreload: Re-imports a python module so you don't have to quit the client

Everything Else
===============

There are a lot of nooks and crannies that are outside the scope of this 
document. It is hoped that this gives you a good grounding for writing
python code that interfaces with EPIC. If it has not, or if it has and you
want to know more, please come talk to us:

    Network: EFnet (irc.efnet.net)
    Channel: #epic

Acknowledgements
================

This document and the high-level python module were written by skullY.

A big thanks to hop for being willing to add "python support that doesn't 
suck". He has spent many hours both talking to me and learning about how
to embed python in his program.

Finally, thanks to caf and everyone else in #epic for providing feedback
along the way. This integration is stronger for it.
"""

from importlib import reload
import sys

from _epic import cmd, eval, expand, expr, echo, say, call
from _epic import run_command, call_function, get_set, get_assign, get_var, builtin_cmd

# Map some commands to friendlier names
command = cmd
evaluate = eval
expression = expr

# Store some potentially useful data
EPIC_BINARY_CKSUM = expression('info(s)')
EPIC_COMMIT_ID = expression('info(i)')
EPIC_COMPILE_INFO = expression('info(c)')
EPIC_COMPILE_OPTS = expression('info(o)')
EPIC_NEW_MATH_PARSER = expression('info(m)') == '1'
EPIC_RELEASE_NAME = expression('info(r)')
EPIC_RELEASE_VERSION = expression('info(v)').split(' ', 1)[1].replace(' ', '.')

# Noise flags
NOISE_DEFAULT = ''
NOISE_SILENT = '^'
NOISE_QUIET = '-'
NOISE_NOISY = '+'
NOISE_SYSTEM = '%'

# Functions that epic python scripts can utilize.
def xecho(message, all=False, all_server=False, banner=True, current=False, 
    e=None, f=False, level=None, line=None, nolog=False, raw=False, say=False, 
    target=None, visible=False, window=None, x=False):
    """Output a line to the user's window.

    `message`
        The message to output

    `all`
        Output to all windows.

    `all_server`
        Output to all windows on the server.

    `banner`
        Prefix the message with the current banner

    `current`
        Output to current server's current window. This is different from the 
        visible option.

    `e`
        After specified number of seconds, echoed line is erased.

    `f`
        Do not notify (%F) if the window is hidden.

    `level`
        Use the given level to hunt for a window, overriding the current level 
        being used.

    `line`
        When used with `win` overwrite a particular line in that window.

    `nolog`
        Do not save the message to any log files that might apply.

    `raw`
        Output the message as a raw string to the underlying tty. This is used 
        to send control characters to the terminal emulator.

    `say`
        Do not output if display is being suppressed.

    `target`
        Use the given target to hunt for a window, overriding the current 
        target being used.

    `visible`
        Output to a visible window. Usually the current window or the top 
        window on the main screen.

    `window`
        Output to the specified window.

    `x`
        Overrule `set mangle_display` and pretend it was set to NORMALIZE 
        instead.
    """
    xecho_args = ['xecho']

    if all:
        xecho_args.append('-all')
    if all_server:
        xecho_args.append('-as')
    if banner:
        xecho_args.append('-b')
    if current:
        xecho_args.append('-current')
    if isinstance(e, int):
        xecho_args.append('-e %d' % e)
    if f:
        xecho_args.append('-f')
    if isinstance(level, int):
        xecho_args.append('-level %d' % level)
    if nolog:
        xecho_args.append('-nolog')
    if raw:
        xecho_args.append('-raw')
    if say:
        xecho_args.append('-say')
    if isinstance(target, int):
        xecho_args.append('-target %d' % target)
    if visible:
        xecho_args.append('-visible')
    if isinstance(window, int):
        xecho_args.append('-window %d' % window)
        if isinstance(line, int):
            xecho_args.append('-line %d' % line)
    if x:
        xecho_args.append('-x')

    # Print the message
    xecho_args.append('--')
    xecho_args.append(message)
    command(' '.join(xecho_args))


# Decorators for registering python functions as aliases or hooks
def alias(name):
    """A decorator used to register an epic alias.

    Epic aliases will always be called with a single argument, and that
    argument will be a string. Aliases are responsible for doing their own
    argument parsing.
    """
    def decorator(f):
        module = f.__module__
        function = f.__name__
        command("alias %s {pydirect %s.%s $*}" % (name, module, function))
        expression("symbolctl(SET %s 0 ALIAS PACKAGE %s.py)" % (function, 
            module))
        return f
        
    return decorator


def on(event_type, wildcard_pattern='*', noise_indicator=NOISE_DEFAULT, 
    exclude_match=False, delete=False, serial_number=None, 
    flexible_pattern=False):
    """A decorator used to register an epic event handler.

    For more detail about how epic event handlers work consult the epic5 
    documentation:

        http://epicsol.org/doku.php/on

    `event_type`
        The event that we're hooking, EG: PUBLIC or MSGS.

    `wildcard_pattern`
        The pattern you want to match. Within a pattern * matches any
        text while % matches any text until a space is encountered.

    `exclude_match`
        When True the `wildcard_pattern` is treated as an exclusion rather 
        than a match.

    `delete`
        When True delete any matching hooks.

    `serial_number`
        The serial number for this hook.

    `noise_indicator`
        A flag indicating how noisy this hook should be. By default all hooks 
        will output all echoed messages plus an alert, "ON <event_type> hooked
        by ...".

        * NOISE_DEFAULT: Show the echoed output plus the alert.
        * NOISE_SILENT: suppress all output, do not run the *default action*.
        * NOISE_QUIET: suppress all output, run the *default action*.
        * NOISE_NOISY: show all output
        * NOISE_SYSTEM: display echoed output and supress the *default action*

    `flexible_pattern`
        When true the `wildcard_pattern` will be expanded every time the hook
        is matched. When false the `wildcard_pattern` will be matched as is.
    """
    if serial_number:
        sni = '#'
        serial_number = ' ' + str(serial_number)
    else:
        sni = ''
        serial_number = ''

    delete = '-' if delete else ''
    exclude_match = '!' if exclude_match else ''
    quote_type = "'" if flexible_pattern else '"'

    if noise_indicator not in (NOISE_DEFAULT, NOISE_SILENT, NOISE_QUIET, 
                              NOISE_NOISY, NOISE_SYSTEM):
        echo('Unknown noise_indicator %s' % noise_indicator)
        noise_indicator = NOISE_DEFAULT

    def decorator(f):
        command("^on %s%s%s%s %s%s%s%s%s {pydirect %s.%s $*}" % (
            sni, noise_indicator, event_type, serial_number, exclude_match,
            delete, quote_type, wildcard_pattern, quote_type, f.__module__,
            f.__name__
        ))
        return f
        
    return decorator


# An example command
@alias('epic_command')
def epic_command(args):
    """A python function working as an epic alias.
    """
    say('Saying ' + args)
    say('Version: ' + expression('info(i)'))


# An example hook
@on('hook')
def on_hook(args):
    """A python function working as an epic hook.
    """
    say('Hooked: ' + args)


# # # # # #
# Capture STDOUT and STDERR
class CaptureStdout(object):
    def __init__(self):
        pass

    def write(self, buf):
        for line in buf.rstrip().splitlines():
            echo("PYTHON-OUTPUT: %s" % (line,))

class CaptureStderr(object):
    def __init__(self):
        pass

    def write(self, buf):
        for line in buf.rstrip().splitlines():
            echo("PYTHON-ERROR: %s" % (line,))
 
my_stdout = CaptureStdout()
my_stderr = CaptureStderr()

sys.stdout = my_stdout
sys.stderr = my_stderr
