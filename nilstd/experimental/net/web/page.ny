@pragma once
# Experimental: NyWeb depends on the unavailable HTTP-server provider.
module nil.std.net

import nil.std.core
import nil.std.net.web.request
import nil.std.net.web.response
import nil.std.net.web.session
import nil.std.net.web.template

# =============================================================================
# IPage — The NyWeb page contract
#
# Every NyWeb page has a .ny controller file that implements IPage.
# The controller handles one or more HTTP methods (on_get, on_post, etc.)
# and returns a nyweb_response.
#
# Convention:
#   pages/dashboard.ny       → implements IPage, paired with dashboard.nyhtml
#   pages/login.ny           → implements IPage, paired with login.nyhtml
#
# The NyWeb router maps URL paths to IPage implementations either by:
#   a. Convention: /dashboard → pages/dashboard.ny.DashboardPage
#   b. Explicit registration in the app configuration
# =============================================================================

pub trait IPage
    # Called before any method handler — return false to abort (e.g. auth check)
    fn before_request(mut self, req: nyweb_request, session: session) -> bool

    # Called for GET requests. Override to handle GET.
    fn on_get(mut self, req: nyweb_request, session: session) -> nyweb_response

    # Called for POST requests. Override to handle POST.
    fn on_post(mut self, req: nyweb_request, session: session) -> nyweb_response

    # Called for PUT requests.
    fn on_put(mut self, req: nyweb_request, session: session) -> nyweb_response

    # Called for DELETE requests.
    fn on_delete(mut self, req: nyweb_request, session: session) -> nyweb_response

    # Called for PATCH requests.
    fn on_patch(mut self, req: nyweb_request, session: session) -> nyweb_response

    # The page title (accessible from templates as <%=Page.title%>)
    fn title(self) -> text
end trait

# =============================================================================
# BasePage — Default implementations so controllers only override what they need
# =============================================================================

pub abstract class base_page
    # Page metadata (set by the controller before rendering)
    pub title: text
    pub description: text

    pub fn new() -> base_page
        return base_page {
            title: "",
            description: ""
        }
    end fn

    # Default before_request: always continue
    pub fn before_request(mut self, req: nyweb_request, session: session) -> bool
        return true
    end fn

    # Default on_get: 405 Method Not Allowed
    pub fn on_get(mut self, req: nyweb_request, session: session) -> nyweb_response
        return nyweb_response.html(http_status.method_not_allowed,
            "<h1>405 Method Not Allowed</h1>")
    end fn

    # Default on_post: 405
    pub fn on_post(mut self, req: nyweb_request, session: session) -> nyweb_response
        return nyweb_response.html(http_status.method_not_allowed,
            "<h1>405 Method Not Allowed</h1>")
    end fn

    # Default on_put: 405
    pub fn on_put(mut self, req: nyweb_request, session: session) -> nyweb_response
        return nyweb_response.html(http_status.method_not_allowed,
            "<h1>405 Method Not Allowed</h1>")
    end fn

    # Default on_delete: 405
    pub fn on_delete(mut self, req: nyweb_request, session: session) -> nyweb_response
        return nyweb_response.html(http_status.method_not_allowed,
            "<h1>405 Method Not Allowed</h1>")
    end fn

    # Default on_patch: 405
    pub fn on_patch(mut self, req: nyweb_request, session: session) -> nyweb_response
        return nyweb_response.html(http_status.method_not_allowed,
            "<h1>405 Method Not Allowed</h1>")
    end fn

    # Render a template with a JSON model string.
    # model_json: flat JSON {"key":"value",...} matching template variables.
    pub fn render(self, template_path: text, model_json: text) -> nyweb_response
        let html = ml_template_render(template_path, model_json)
        if html == "" then
            let err = ml_nyweb_last_error()
            return nyweb_response.html(http_status.internal_server_error,
                "<h1>Template Error</h1><pre>" + err + "</pre>")
        end if
        return nyweb_response.ok(html)
    end fn

    # Require authentication — call at the top of before_request for protected pages
    pub fn require_auth(self, session: session, login_url: text) -> Option<nyweb_response>
        if not session.is_authenticated() then
            return Option.Some(nyweb_response.redirect(login_url))
        end if
        return Option.None
    end fn
end class

# Create a page instance from a class name. Dynamic class lookup is not yet
# available, so callers register concrete page constructors with the dispatcher.
pub fn create_page(class_name: text) -> Option<base_page>
    return Option.None
end fn

# =============================================================================
# Native bindings (primitive-only ABI — no struct pass-through)
# =============================================================================

# These are declared in template.ny — re-exported here for page convenience.
# ml_template_render, ml_nyweb_last_error, ml_page_to_json are in template.ny.
