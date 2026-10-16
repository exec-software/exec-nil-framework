@pragma once
# Experimental: NyWeb depends on the unavailable HTTP-server provider.
module nil.std.net

import nil.std.core
import nil.std.net.http_server.request
import nil.std.net.http_server.context

# =============================================================================
# NyWeb Request
# Thin wrapper around http_server_request that adds NyWeb-specific
# conveniences: form model binding, file uploads, session access token.
# =============================================================================

pub struct nyweb_request
    inner: http_server_request
    session_token: Option<text>
    form_data: vec<form_field>
    cookies: vec<form_field>
    files: vec<uploaded_file>
end struct

# A parsed form field (application/x-www-form-urlencoded or multipart)
pub struct form_field
    name: text
    value: text
end struct

# An uploaded file from a multipart form
pub struct uploaded_file
    field_name: text
    file_name: text
    content_type: text
    size: i64
    # Content is kept in server temp storage; access via the path
    temp_path: text
end struct

impl nyweb_request
    pub fn from_context(ctx: http_context) -> nyweb_request
        let session_token = ctx.request.get_header("Cookie")
        return nyweb_request {
            inner: ctx.request,
            session_token: session_token,
            form_data: vec.new(),
            cookies: vec.new(),
            files: vec.new()
        }
    end fn

    # Create a new request with parsed cookies attached
    pub fn with_cookies(self, parsed_cookies: vec<form_field>) -> nyweb_request
        return nyweb_request {
            inner: self.inner,
            session_token: self.session_token,
            form_data: self.form_data,
            cookies: parsed_cookies,
            files: self.files
        }
    end fn

    # Create a new request with parsed form data attached
    pub fn with_form_data(self, data: vec<form_field>) -> nyweb_request
        return nyweb_request {
            inner: self.inner,
            session_token: self.session_token,
            form_data: data,
            cookies: self.cookies,
            files: self.files
        }
    end fn

    # Get method
    pub fn method(self) -> http_method
        return self.inner.get_method()
    end fn

    # Get request path
    pub fn path(self) -> text
        return self.inner.get_path()
    end fn

    # Get a named route parameter (e.g. :id from /users/:id)
    pub fn param(self, name: text) -> Option<text>
        return self.inner.get_route_param(name)

    end fn

    # Get a query string parameter
    pub fn query(self, key: text) -> Option<text>
        return self.inner.get_query(key)
    end fn

    # Get a request header
    pub fn header(self, name: text) -> Option<text>
        return self.inner.get_header(name)
    end fn

    # Get the raw body
    pub fn body(self) -> text
        return self.inner.get_body()
    end fn

    # Get a form field by name
    pub fn form(self, name: text) -> Option<text>
        for field in self.form_data
            if field.name == name then
                return Option.Some(field.value)
            end if
        end for
        return Option.None
    end fn

    # Get a parsed cookie by name
    pub fn cookie(self, name: text) -> Option<text>
        let mut i = 0
        while i < self.cookies.length() do
            let c = self.cookies.get(i).unwrap()
            if c.name == name then
                return Option.Some(c.value)
            end if
            i = i + 1
        end while
        return Option.None
    end fn

    # Get an uploaded file by field name
    pub fn file(self, field_name: text) -> Option<uploaded_file>
        for f in self.files
            if f.field_name == field_name then
                return Option.Some(f)
            end if
        end for
        return Option.None
    end fn

    # Whether this is a GET request
    pub fn is_get(self) -> bool
        match self.inner.get_method()
            http_method.get -> return true
            _ -> return false
        end match
    end fn

    # Whether this is a POST request
    pub fn is_post(self) -> bool
        match self.inner.get_method()
            http_method.post -> return true
            _ -> return false
        end match
    end fn
end impl
