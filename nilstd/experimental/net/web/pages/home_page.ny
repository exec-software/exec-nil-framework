@pragma once
# Experimental: NyWeb depends on the unavailable HTTP-server provider.
module nil.std.net

import nil.std.core

# Simple home page - minimal class
class HomePage
    pub title: text = "Home"

    pub fn new() -> HomePage
        return HomePage {
            title: "Home"
        }
    end fn

    pub fn get_title() -> text
        return self.title
    end fn
end class
