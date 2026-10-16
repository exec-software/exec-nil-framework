@pragma once

module nil.std.net

import nil.std.core
import nil.std.collections

fn ascii_case_equal(left: text, right: text) -> bool
    if left.len() != right.len() then return false end if
    let upper: text = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    let lower: text = "abcdefghijklmnopqrstuvwxyz"
    let mut i: i32 = 0
    while i < left.len()
        let left_char: text = left.substr(i, 1)
        let right_char: text = right.substr(i, 1)
        if left_char != right_char then
            let mut matched: bool = false
            let mut letter: i32 = 0
            while letter < 26
                let upper_char: text = upper.substr(letter, 1)
                let lower_char: text = lower.substr(letter, 1)
                if (left_char == upper_char or left_char == lower_char) and
                   (right_char == upper_char or right_char == lower_char) then
                    matched = true
                    letter = 26
                else
                    letter = letter + 1
                end if
            end while
            if not matched then return false end if
        end if
        i = i + 1
    end while
    return true
end fn

# Transport-independent HTTP protocol values. Network I/O is intentionally
# absent from this active module until an HTTP provider is built and tested.
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
    pub fn parse(value: text) -> Option<http_method>
        return parse_http_method_impl(value)
    end fn

    pub fn to_text(self) -> text
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

fn parse_http_method_impl(value: text) -> Option<http_method>
    if ascii_case_equal(value, "GET") then return Option.Some(http_method.get) end if
    if ascii_case_equal(value, "POST") then return Option.Some(http_method.post) end if
    if ascii_case_equal(value, "PUT") then return Option.Some(http_method.put) end if
    if ascii_case_equal(value, "DELETE") then return Option.Some(http_method.delete) end if
    if ascii_case_equal(value, "PATCH") then return Option.Some(http_method.patch) end if
    if ascii_case_equal(value, "HEAD") then return Option.Some(http_method.head) end if
    if ascii_case_equal(value, "OPTIONS") then return Option.Some(http_method.options) end if
    return Option.None
end fn

pub enum http_status
    ok
    created
    accepted
    no_content
    moved_permanently
    found
    not_modified
    bad_request
    unauthorized
    forbidden
    not_found
    method_not_allowed
    internal_server_error
    bad_gateway
    service_unavailable
end enum

impl http_status
    pub fn from_code(code: i32) -> Option<http_status>
        return http_status_from_code_impl(code)
    end fn

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

    pub fn is_success(self) -> bool
        let value: i32 = self.code()
        return value >= 200 and value < 300
    end fn

    pub fn is_redirect(self) -> bool
        let value: i32 = self.code()
        return value >= 300 and value < 400
    end fn

    pub fn is_client_error(self) -> bool
        let value: i32 = self.code()
        return value >= 400 and value < 500
    end fn

    pub fn is_server_error(self) -> bool
        let value: i32 = self.code()
        return value >= 500 and value < 600
    end fn
end impl

fn http_status_from_code_impl(code: i32) -> Option<http_status>
    if code == 200 then return Option.Some(http_status.ok) end if
    if code == 201 then return Option.Some(http_status.created) end if
    if code == 202 then return Option.Some(http_status.accepted) end if
    if code == 204 then return Option.Some(http_status.no_content) end if
    if code == 301 then return Option.Some(http_status.moved_permanently) end if
    if code == 302 then return Option.Some(http_status.found) end if
    if code == 304 then return Option.Some(http_status.not_modified) end if
    if code == 400 then return Option.Some(http_status.bad_request) end if
    if code == 401 then return Option.Some(http_status.unauthorized) end if
    if code == 403 then return Option.Some(http_status.forbidden) end if
    if code == 404 then return Option.Some(http_status.not_found) end if
    if code == 405 then return Option.Some(http_status.method_not_allowed) end if
    if code == 500 then return Option.Some(http_status.internal_server_error) end if
    if code == 502 then return Option.Some(http_status.bad_gateway) end if
    if code == 503 then return Option.Some(http_status.service_unavailable) end if
    return Option.None
end fn

pub struct http_header
    pub name: text
    pub value: text
end struct

impl http_header
    pub fn new(name: text, value: text) -> http_header
        return http_header { name: name, value: value }
    end fn

    pub fn name_equals(self, name: text) -> bool
        return ascii_case_equal(self.name, name)
    end fn
end impl

pub struct http_request
    _method: http_method
    _url: text
    _headers: vec<http_header>
    _body: Option<text>
end struct

impl http_request
    pub fn new(method: http_method, url: text) -> http_request
        return http_request {
            _method: method,
            _url: url,
            _headers: vec<http_header>.new(),
            _body: Option.None
        }
    end fn

    pub fn get(url: text) -> http_request
        return http_request.new(http_method.get, url)
    end fn

    pub fn post(url: text, body: text) -> http_request
        let mut request: http_request = http_request.new(http_method.post, url)
        request._body = Option.Some(body)
        return request
    end fn

    pub fn with_header(mut self, name: text, value: text) -> http_request
        let mut i: i32 = 0
        while i < self._headers.length()
            let current: http_header = self._headers.get(i).unwrap()
            if current.name_equals(name) then
                self._headers.set(i, http_header.new(name, value))
                return self
            end if
            i = i + 1
        end while
        self._headers.push(http_header.new(name, value))
        return self
    end fn

    pub fn with_content_type(self, content_type: text) -> http_request
        return self.with_header("Content-Type", content_type)
    end fn

    pub fn with_bearer_token(self, token: text) -> http_request
        return self.with_header("Authorization", "Bearer " + token)
    end fn

    pub fn method(self) -> http_method
        return self._method
    end fn

    pub fn url(self) -> text
        return self._url
    end fn

    pub fn body(self) -> Option<text>
        return self._body
    end fn

    pub fn header(self, name: text) -> Option<text>
        let mut i: i32 = 0
        while i < self._headers.length()
            let current: http_header = self._headers.get(i).unwrap()
            if current.name_equals(name) then
                return Option.Some(current.value)
            end if
            i = i + 1
        end while
        return Option.None
    end fn
end impl

pub struct http_response
    _status: http_status
    _headers: vec<http_header>
    _body: text
end struct

impl http_response
    pub fn new(status: http_status, body: text) -> http_response
        return http_response {
            _status: status,
            _headers: vec<http_header>.new(),
            _body: body
        }
    end fn

    pub fn with_header(mut self, name: text, value: text) -> http_response
        let mut i: i32 = 0
        while i < self._headers.length()
            let current: http_header = self._headers.get(i).unwrap()
            if current.name_equals(name) then
                self._headers.set(i, http_header.new(name, value))
                return self
            end if
            i = i + 1
        end while
        self._headers.push(http_header.new(name, value))
        return self
    end fn

    pub fn status(self) -> http_status
        return self._status
    end fn

    pub fn body(self) -> text
        return self._body
    end fn

    pub fn is_ok(self) -> bool
        return self._status.is_success()
    end fn

    pub fn header(self, name: text) -> Option<text>
        let mut i: i32 = 0
        while i < self._headers.length()
            let current: http_header = self._headers.get(i).unwrap()
            if current.name_equals(name) then
                return Option.Some(current.value)
            end if
            i = i + 1
        end while
        return Option.None
    end fn
end impl
