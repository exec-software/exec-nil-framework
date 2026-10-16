@pragma once
# Experimental: NyWeb depends on the unavailable HTTP-server provider.
module nil.std.net

import nil.std.core

# Simple contact page
class ContactPage
    pub title: text = "Contact"
    pub submitted: bool = false

    pub fn new() -> ContactPage
        return ContactPage {
            title: "Contact",
            submitted: false
        }
    end fn

    pub fn submit(name: text, email: text, message: text) -> bool
        if name == "" or email == "" then
            return false
        end if
        self.submitted = true
        return true
    end fn
end class
