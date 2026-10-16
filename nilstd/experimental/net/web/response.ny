@pragma once
# Experimental: NyWeb depends on the unavailable HTTP-server provider.
module nil.std.net

import nil.std.core
import nil.std.net.http_server.response

# =============================================================================
# NyWeb Response
# Higher-level response types returned by page controllers.
# The NyWeb runtime converts these to lower-level HTTP server responses.
# =============================================================================

pub enum nyweb_response_kind
    rendered_html       # A rendered .nyhtml template
    redirect_to         # Client-side redirect
    json_data           # JSON API response
    static_file         # Serve a file from disk
    status_only         # Status code with no body (e.g. 204)
    raw                 # Pass-through to the underlying HTTP response
end enum

pub struct nyweb_response
    kind: nyweb_response_kind
    status: http_status
    body: text
    headers: vec<http_header>
    redirect_url: Option<text>
    file_path: Option<text>
end struct

impl nyweb_response
    # Render: return a rendered HTML page
    pub fn html(status: http_status, html_body: text) -> nyweb_response
        return nyweb_response {
            kind: nyweb_response_kind.rendered_html,
            status: status,
            body: html_body,
            headers: vec.new(),
            redirect_url: Option.None,
            file_path: Option.None
        }
    end fn

    # 200 OK rendered HTML
    pub fn ok(html_body: text) -> nyweb_response
        return nyweb_response.html(http_status.ok, html_body)
    end fn

    # JSON API response
    pub fn json(data: text) -> nyweb_response
        let mut r = nyweb_response {
            kind: nyweb_response_kind.json_data,
            status: http_status.ok,
            body: data,
            headers: vec.new(),
            redirect_url: Option.None,
            file_path: Option.None
        }
        r.headers.push(http_header { name: "Content-Type", value: "application/json" })
        return r
    end fn

    # JSON with explicit status
    pub fn json_status(status: http_status, data: text) -> nyweb_response
        let mut r = nyweb_response.json(data)
        r.status = status
        return r
    end fn

    # Client redirect (302)
    pub fn redirect(url: text) -> nyweb_response
        return nyweb_response {
            kind: nyweb_response_kind.redirect_to,
            status: http_status.found,
            body: "",
            headers: vec.new(),
            redirect_url: Option.Some(url),
            file_path: Option.None
        }
    end fn

    # Permanent redirect (301)
    pub fn redirect_permanent(url: text) -> nyweb_response
        return nyweb_response {
            kind: nyweb_response_kind.redirect_to,
            status: http_status.moved_permanently,
            body: "",
            headers: vec.new(),
            redirect_url: Option.Some(url),
            file_path: Option.None
        }
    end fn

    # Serve a file from disk
    pub fn file(path: text) -> nyweb_response
        return nyweb_response {
            kind: nyweb_response_kind.static_file,
            status: http_status.ok,
            body: "",
            headers: vec.new(),
            redirect_url: Option.None,
            file_path: Option.Some(path)
        }
    end fn

    # 404 Not Found
    pub fn not_found() -> nyweb_response
        return nyweb_response.html(http_status.not_found, "<h1>404 Not Found</h1>")
    end fn

    # 401 Unauthorized — redirect to login
    pub fn unauthorized(login_url: text) -> nyweb_response
        return nyweb_response.redirect(login_url)
    end fn

    # 204 No Content (for API endpoints)
    pub fn no_content() -> nyweb_response
        return nyweb_response {
            kind: nyweb_response_kind.status_only,
            status: http_status.no_content,
            body: "",
            headers: vec.new(),
            redirect_url: Option.None,
            file_path: Option.None
        }
    end fn

    # Get the numeric HTTP status code (e.g. 200, 404, 500)
    pub fn status_code(self) -> i32
        return self.status.code()
    end fn

    # Add a header to the response
    pub fn with_header(mut self, name: text, value: text) -> nyweb_response
        self.headers.push(http_header { name: name, value: value })
        return self
    end fn

    # Convert to an HTTP server response for wire transmission
    pub fn to_http_response(self) -> http_server_response
        let mut r = http_server_response.new(self.status, self.body)
        for h in self.headers
            r.set_header(h.name, h.value)
        end for
        match self.redirect_url
            Option.Some(url) -> r.set_header("Location", url)
            Option.None -> {}
        end match
        return r
    end fn
end impl
