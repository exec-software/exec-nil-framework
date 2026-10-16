@pragma once
module nil.std.net

import nil.std.core
import Ny.Language.Net.Socket
import Ny.Language.Net.HTTP # For HTTP header parsing and response generation
import result
import crypto.sha1 # Assuming SHA-1 is available
import crypto.base64 # Assuming Base64 encoding is available

# --- Callback Types ---
# Define function signatures for WebSocket event callbacks.
# These will be provided by the user of the WebSocket library.

# Called when a text message is received.
# The message content is provided as a Ny text string.
pub trait WebSocketCallbacks
    pub fn on_message(message: text)

    # Called when a binary message is received.
    # The raw data is provided as a vector of bytes.
    pub fn on_binary_message(data: vec<u8>)

    # Called when the connection is closed.
    # Code and reason are standard WebSocket close frame codes/reasons.
    pub fn on_close(code: i32, reason: text)

    # Called when a ping frame is received. The connection should respond with a PONG.
    pub fn on_ping(payload: vec<u8>)

    # Called when a pong frame is received.
    pub fn on_pong(payload: vec<u8>)
end trait


# =============================================================================
# WebSocket Connection State
# =============================================================================

pub struct websocket_connection
    socket: Socket.tcp_connection
    url: text # The URL the connection is established to/from
    is_server: bool # Indicates if this connection is acting as a server
    callbacks: Box<dyn WebSocketCallbacks> # User-provided callbacks for events

    # Internal state for framing
    read_buffer: vec<u8>
    write_buffer: vec<u8>
    next_tag: i64 # For generating unique command tags (e.g., "a001") - perhaps not needed for WS?

    # WebSocket specific state
    is_masked: bool # If true, incoming frames must be masked (server perspective)
    # ... other connection details like fragmentation state, active opcode, etc. ...
end struct

# =============================================================================
# WebSocket Framing Constants
# =============================================================================
const OPCODE_CONTINUATION: u8 = 0x0
const OPCODE_TEXT: u8 = 0x1
const OPCODE_BINARY: u8 = 0x2
const OPCODE_CLOSE: u8 = 0x8
const OPCODE_PING: u8 = 0x9
const OPCODE_PONG: u8 = 0xA

const MASK_BIT: u8 = 0x80

# =============================================================================
# WebSocket Client Functions
# =============================================================================

# Initiates a WebSocket connection to a given URL.
pub fn connect(url: text) -> result<websocket_connection, text>
    # TODO: Implement WebSocket client connection logic
    # 1. Parse URL (scheme, host, port, path)
    # 2. Perform initial HTTP GET request with Upgrade headers
    # 3. If handshake is successful (101 Switching Protocols), establish WS connection
    # 4. Return a configured websocket_connection
    return result.error("Not implemented")
end fn

# =============================================================================
# WebSocket Server Functions
# =============================================================================

# Accepts an existing HTTP connection and attempts to upgrade it to WebSocket.
# If successful, returns a websocket_connection. Otherwise, returns an error.
# This function is called by the HTTP server after a handshake is detected.
pub fn accept_connection(http_req: http_server_request) -> result<websocket_connection, text>
    # 1. Validate the HTTP request headers for WebSocket handshake
    let upgrade_header = http_req.headers.get("Upgrade")
    let connection_header = http_req.headers.get("Connection")
    let ws_key = http_req.headers.get("Sec-WebSocket-Key")
    let ws_version = http_req.headers.get("Sec-WebSocket-Version")

    if core.text.lower(upgrade_header) != "websocket" then
        return result.error("Not a WebSocket upgrade request")
    end if
    # Check if Connection header contains 'Upgrade' (case-insensitive)
    let connection_parts = core.text.split(core.text.lower(connection_header), ", ")
    let mut found_upgrade = false
    for part in connection_parts
        if part == "upgrade" then
            found_upgrade = true
            break
        end if
    end for
    if not found_upgrade then
        return result.error("Connection header must include 'Upgrade'")
    end if

    if ws_key == "" then
        return result.error("Sec-WebSocket-Key header is missing")
    end if
    if ws_version != "13" then # Assuming version 13 as standard
        return result.error("Unsupported WebSocket version: " + ws_version)
    end if

    # 2. Generate Sec-WebSocket-Accept header
    let magic_string = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11" # WebSocket magic string
    let key_hash_input = ws_key + magic_string

    # Use SHA-1 hash and Base64 encode the result.
    # Placeholder for crypto/base64 functions. Assuming they exist in stdlib.
    let sha1_hash_res = crypto.sha1(key_hash_input.to_bytes()) # Assume sha1 works on bytes
    match sha1_hash_res
        result.error(msg) -> return result.error("SHA-1 hashing failed: " + msg)
        result.ok(hash_bytes) ->
            let accept_key_res = base64.encode(hash_bytes)
            match accept_key_res
                result.error(msg) -> return result.error("Base64 encoding failed: " + msg)
                result.ok(accept_key) ->
                    # 3. Send the 101 Switching Protocols response
                    # This is handled by the HTTP server sending the response.
                    # The underlying socket is now considered a WebSocket connection.

                    # Return a new websocket_connection struct wrapping the underlying socket.
                    # The callbacks need to be provided by the user.
                    return result.ok(websocket_connection {
                        socket: http_req.socket, # Assume socket can be passed or reused
                        url: http_req.path,
                        is_server: true,
                        callbacks: null, # User must provide callbacks later
                        read_buffer: vec.new(),
                        write_buffer: vec.new(),
                        next_tag: 1, # Not used for WS, but part of struct
                        is_masked: false # Server does not mask outgoing frames
                    })
            end match
    end match
end fn

# Basic send function for text frames (not handling fragmentation, masking etc. yet)
fn send_text(conn: mut websocket_connection, message: text) -> result<bool, text>
    # TODO: Implement WebSocket framing for text messages.
    # This requires FIN bit, OPCODE (TEXT), MASK bit (0 for server), PAYLOAD LEN.
    # Masking key is generated by server, applied by client. Server frames don't need masking.
    return result.error("Not implemented")
end fn

# Basic send function for binary frames
fn send_binary(conn: mut websocket_connection, data: vec<u8>) -> result<bool, text>
    # TODO: Implement WebSocket framing for binary messages.
    return result.error("Not implemented")
end fn

# Receive a message from the websocket connection.
# Needs to handle fragmented messages and control frames.
fn receive_message(conn: mut websocket_connection) -> result<text, text> # Simplified: returns text message
    # TODO: Implement logic to read from socket, buffer data, parse frames, and reassemble messages.
    # This function should ideally return different types for text, binary, or control frames.
    return result.error("Not implemented")
end fn
