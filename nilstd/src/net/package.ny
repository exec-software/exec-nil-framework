@pragma once

# nil.std.net provides transport-independent HTTP protocol values and
# request/response construction (http.ny), and TCP stream sockets backed by
# runtime intrinsics (socket.ny). Sockets are interpreter-backed; the native
# route does not carry them yet.
#
# The HTTP client, HTTP server, WebSocket, IMAP and SMTP designs remain under
# experimental/ until a built provider and route-level integration tests ship
# with the SDK. They now have a transport to be built on.
package nil.std.net

import nil.std.net.http
import nil.std.net.socket
