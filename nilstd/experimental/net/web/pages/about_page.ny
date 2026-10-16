@pragma once
# Experimental: NyWeb depends on the unavailable HTTP-server provider.
module nil.std.net

import nil.std.core

# Simple about page
class AboutPage
    pub title: text = "About"
    pub version: text = "0.1.0"

    pub fn new() -> AboutPage
        return AboutPage {
            title: "About",
            version: "0.1.0"
        }
    end fn

    pub fn get_info() -> text
        return self.title + " v" + self.version
    end fn
end class
