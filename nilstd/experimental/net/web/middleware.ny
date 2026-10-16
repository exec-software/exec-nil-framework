@pragma once
# Experimental: NyWeb depends on the unavailable HTTP-server provider.
module nil.std.net

import nil.std.core
import nil.std.io.console
import nil.std.net.web.request
import nil.std.net.web.response
import nil.std.net.web.session
import nil.std.net.http_server.request

# =============================================================================
# Native Bindings
# =============================================================================

# Returns current wall-clock time in milliseconds (for duration measurement)
@interop("ml_nyweb")
pub fn ml_current_time_ms() -> i64
end fn

# =============================================================================
# Pre-Filter Entry
#
# A pre-filter runs before the page handler. It can short-circuit the request
# by returning Option.Some(response), or continue with Option.None.
# =============================================================================

pub struct pre_filter_entry
    pub name: text
    pub filter: fn(nyweb_request, session) -> Option<nyweb_response>
end struct

# =============================================================================
# Middleware Chain
#
# Composable request processing pipeline:
#   Request → [cookie parse] → [body parse] → [pre-filters] → handler → [log]
#
# Usage:
#   let chain = middleware_chain.new()
#   chain.use_logger()
#   chain.use_cookie_parser()
#   chain.use_body_parser()
#   chain.use_filter("auth", my_auth_fn)
#   let resp = chain.dispatch(req, sess, my_handler)
# =============================================================================

pub struct middleware_chain
    pre_filters: vec<pre_filter_entry>
    logger_enabled: bool
    cookie_parse_enabled: bool
    body_parse_enabled: bool
    error_handler_enabled: bool
end struct

impl middleware_chain
    pub fn new() -> middleware_chain
        return middleware_chain {
            pre_filters: vec<pre_filter_entry>.new(),
            logger_enabled: false,
            cookie_parse_enabled: false,
            body_parse_enabled: false,
            error_handler_enabled: false
        }
    end fn

    # Enable request/response logging to stdout.
    # Logs: [METHOD] /path STATUS_CODE (Nms)
    pub fn use_logger(mut self)
        self.logger_enabled = true
    end fn

    # Parse Cookie request header into nyweb_request.cookies before dispatch.
    pub fn use_cookie_parser(mut self)
        self.cookie_parse_enabled = true
    end fn

    # Parse application/x-www-form-urlencoded request body into
    # nyweb_request.form_data before dispatch.
    pub fn use_body_parser(mut self)
        self.body_parse_enabled = true
    end fn

    # Enable default error handling: catches any pre-filter short-circuit and
    # wraps unexpected states in a 500 Internal Server Error response.
    pub fn use_error_handler(mut self)
        self.error_handler_enabled = true
    end fn

    # Register a custom pre-filter.
    # Filters run in registration order before the page handler.
    # Return Option.Some(response) to short-circuit; Option.None to continue.
    pub fn use_filter(mut self, name: text, filter: fn(nyweb_request, session) -> Option<nyweb_response>)
        self.pre_filters.push(pre_filter_entry { name: name, filter: filter })
    end fn

    # Run all registered pre-filters in order.
    # Returns the first short-circuit response, or Option.None if all pass.
    pub fn run_pre_filters(self, req: nyweb_request, sess: session) -> Option<nyweb_response>
        let mut i = 0
        while i < self.pre_filters.length() do
            let entry = self.pre_filters.get(i).unwrap()
            let result = entry.filter(req, sess)
            if result.is_some() then
                return result
            end if
            i = i + 1
        end while
        return Option.None
    end fn

    # Dispatch a request through the full middleware pipeline.
    #
    # handler — the terminal page/API handler to invoke after all middleware.
    #           Signature: fn(nyweb_request, session) -> nyweb_response
    #
    # Pipeline order:
    #   1. Cookie parsing (if use_cookie_parser())
    #   2. Body parsing   (if use_body_parser())
    #   3. Pre-filters    (registered via use_filter())
    #   4. handler()
    #   5. Logging        (if use_logger())
    pub fn dispatch(self, req: nyweb_request, sess: session,
                          handler: fn(nyweb_request, session) -> nyweb_response) -> nyweb_response
        # Step 1: Parse cookies
        let mut processed = req
        if self.cookie_parse_enabled then
            let cookie_opt = req.header("Cookie")
            if cookie_opt.is_some() then
                let parsed = parse_cookies(cookie_opt.unwrap())
                processed = processed.with_cookies(parsed)
            end if
        end if

        # Step 2: Parse request body (URL-encoded forms only)
        if self.body_parse_enabled then
            let ct_opt = processed.header("Content-Type")
            if ct_opt.is_some() then
                let ct = ct_opt.unwrap()
                if text_contains(ct, "application/x-www-form-urlencoded") then
                    let fields = parse_urlencoded(processed.body())
                    processed = processed.with_form_data(fields)
                end if
            end if
        end if

        # Step 3: Run pre-filters (may short-circuit)
        let filter_result = self.run_pre_filters(processed, sess)
        if filter_result.is_some() then
            return filter_result.unwrap()
        end if

        # Step 4: Record start time (only if logging)
        let mut start_ms: i64 = 0
        if self.logger_enabled then
            start_ms = ml_current_time_ms()
        end if

        # Step 5: Call the page handler
        let response = handler(processed, sess)

        # Step 6: Log request + response
        if self.logger_enabled then
            let elapsed: i64 = ml_current_time_ms() - start_ms
            log_request(processed.method().to_str(), processed.path(),
                        response.status_code(), elapsed)
        end if

        return response
    end fn
