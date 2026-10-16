@pragma once

# Experimental: http.send has no interpreter or native dispatcher in the
# current toolchain. This transport design is excluded from active nilstd.

module nil.std.net
import nil.std.core

# =============================================================================
# HTTP Types
# =============================================================================

# HTTP request method
pub enum http_method
    get
    post
    put
    delete
    patch
    head
    options
end enum

impl http_method
    # Parse an HTTP method string (case-insensitive) into the enum variant.
    # Returns http_method.get as the default for unknown methods.
    pub fn from_str(s: text) -> http_method
        if s == "GET" or s == "get" then
            return http_method.get
        end if
        if s == "POST" or s == "post" then
            return http_method.post
        end if
        if s == "PUT" or s == "put" then
            return http_method.put
        end if
        if s == "DELETE" or s == "delete" then
            return http_method.delete
        end if
        if s == "PATCH" or s == "patch" then
            return http_method.patch
        end if
        if s == "HEAD" or s == "head" then
            return http_method.head
        end if
        if s == "OPTIONS" or s == "options" then
            return http_method.options
        end if
        return http_method.get
    end fn

    pub fn to_str(self) -> text
        match self
            http_method.get -> return "GET"
            http_method.post -> return "POST"
            http_method.put -> return "PUT"
            http_method.delete -> return "DELETE"
            http_method.patch -> return "PATCH"
            http_method.head -> return "HEAD"
            http_method.options -> return "OPTIONS"
        end match
    end fn
end impl

# HTTP status codes
pub enum http_status
    ok                      # 200
    created                 # 201
    accepted                # 202
    no_content              # 204
    moved_permanently       # 301
    found                   # 302
    not_modified            # 304
    bad_request             # 400
    unauthorized            # 401
    forbidden               # 403
    not_found               # 404
    method_not_allowed      # 405
    internal_server_error   # 500
    bad_gateway             # 502
    service_unavailable     # 503
end enum

impl http_status
    # Get numeric status code
    pub fn code(self) -> i32
        match self
            http_status.ok -> return 200
            http_status.created -> return 201
            http_status.accepted -> return 202
            http_status.no_content -> return 204
            http_status.moved_permanently -> return 301
            http_status.found -> return 302
            http_status.not_modified -> return 304
            http_status.bad_request -> return 400
            http_status.unauthorized -> return 401
            http_status.forbidden -> return 403
            http_status.not_found -> return 404
            http_status.method_not_allowed -> return 405
            http_status.internal_server_error -> return 500
            http_status.bad_gateway -> return 502
            http_status.service_unavailable -> return 503
        end match
    end fn

    # Check if status is success (2xx)
    pub fn is_success(self) -> bool
        let c: i32 = self.code()
        return c >= 200 and c < 300
    end fn

    # Check if status is redirect (3xx)
    pub fn is_redirect(self) -> bool
        let c: i32 = self.code()
        return c >= 300 and c < 400
    end fn

    # Check if status is client error (4xx)
    pub fn is_client_error(self) -> bool
        let c: i32 = self.code()
        return c >= 400 and c < 500
    end fn

    # Check if status is server error (5xx)
    pub fn is_server_error(self) -> bool
        let c: i32 = self.code()
        return c >= 500 and c < 600
    end fn
end impl

# =============================================================================
# HTTP Header
# =============================================================================

pub struct http_header
    name: text
    value: text
end struct

# =============================================================================
# HTTP Request
# =============================================================================

pub struct http_request
    method: http_method
    url: text
    headers: vec<http_header>
    body: Option<text>
end struct

impl http_request
    # Create a GET request
    pub fn get(url: text) -> http_request
        return http_request {
            method: http_method.get,
            url: url,
            headers: vec.new(),
            body: Option.None
        }
    end fn

    # Create a POST request
    pub fn post(url: text, body: text) -> http_request
        return http_request {
            method: http_method.post,
            url: url,
            headers: vec.new(),
            body: Option.Some(body)
        }
    end fn

    # Add a header
    pub fn with_header(mut self, name: text, value: text) -> http_request
        self.headers.push(http_header { name: name, value: value })
        return self
    end fn

    # Set content type
    pub fn with_content_type(self, content_type: text) -> http_request
        return self.with_header("Content-Type", content_type)
    end fn

    # Set authorization header
    pub fn with_auth(self, token: text) -> http_request
        return self.with_header("Authorization", "Bearer " + token)
    end fn
end impl

# =============================================================================
# HTTP Response
# =============================================================================

pub struct http_response
    status: http_status
    headers: vec<http_header>
    body: text
end struct

impl http_response
    # Check if response was successful
    pub fn is_ok(self) -> bool
        return self.status.is_success()
    end fn

    # Get a header value by name
    pub fn get_header(self, name: text) -> Option<text>
        for header in self.headers
            if header.name == name then
                return Option.Some(header.value)
            end if
        end for
        return Option.None
    end fn
end impl

# =============================================================================
# HTTP Client
# =============================================================================

pub struct http_client
    base_url: Option<text>
    timeout_ms: i32
    default_headers: vec<http_header>
end struct

impl http_client
    # Create a new HTTP client
    pub fn new() -> http_client
        return http_client {
            base_url: Option.None,
            timeout_ms: 30000,
            default_headers: vec.new()
        }
    end fn

    # Create client with base URL
    pub fn with_base_url(url: text) -> http_client
        return http_client {
            base_url: Option.Some(url),
            timeout_ms: 30000,
            default_headers: vec.new()
        }
    end fn

    # Set timeout
    pub fn set_timeout(mut self, ms: i32)
        self.timeout_ms = ms
    end fn

    # Add default header
    pub fn add_default_header(mut self, name: text, value: text)
        self.default_headers.push(http_header { name: name, value: value })
    end fn

    # Send a request
    @intrinsic("http.send")
    pub fn send(self, request: http_request) -> Result<http_response, text>

    # GET request
    pub fn get(self, url: text) -> Result<http_response, text>
        let request: http_request = http_request.get(url)
        return self.send(request)
    end fn

    # POST request
    pub fn post(self, url: text, body: text) -> Result<http_response, text>
        let request: http_request = http_request.post(url, body)
        return self.send(request)
    end fn
end impl
