@pragma once
# Experimental: NyWeb depends on the unavailable HTTP-server provider.
module nil.std.net

import nil.std.core
import nil.std.net.web.request
import nil.std.net.web.response
import nil.std.net.web.session
import nil.std.net.web.template
import nil.std.net.web.page

# =============================================================================
# Page Dispatcher
#
# Maps URL paths to Page class instances and dispatches HTTP requests.
# Supports both convention-based routing (pages/*.ny) and explicit registration.
# =============================================================================

pub struct page_dispatcher
    pages: vec<page_entry>
    not_found_handler: Option<fn(nyweb_request, session) -> nyweb_response>
end struct

pub struct page_entry
    pub path: text
    pub method: http_method
    pub handler: fn(nyweb_request, session) -> nyweb_response
end struct

impl page_dispatcher
    pub fn new() -> page_dispatcher
        return page_dispatcher {
            pages: vec<page_entry>.new(),
            not_found_handler: Option.None
        }
    end fn

    # Register a page handler for a specific path and method
    pub fn register(mut self, path: text, method: http_method, handler: fn(nyweb_request, session) -> nyweb_response)
        self.pages.push(page_entry { path: path, method: method, handler: handler })
    end fn

    # Register a page handler for GET requests
    pub fn get(mut self, path: text, handler: fn(nyweb_request, session) -> nyweb_response)
        self.register(path, http_method.get, handler)
    end fn

    # Register a page handler for POST requests
    pub fn post(mut self, path: text, handler: fn(nyweb_request, session) -> nyweb_response)
        self.register(path, http_method.post, handler)
    end fn

    # Register a page handler for PUT requests
    pub fn put(mut self, path: text, handler: fn(nyweb_request, session) -> nyweb_response)
        self.register(path, http_method.put, handler)
    end fn

    # Register a page handler for DELETE requests
    pub fn delete(mut self, path: text, handler: fn(nyweb_request, session) -> nyweb_response)
        self.register(path, http_method.delete, handler)
    end fn

    # Register a page handler for PATCH requests
    pub fn patch(mut self, path: text, handler: fn(nyweb_request, session) -> nyweb_response)
        self.register(path, http_method.patch, handler)
    end fn

    # Set the 404 handler
    pub fn not_found(mut self, handler: fn(nyweb_request, session) -> nyweb_response)
        self.not_found_handler = Option.Some(handler)
    end fn

    # Dispatch a request to the appropriate page handler
    pub fn dispatch(self, req: nyweb_request, session: session) -> nyweb_response
        let method = req.method()
        let path = req.path()

        # Search for matching route
        for entry in self.pages
            if entry.method == method and entry.path == path then
                return entry.handler(req, session)
            end if
        end for

        # No route found
        if self.not_found_handler.is_some() then
            return self.not_found_handler.unwrap()(req, session)
        else
            return nyweb_response.html(http_status.not_found,
                "<h1>404 Not Found</h1><p>Page not found: " + path + "</p>")
        end if
    end fn
end impl

# =============================================================================
# Convention-Based Page Discovery
# =============================================================================

# Discover pages from a directory and register them
pub fn discover_pages(pages_dir: text, dispatcher: page_dispatcher) -> void
    # TODO: Implement directory scanning and page class instantiation
    # For now, pages must be registered explicitly
    println("Page discovery not yet implemented")
end fn
