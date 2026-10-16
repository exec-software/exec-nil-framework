@pragma once

# ny.lang.net currently guarantees transport-independent HTTP protocol values
# and request/response construction. The HTTP client, HTTP server, NyWeb,
# sockets, WebSocket, IMAP, and SMTP designs remain under experimental/ until
# a built provider and route-level integration tests ship with the SDK.
package nil.std.net

import nil.std.net.http
