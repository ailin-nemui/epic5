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

Quick Examples
==============

@alias('epic_command')
def epic_command(args):
    '''A python function working as an epic alias.
    '''
    say('Saying ' + args)
    say('Version: ' + expression('info(i)'))


@on('hook')
def on_hook(args):
    '''A python function working as an epic hook.
    '''
    say('Hooked: ' + args)


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

There are three primary ways that your python code will be executed by EPIC:

* User typed commands
* Event Triggers
* Socket Listeners

User Typed Commands
-------------------

Much like your shell, a command typed at the EPIC prompt has the full power
of the ircii language available to it. This allows the user to build powerful
constructs on their irc input line, much in the same way shells allow you to
build powerful constructs:

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

Socket Listeners
---------------

If you want to provide a network service, such as an HTTP or IRC server, you
will need to register your listening socket with EPIC. You can do this with
the `register_listener_callback()` function or the `SocketServerMixin` class,
both explained below.

Logging and Output
==================

EPIC configures a log handler for you. You don't have to worry about logging
details inside your script, simply setup a logger like so:

    import logging

    logger = logging.getLogger('my_script')
    logger.info('Hello, World!')

Anything output to STDOUT or STDERR will automatically be captured and output
into the user's EPIC window.

Classes
=======

This module comes with one class.

SocketServerMixin
-----------------

If you use a server based on Python's `socketserver.BaseRequestHandler` class
you can easily hook into EPIC's callback system by using `SocketServerMixin`.
Most of the included servers, such as `socketserver.TCPServer` and
`http.server.HTTPServer`, work with this mixin.

Under the hood `epic.SocketServerMixin` registers a socket callback for
your class and overloads `handle_request()` to integrate with EPIC's event
loop.

Decorators
==========

This module comes with two decorators.

* @alias(): Register function as a command that can be typed at the prompt
* @on(): Register function to be executed when an event happens

@alias()
--------

When a function is decorated with @alias() it will be executed when the
user types the name of your alias. Anything your function returns will be
ignored. If you wish to convey information back to the user you will need
to use a `logger`, `echo()`, `xecho()`, or `command()`.

Alias functions will be called with exactly one argument- a string
containing any arguments typed after the command.

Example:

    @alias('hello')
    def hello(args):
        xecho('Hello, %s!' % args)

When the user types "/hello Python World" it will translate to
`hello('Python World')`.

All python aliases have a small ircii shim installed. You can examine
this shim by typing "/alias hello" at the EPIC prompt.

@on()
-----

When an event happens EPIC will call any registered event handlers. Events
are thrown for basically everything that happens. By the time your client
has connected to a server, before it has even joined a channel, dozens of
events have been thrown.

You use the @on() decorator to register your event handler. An event handler
is a function that takes a single argument. When called it will be passed a
string containing one or more words corresponding to the event. For now
parsing that string is up to each event handler.

Example:

    @on('privmsg', '*enemy ships*', NOISE_QUIET)
    def enemy_ships(args):
        xecho("It's a trap!")

In this example anytime someone sends a message with the phrase "enemy ships"
their client will print out a warning:

    *** It's a trap!

All python event triggers have a small ircii shim installed. You can examine
the shim installed for our example by typing "/on privmsg" at the EPIC prompt.

Calling EPIC From Python
========================

If you want EPIC to do something for you there are 3 primary ways to do so.

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
untrusted input. This will not do any parsing of the command line.

If you use evaluate() EPIC will parse the line first to replace $-strings
with their expandos.

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

register_listener_callback()
----------------------------

Due to the way EPIC embeds python your python code will not have its own main
loop. To allow network services to work EPIC lets you register a callback for
a network socket using `register_listener_callback()`. When new connections
come in your callback will be called so you can handle the incoming request.

Calling Python from EPIC
========================

There are two primary ways to call Python from within EPIC. You can execute
a statement using /python or execute an expression you using $python().

/python
-------

You can use /python to execute a python statement from the top-level
(__main__) namespace. You can not execute an expression using /python,
if you attempt to do so a stack trace will be output to the currently
active window.

$python()
---------

You can use $python() to execute a python expression and get the resulting
object back. Strings will be passed as bare strings, any other object
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
python code that interfaces with EPIC, but it does not talk about how
to actually use EPIC. EPIC has a rich feature set which you can access
from Python, please take some time to browse the full documentation
here:

    http://epicsol.org/help_root

If you want to know more, or just want to chat with like-minded programmers,
please come talk to us:

    Network: EFnet (irc.efnet.net)
    Channel: #epic

Acknowledgements
================

EPIC5 and the low-level Python functionality was written by hop.

This document and the high-level Python module was written by skullY.

A big thanks to hop for being willing to add "python support that doesn't
suck". He has spent many hours both talking to me and learning about how
to embed python in his program.