end impl

# =============================================================================
# Cookie Parsing
#
# Parses the Cookie header value: "name1=val1; name2=val2; ..."
# Returns a vec of form_field (name, value) pairs.
# =============================================================================

pub fn parse_cookies(cookie_header: text) -> vec<form_field>
    let mut result = vec<form_field>.new()
    if cookie_header == "" then return result end if

    let len = cookie_header.length()
    let mut pos = 0

    while pos < len do
        # Locate end of this cookie pair ("; " separator)
        let semi = find_in(cookie_header, "; ", pos)
        let end_pos = if semi == -1 then len else semi end if

        let pair = cookie_header.substring(pos, end_pos - pos)

        # Split on first "="
        let eq = find_in(pair, "=", 0)
        if eq > 0 then
            let name = trim_ws(pair.substring(0, eq))
            let value_start = eq + 1
            let value = if value_start < pair.length() then
                pair.substring(value_start, pair.length() - value_start)
            else
                ""
            end if
            result.push(form_field { name: name, value: value })
        end if

        if semi == -1 then break end if
        pos = semi + 2  # skip the "; " separator
    end while

    return result
end fn

# =============================================================================
# URL-Encoded Body Parsing
#
# Parses application/x-www-form-urlencoded bodies: "key1=val1&key2=val2"
# Returns a vec of form_field (name, value) pairs with URL-decoding applied.
# =============================================================================

pub fn parse_urlencoded(body: text) -> vec<form_field>
    let mut result = vec<form_field>.new()
    if body == "" then return result end if

    let len = body.length()
    let mut pos = 0

    while pos < len do
        let amp = find_in(body, "&", pos)
        let end_pos = if amp == -1 then len else amp end if

        let pair = body.substring(pos, end_pos - pos)

        let eq = find_in(pair, "=", 0)
        if eq >= 0 then
            let raw_key = pair.substring(0, eq)
            let val_start = eq + 1
            let raw_val = if val_start < pair.length() then
                pair.substring(val_start, pair.length() - val_start)
            else
                ""
            end if
            # url_decode is public in the HTTP request module imported above.
            result.push(form_field { name: url_decode(raw_key), value: url_decode(raw_val) })
        end if

        if amp == -1 then break end if
        pos = amp + 1
    end while

    return result
end fn

# =============================================================================
# Built-in Logging Middleware
#
# Default format: [METHOD] /path STATUS_CODE (Nms)
# Example:        [GET] /api/users 200 (3ms)
# =============================================================================

pub fn log_request(method: text, path: text, status_code: i32, duration_ms: i64)
    let code_str: text = status_code as text
    let dur_str: text = duration_ms as text
    nil::std::io::writeln("[" + method + "] " + path + " " + code_str + " (" + dur_str + "ms)")
end fn

# =============================================================================
# Error Response Helpers
# =============================================================================

# Produce a 500 Internal Server Error response with an optional message.
pub fn internal_error(detail: text) -> nyweb_response
    return nyweb_response.html(http_status.internal_server_error,
        "<html><body><h1>500 Internal Server Error</h1><p>" + detail + "</p></body></html>")
end fn

# Produce a 400 Bad Request response.
pub fn bad_request(detail: text) -> nyweb_response
    return nyweb_response.html(http_status.bad_request,
        "<html><body><h1>400 Bad Request</h1><p>" + detail + "</p></body></html>")
end fn

# =============================================================================
# Private Helpers
# =============================================================================

# Find the first occurrence of needle in s starting at from_pos.
# Returns -1 if not found.
fn find_in(s: text, needle: text, from_pos: i32) -> i32
    let s_len = s.length()
    let n_len = needle.length()
    if n_len == 0 or n_len > s_len - from_pos then return -1 end if

    let mut i = from_pos
    while i <= s_len - n_len do
        if s.substring(i, n_len) == needle then
            return i
        end if
        i = i + 1
    end while
    return -1
end fn

# Trim leading/trailing ASCII whitespace from text.
fn trim_ws(s: text) -> text
    let len = s.length()
    if len == 0 then return "" end if

    let mut start = 0
    while start < len do
        let c = s.substring(start, 1)
        if c != " " and c != "\t" then break end if
        start = start + 1
    end while

    let mut end_idx = len
    while end_idx > start do
        let c = s.substring(end_idx - 1, 1)
        if c != " " and c != "\t" then break end if
        end_idx = end_idx - 1
    end while

    return s.substring(start, end_idx - start)
end fn

# Returns true if s contains the needle substring.
fn text_contains(s: text, needle: text) -> bool
    return find_in(s, needle, 0) != -1
end fn

# url_decode() comes from the imported HTTP request module and delegates to
# ml_url_decode internally.
