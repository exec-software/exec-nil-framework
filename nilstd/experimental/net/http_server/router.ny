@pragma once
# Experimental: the ml_nyhttp provider is not linked into the SDK.
module nil.std.net

import nil.std.core
import nil.std.net.http_server.request
import nil.std.net.http_server.response
import nil.std.net.http_server.context

pub struct route_match
    pub handler: fn(http_context) -> http_server_response
end struct

impl route_match
    pub fn invoke(self, context: http_context) -> http_server_response
        return self.handler(context)
    end fn
end impl

pub struct route_entry
    pub method: http_method
    pub path: text
    pub handler: fn(http_context) -> http_server_response
end struct

pub struct http_router
    pub routes: vec<route_entry>
    pub not_found_handler: Option<fn(http_context) -> http_server_response>
    pub router_handle: i64
end struct

impl http_router
    pub fn new() -> http_router
        return http_router {
            routes: vec<route_entry>.new(),
            not_found_handler: Option.None,
            router_handle: -1
        }
    end fn

    pub fn get(mut self, path: text, handler: fn(http_context) -> http_server_response)
        self.routes.push(route_entry { method: http_method.get, path: path, handler: handler })
    end fn

    pub fn post(mut self, path: text, handler: fn(http_context) -> http_server_response)
        self.routes.push(route_entry { method: http_method.post, path: path, handler: handler })
    end fn

    pub fn put(mut self, path: text, handler: fn(http_context) -> http_server_response)
        self.routes.push(route_entry { method: http_method.put, path: path, handler: handler })
    end fn

    pub fn delete(mut self, path: text, handler: fn(http_context) -> http_server_response)
        self.routes.push(route_entry { method: http_method.delete, path: path, handler: handler })
    end fn

    pub fn patch(mut self, path: text, handler: fn(http_context) -> http_server_response)
        self.routes.push(route_entry { method: http_method.patch, path: path, handler: handler })
    end fn

    pub fn head(mut self, path: text, handler: fn(http_context) -> http_server_response)
        self.routes.push(route_entry { method: http_method.head, path: path, handler: handler })
    end fn

    pub fn options(mut self, path: text, handler: fn(http_context) -> http_server_response)
        self.routes.push(route_entry { method: http_method.options, path: path, handler: handler })
    end fn

    # Register a route for any HTTP method
    pub fn any(mut self, path: text, handler: fn(http_context) -> http_server_response)
        # Register for all methods
        self.routes.push(route_entry { method: http_method.get, path: path, handler: handler })
        self.routes.push(route_entry { method: http_method.post, path: path, handler: handler })
        self.routes.push(route_entry { method: http_method.put, path: path, handler: handler })
        self.routes.push(route_entry { method: http_method.delete, path: path, handler: handler })
        self.routes.push(route_entry { method: http_method.patch, path: path, handler: handler })
    end fn

    pub fn resolve(self, method: http_method, path: text) -> Option<route_match>
        # Strip query string for matching
        let clean_path = strip_query(path)
        let mut i = 0
        while i < self.routes.length() do
        let entry = self.routes.get(i).unwrap()
        if entry.method == method then
            if entry.path == clean_path then
                return Option.Some(route_match { handler: entry.handler })
            end if
            # Wildcard prefix: "/*" matches anything
            if entry.path == "/*" then
                return Option.Some(route_match { handler: entry.handler })
            end if
        end if
        i = i + 1
        end while
        return Option.None
    end fn
end impl

# Strip query string from a path (e.g. "/foo?bar=1" -> "/foo")
pub fn strip_query(path: text) -> text
    let q = path.find("?", 0)
    if q == -1 then return path end if
    return path.substring(0, q)
end fn

# Split a path string by delimiter
pub fn split_by(s: text, delim: text) -> vec<text>
    let mut result = vec<text>.new()
    if s == "" then return result end if

    let mut pos = 0
    while pos < s.length() do
        let delim_pos = s.find(delim, pos)
        if delim_pos == -1 then
            result.push(s.substring(pos))
            break
        end if
        result.push(s.substring(pos, delim_pos - pos))
        pos = delim_pos + 1
    end while

    return result
end fn
