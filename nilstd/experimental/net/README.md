# Experimental network transports

This directory preserves HTTP client/server, NyWeb, socket, WebSocket, IMAP,
and SMTP designs that do not yet have a provider built and installed with the
Ny SDK. There is intentionally no experimental package aggregator: consumers
must not mistake these sources for a coherent opt-in transport library.

The active `ny.lang.net` package contains only deterministic HTTP protocol
values and request/response construction. Promotion of a transport requires a
bounded runtime provider plus textual, binary, and applicable native route
evidence.
