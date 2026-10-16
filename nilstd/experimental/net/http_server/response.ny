@pragma once
# Experimental: the ml_nyhttp provider is not linked into the SDK.
module nil.std.net

import nil.std.core

pub struct http_server_response
    pub status: http_status
    pub headers: vec<http_header>
    pub body: text
end struct

impl http_server_response
    pub fn new(status: http_status, body: text) -> http_server_response
        let empty_headers = vec<http_header>.new()
        return http_server_response {
            status: status,
            headers: empty_headers,
            body: body
        }
    end fn

    pub fn ok(body: text) -> http_server_response
        return http_server_response.new(http_status.ok, body)
    end fn

    pub fn ok_html(body: text) -> http_server_response
        return http_server_response.new(http_status.ok, body)
    end fn

    pub fn bad_request(msg: text) -> http_server_response
        return http_server_response.new(http_status.bad_request, msg)
    end fn

    pub fn not_found() -> http_server_response
        return http_server_response.new(http_status.not_found, "Not Found")
    end fn

    pub fn unauthorized() -> http_server_response
        return http_server_response.new(http_status.unauthorized, "Unauthorized")
    end fn

    pub fn redirect(location: text) -> http_server_response
        let mut resp = http_server_response.new(http_status.found, "")
        resp.set_header("Location", location)
        return resp
    end fn

    pub fn set_header(mut self, name: text, value: text)
        let mut i = 0
        while i < self.headers.length() do
            let header = self.headers.get(i).unwrap()
            if header.name == name then
                self.headers.set(i, http_header { name: name, value: value })
                return
            end if
            i = i + 1
        end while
        self.headers.push(http_header { name: name, value: value })
    end fn

    pub fn send(self, req_handle: i64)
        ml_http_send_response(
            req_handle,
            self.status.code(),
            "",
            self.body
        )
    end fn
end impl

@interop("ml_nyhttp")
pub fn ml_http_send_response(req_handle: i64, status: i32, headers: text, body: text)
end fn
