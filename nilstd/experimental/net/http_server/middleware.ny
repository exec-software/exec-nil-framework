@pragma once
# Experimental: the ml_nyhttp provider is not linked into the SDK.
module nil.std.net

import nil.std.core

pub struct middleware_pipeline
    pub stages: vec<text>
end struct

impl middleware_pipeline
    pub fn new() -> middleware_pipeline
        return middleware_pipeline {
            stages: vec<text>.new()
        }
    end fn
end impl
