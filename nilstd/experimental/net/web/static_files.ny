@pragma once
# Experimental: NyWeb depends on the unavailable HTTP-server provider.
module nil.std.net

import nil.std.core
import nil.std.net.http_server.request
import nil.std.net.http_server.response
import nil.std.net.http_server.context

# =============================================================================
# Static File Serving
# Serves files from a directory on disk for a URL prefix.
# Used for CSS, JS, images, fonts — anything that doesn't need rendering.
#
# Example: serve /assets/* from disk at web/assets/
# =============================================================================

pub struct static_file_options
    url_prefix: text        # URL prefix this handler owns (e.g. "/assets/")
    root_dir: text          # Filesystem directory to serve from
    cache_max_age: i32      # Cache-Control max-age in seconds (0 = no cache)
    allow_directory_listing: bool
    index_files: vec<text>  # Try these files when a directory is requested
end struct

impl static_file_options
    pub fn new(url_prefix: text, root_dir: text) -> static_file_options
        let mut index: vec<text> = vec.new()
        index.push("index.html")
        index.push("index.nyhtml")
        return static_file_options {
            url_prefix: url_prefix,
            root_dir: root_dir,
            cache_max_age: 86400,    # 1 day default
            allow_directory_listing: false,
            index_files: index
        }
    end fn

    pub fn no_cache(mut self) -> static_file_options
        self.cache_max_age = 0
        return self
    end fn

    pub fn with_max_age(mut self, seconds: i32) -> static_file_options
        self.cache_max_age = seconds
        return self
    end fn
end impl

# Attempt to serve a static file for a request.
# Returns some(response) if the file was found, none to pass to the next handler.
pub fn try_serve_static(req: http_server_request, opts: static_file_options) -> Option<http_server_response>
    let path = req.get_path()
    if not path.starts_with(opts.url_prefix) then
        return Option.None
    end if
    let rel_path = path.substring(opts.url_prefix.len())
    let file_path = opts.root_dir + "/" + rel_path
    return ml_serve_static_file(file_path, opts)
end fn

# Get the MIME type for a file extension
pub fn mime_for_extension(ext: text) -> text
    match ext
        "html" -> return "text/html; charset=utf-8"
        "css"  -> return "text/css"
        "js"   -> return "application/javascript"
        "json" -> return "application/json"
        "png"  -> return "image/png"
        "jpg"  -> return "image/jpeg"
        "jpeg" -> return "image/jpeg"
        "gif"  -> return "image/gif"
        "svg"  -> return "image/svg+xml"
        "ico"  -> return "image/x-icon"
        "woff" -> return "font/woff"
        "woff2"-> return "font/woff2"
        "ttf"  -> return "font/ttf"
        "pdf"  -> return "application/pdf"
        "zip"  -> return "application/zip"
        "txt"  -> return "text/plain"
        _      -> return "application/octet-stream"
    end match
end fn

# =============================================================================
# Native bindings
# =============================================================================

@interop("ml_nyweb")
pub fn ml_serve_static_file(file_path: text, opts: static_file_options) -> Option<http_server_response>
end fn