Finally, thanks to caf and everyone else in #epic for providing feedback
along the way. This integration is stronger for it.
"""


# FIXME: Remove this
NOTES = """
hop said he should implement _epic.set_set().
It exists, but it throws a "not implemented" exception.
You can use symbolctl(PMATCH BUILTIN_VARIABLE name) will return an empty string if that set does't exist.
You can PMATCH, CREATE, and DELETE.
This is the only way to get at some things like builtin variables.
"""

import logging
import sys
from importlib import reload

from _epic import callback_when_readable, cancel_callback, cmd, eval, expand, expr, echo, say, call
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

# Tracking object for registered socket listeners.
# Format: _listening_sockets[<fd_num>] = (<dispatch_function>, <cleanup_function>)
_listening_sockets = {}


# Classes that help script writers interact with epic
class Console(object):
    """Write a file-like object to the epic console.
    """
    def __init__(self, output_name='PYTHON-CONSOLE'):
        self.output_name = output_name

    def write(self, buf):
        for line in buf.rstrip().splitlines():
            if self.output_name:
                echo("%s: %s" % (self.output_name, line))
            else:
                echo(line)

    def flush(self):
        pass


class SocketServerMixin(object):
    """Modify most SocketServers to work with EPIC.

    Because EPIC is a single-threaded program that controls the main loop,
    Python code is only run when EPIC calls into it. This mixin overrides
    `handle_request()` so that EPIC can dispatch to that function when a new
    connection happens. The listening socket is automatically registered with
    EPIC's callback system.

    Set `self.server_name` to change the name reported in log messages.
    """
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.server_name = self.__class__.__name__
        register_listener_callback(self.socket.fileno(), self.handle_request, self.server_close)

    def handle_request(self, vfd):
        """Handle a single request from the parent socket.
        """
        self._handle_request_noblock()
        self.service_actions()

    def server_close(self, vfd=None, *args, **kwargs):
        """Remove the callback and close the parent socket.
        """
        cancel_callback(self.socket.fileno())
        super().server_close(*args, **kwargs)


# Configure our output
sys.stdout = Console('PYTHON-STDOUT')
sys.stderr = Console('PYTHON-STDERR')
log = logging.getLogger('epic')
log.setLevel(logging.DEBUG)
log_handler = logging.StreamHandler(Console(None))
log_handler.setLevel(logging.INFO)
#log_formatter = logging.Formatter('%(asctime)s: %(name)s: %(levelname)s: %(message)s', '%H:%M:%S')
log_formatter = logging.Formatter('%(asctime)s: python.%(module)s: %(message)s', '%H:%M:%S')
log_handler.setFormatter(log_formatter)
log.addHandler(log_handler)


# Functions that epic python scripts can utilize.
def set_set(set_key, set_value, *, quiet=True):
    """Assign a value to a /set in EPIC.

    A /set is a key-value pair that always exists.
    """
    quiet = '^' if quiet else ''

    return command('%sSET %s %s' % (quiet, set_key, set_value))


def assign(assign_key, assign_value, *, quiet=True):
    """Assign a value to a /assign in EPIC.

    A /assign is a key-value pair that may or may not exist.
    """
    quiet = '^' if quiet else ''

    return command('%sASSIGN %s %s' % (quiet, set_key, set_value))


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


def on(event_type, wildcard_pattern='*', noise_indicator=NOISE_SILENT,
    exclude_match=False, delete=False, serial_number='-',
    flexible_pattern=False):
    """A decorator used to register an epic event handler.

    For complete detail about how epic event handlers work consult the epic5
    documentation:

        <http://epicsol.org/on>

    Uniqueness
    ----------

    ONs have to be “unique”. The primary key of an ON is:

        * Event Type
        * Serial Number
        * Wildcard Pattern

    That means every ON has to have these three pieces of information.
    Creating an ON is like an “upsert” – if there is not an existing ON, you
    will create it. If there is an existing ON, you will update it. If you
    do not provide a serial number one will be selected for you.

    Serial Numbers
    --------------

    Every ON has a serial number. The default is '-'. You will want to choose
    a unique serial number for your script or leave the default, which will
    automatically choose an unused serial number less than 0.

    For each serial number at most one `@on()` will be executed. EPIC will
    determine the best match among the available Wildcard Patterns and execute
    only that hook.

    Serial numbers are executed in order. Serial number 0 is special in that
    it can supress EPIC's default output for a hook, see the full documentation
    for /on for more detail: <http://epicsol.org/on>

    Arguments
    ---------

    `event_type`
        The event that we're hooking, EG: PUBLIC or MSGS. The full list of
        possible event types is on <http://epicsol.org/help_root>, and can
        by found by looking for the pages that start with `on_`.

    `wildcard_pattern`
        The pattern you want to match. Within a pattern * matches any
        text while % matches any text until a space is encountered.

    `exclude_match`
        When True the `wildcard_pattern` is treated as an exclusion rather
        than a match.

    `delete`
        When True delete any matching hooks.

    `serial_number`
        The serial number for this hook. Use '-' to select the next unused
        number below 0, '+' to select the next unused number above 0, or
        specify your own integer here.

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
        log.debug("on %s%s%s%s %s%s%s%s%s {pydirect %s.%s $*}",
            sni, noise_indicator, event_type, serial_number, exclude_match,
            delete, quote_type, wildcard_pattern, quote_type, f.__module__,
            f.__name__
        )
        command("on %s%s%s%s %s%s%s%s%s {pydirect %s.%s $*}" % (
            sni, noise_indicator, event_type, serial_number, exclude_match,
            delete, quote_type, wildcard_pattern, quote_type, f.__module__,
            f.__name__
        ))
        return f

    return decorator


def register_listener_callback(fd, dispatch_function, cleanup_function):
    """Register a dispatch and a cleanup function for a listening file descriptor.
    """
    log.debug(
        'register_listener_callback(fd=%d, dispatch_function=%s, cleanup_function=%s)',
        fd, dispatch_function.__name__, cleanup_function.__name__
    )

    if fd in _listening_sockets:
        xecho('FD already registered: %d' % fd)
        return False

    callback_when_readable(fd, dispatch_function, cleanup_function, 0)
    _listening_sockets[fd] = (dispatch_function, cleanup_function)

    return True


@on('exit')
def cleanup_listener_callbacks(args):
    """Called by our exit hook to close up our servers.
    """
    log.debug('cleanup_listener_callbacks(args=%s)', repr(args))
    for fd, funcs in _listening_sockets.items():
        cancel_callback(fd)
        funcs[1]()  # Run the cleanup function
