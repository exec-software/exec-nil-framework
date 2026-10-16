@pragma once
# Experimental: the ml_nyhttp provider is not linked into the SDK.
module nil.std.net

import nil.std.core

# Named pair for query/route params (avoids nested generic tuple<text,text>)
pub struct http_param
    pub key: text
    pub value: text
end struct

pub struct http_server_request
    pub method: http_method
    pub path: text
    pub query_params: vec<http_param>
    pub headers: vec<http_header>
    pub route_params: vec<http_param>
    pub body: text
    pub remote_addr: text
    pub version: text
end struct

impl http_server_request
    # Stable accessors keep callers independent from record layout and allow
    # request values to be consumed before their declaration unit is visited.
    pub fn get_method(self) -> http_method
        return self.method
    end fn

    pub fn get_path(self) -> text
        return self.path
    end fn

    pub fn get_body(self) -> text
        return self.body
    end fn

    pub fn get_query(self, key: text) -> Option<text>
        let mut i = 0
        while i < self.query_params.length() do
            let param = self.query_params.get(i).unwrap()
            if param.key == key then
                return Option.Some(param.value)
            end if
            i = i + 1
        end while
        return Option.None
    end fn

    pub fn get_header(self, name: text) -> Option<text>
        let mut i = 0
        while i < self.headers.length() do
            let header = self.headers.get(i).unwrap()
            if header.name == name then
                return Option.Some(header.value)
            end if
            i = i + 1
        end while
        return Option.None
    end fn

    pub fn get_route_param(self, name: text) -> Option<text>
        let mut i = 0
        while i < self.route_params.length() do
            let param = self.route_params.get(i).unwrap()
            if param.key == name then
                return Option.Some(param.value)
            end if
            i = i + 1
        end while
        return Option.None
    end fn
end impl

pub fn assemble_request(req_handle: i64, remote: text) -> Result<http_server_request, text>
    let method_code = ml_http_request_method(req_handle)
    let method = method_from_code(method_code)
    let path = ml_http_request_path(req_handle)
    let query_str = ml_http_request_query_string(req_handle)
    let query_params = parse_query_string(query_str)
    let header_count = ml_http_request_header_count(req_handle)
    let mut headers = vec<http_header>.new()
    let mut i = 0
    while i < header_count do
        let name = ml_http_request_header_name(req_handle, i)
        let value = ml_http_request_header_value(req_handle, i)
        headers = headers + [http_header { name: name, value: value }]
        i = i + 1
    end while
    let body = ml_http_request_body(req_handle)
    let version = ml_http_request_version(req_handle)
    let empty_route = vec<http_param>.new()
    let req = http_server_request {
        method: method,
        path: path,
        query_params: query_params,
        headers: headers,
        route_params: empty_route,
        body: body,
        remote_addr: remote,
        version: version
    }
    return Result.Ok(req)
end fn

pub fn method_from_code(code: i32) -> http_method
    if code == 0 then return http_method.get end if
    if code == 1 then return http_method.post end if
    if code == 2 then return http_method.put end if
    if code == 3 then return http_method.delete end if
    if code == 4 then return http_method.patch end if
    if code == 5 then return http_method.head end if
    if code == 6 then return http_method.options end if
    return http_method.get
end fn

pub fn parse_query_string(qs: text) -> vec<http_param>
    let mut params = vec<http_param>.new()
    if qs == "" then return params end if
    let mut pos = 0
    while pos < qs.length() do
        let amp = qs.find("&", pos)
        let end_pos = if amp == -1 then qs.length() else amp end if
        let pair = qs.substring(pos, end_pos - pos)
        let eq = pair.find("=", 0)
        if eq > 0 then
            let key = url_decode(pair.substring(0, eq))
            let val = url_decode(pair.substring(eq + 1))
            params = params + [http_param { key: key, value: val }]
        end if
        if amp == -1 then break end if
        pos = amp + 1
    end while
    return params
end fn

pub fn url_decode(s: text) -> text
    return ml_url_decode(s)
end fn

@interop("ml_nyhttp")
pub fn ml_http_request_method(req_handle: i64) -> i32
end fn

@interop("ml_nyhttp")
pub fn ml_http_request_path(req_handle: i64) -> text
end fn

@interop("ml_nyhttp")
pub fn ml_http_request_query_string(req_handle: i64) -> text
end fn

@interop("ml_nyhttp")
pub fn ml_http_request_body(req_handle: i64) -> text
end fn

@interop("ml_nyhttp")
pub fn ml_http_request_header_count(req_handle: i64) -> i32
end fn

@interop("ml_nyhttp")
pub fn ml_http_request_header_name(req_handle: i64, index: i32) -> text
end fn

@interop("ml_nyhttp")
pub fn ml_http_request_header_value(req_handle: i64, index: i32) -> text
end fn

@interop("ml_nyhttp")
pub fn ml_http_request_version(req_handle: i64) -> text
end fn

@interop("ml_nyhttp")
pub fn ml_url_decode(encoded: text) -> text
end fn
