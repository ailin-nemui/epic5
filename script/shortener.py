"""A URL shortening service running inside epic.
"""
import logging
from http.server import HTTPServer, BaseHTTPRequestHandler

from epic import EPIC_COMMIT_ID, EPIC_RELEASE_NAME, EPIC_RELEASE_VERSION, \
                 NOISE_QUIET, SocketServerMixin, alias, command, on, xecho, register_listener_callback

HOST_NAME = '127.0.0.1'
PORT_NUMBER = 8080
STATIC_URLS = {'/epic': 'http://epicsol.org/'}
LAST_RESORT = 'http://lmgtfy.com/?q=WTF%3F'
REDIRECTIONS = []
URL_FILE = 'irc_urls.txt'
__version__ = '0.1'
httpd = None
log = logging.getLogger(__name__)


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
    nick = args.split()[0]

    for url in find_url(args.split()):
        if url not in REDIRECTIONS:
            REDIRECTIONS.append(url)

        xecho('From %s: http://%s:%s/%s' % (nick, HOST_NAME, PORT_NUMBER, REDIRECTIONS.index(url)))


class RedirectHandler(BaseHTTPRequestHandler):
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

    def log_message(self, format, *args):
        """Direct all log messages to the client.
        """
        message = format % args
        log.info("%s - - [%s] %s", self.address_string(), self.log_date_time_string(), message)

    def version_string(self):
        """Returns the server version reported in headers.
        """
        return '%s %s epic5 %s (%s) (%s)' % (__name__, __version__, EPIC_RELEASE_VERSION, EPIC_RELEASE_NAME, EPIC_COMMIT_ID)


class EPICHTTPServer(SocketServerMixin, HTTPServer):
    pass


# Start the HTTP Server
httpd = EPICHTTPServer((HOST_NAME, PORT_NUMBER), RedirectHandler)

#skullY'2021
