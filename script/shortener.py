"""A URL shortening service running inside epic.
"""
from http.server import HTTPServer, BaseHTTPRequestHandler

from epic import EPIC_COMMIT_ID, EPIC_RELEASE_NAME, EPIC_RELEASE_VERSION, \
                 NOISE_QUIET, alias, command, on, xecho
from _epic import callback_when_readable, cancel_callback

HOST_NAME = '127.0.0.1'
PORT_NUMBER = 8080
STATIC_URLS = {'/epic': 'http://epicsol.org/'}
LAST_RESORT = 'http://lmgtfy.com/?q=WTF%3F'
REDIRECTIONS = []
URL_FILE = 'irc_urls.txt'
__version__ = '0.1'
httpd = None

# ---- creating shortened urls for the user ----
def find_url(haystack):
    """Return any URL's within the haystack string.
    """
    results = []
    for word in haystack:
        xecho('trying ' + word)
        if word.startswith('http:') or word.startswith('https:'):
            results.append(word)

    return results

@on('action', '*http:*', NOISE_QUIET)
@on('action', '*https:*', NOISE_QUIET)
@on('general_notice', '*http:*', NOISE_QUIET)
@on('general_notice', '*https:*', NOISE_QUIET)
@on('general_privmsg', '*http:*', NOISE_QUIET)
@on('general_privmsg', '*https:*', NOISE_QUIET)
@alias('shorten_url')
def url_handler(args):
    """Extract URL's from messages and create short URLs.
    """
    xecho('handling urls from %s' % (args,))
    for url in find_url(args.split()):
        if url not in REDIRECTIONS:
            REDIRECTIONS.append(url)

        xecho('http://%s:%s/%s' % (HOST_NAME, PORT_NUMBER, 
                                   REDIRECTIONS.index(url)))

# ----- handling the shortened urls from the user -----
#
# These two functions get called back by epic when httpd.fileno() 
# is readable.  connection_dispatch() asks socketserver to do its
# thing, and error_dispatch() cleans up after ourselves
#
# XXX TODO XXX There should probably be some kind of restart
# mechanism.  In theory, this server should never quit.
#
def connection_dispatch(vfd):
    global httpd
    httpd._handle_request_noblock()
    httpd.service_actions()

def error_dispatch(vfd):
    global httpd
    cancel_callback(httpd.socket.fileno())
    httpd.server_close()
    httpd = None

#
# This is a socketserver callback.  
#  When a physical connection comes in, it is handled by httpd._handle_request_nonblock().
#  This creates an application level event, which is handled by httpd.service_actions().
#  Because this class was hooked up the httpd as its application callback (see below)
#  this gets called when we do httpd.service_actions()
#
class RedirectHandler(BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        """Direct all log messages to the client.
        """
        xecho("%s - - [%s] %s" % (self.address_string(),
                                  self.log_date_time_string(),
                                  format % args))

    def version_string(self):
        return 'shortener.py %s epic5 %s (%s) (%s)' % (__version__,
                                                       EPIC_RELEASE_VERSION,
                                                       EPIC_RELEASE_NAME,
                                                       EPIC_COMMIT_ID)

    def do_HEAD(self):
        if self.path in STATIC_URLS:
            location = STATIC_URLS.get(self.path)
        else:
            try:
                id = int(self.path[1:])
                if REDIRECTIONS and id < len(REDIRECTIONS):
                    location = REDIRECTIONS[id]
                else:
                    location = LAST_RESORT
            except ValueError:
                location = LAST_RESORT

        self.send_response(301)
        self.send_header("Location", location)
        self.end_headers()

    def do_GET(self):
        self.do_HEAD()

# Start the HTTP Server
httpd = HTTPServer((HOST_NAME, PORT_NUMBER), RedirectHandler)

# Ask EPIC to tell us every time this socket is readable.
callback_when_readable(httpd.socket.fileno(), connection_dispatch, error_dispatch, 0)

#skullY'2020 (but blame hop for any bugs)
