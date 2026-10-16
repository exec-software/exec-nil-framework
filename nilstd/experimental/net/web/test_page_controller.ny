@pragma once
# Experimental: NyWeb depends on the unavailable HTTP-server provider.
module nil.std.net

import nil.std.core

# Simple test page controller
class TestPage
    pub title: text = "Test Page"
    pub description: text = "A test page for NyWeb"

    pub fn new() -> TestPage
        return TestPage {
            title: "Test Page",
            description: "A test page for NyWeb"
        }
    end fn

    pub fn get_title() -> text
        return self.title
    end fn
end class
