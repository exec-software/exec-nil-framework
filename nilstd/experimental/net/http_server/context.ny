@pragma once
# Experimental: the ml_nyhttp provider is not linked into the SDK.
module nil.std.net

import nil.std.core
import nil.std.net.http_server.request

# State bag entry for middleware/handler key-value storage
pub struct ctx_value
    pub key: text
    pub value: text
end struct

pub struct http_context
    pub request: http_server_request
    pub values: vec<ctx_value>
end struct

impl http_context
    pub fn new(req: http_server_request) -> http_context
        let empty = vec<ctx_value>.new()
        return http_context {
            request: req,
            values: empty
        }
    end fn
end impl
