@pragma once
# Experimental: NyWeb depends on the unavailable HTTP-server provider.
module nil.std.net

import nil.std.core

# =============================================================================
# NyWeb Template Engine
# Parses and renders .nyhtml files.
#
# Template syntax:
#   <%= expr %>     Evaluate expr, HTML-escape, write to output
#   <%! expr %>     Evaluate expr, write RAW (unescaped) to output
#   <% stmt %>      Execute stmt (no output) — for loops, if blocks, etc.
#   <%@ include "path/to/partial.nyhtml" %>
#   <%@ layout "layouts/main.nyhtml" %>
#
# The template engine compiles .nyhtml files to Ny code at build time.
# At runtime, the compiled render function is called with the page model.
# =============================================================================

# A compiled template (already parsed, ready to render)
pub struct compiled_template
    path: text
    source: text        # Original .nyhtml source (for error reporting)
    compiled_fn: text   # Compiled Ny fn body (for the codegen phase)
end struct

# Template rendering context passed to each render call
pub struct render_context
    model_type: text    # The type name of the page model
    layout_path: Option<text>
    includes: vec<text>
    output: text        # Accumulated HTML output
end struct

impl render_context
    pub fn new() -> render_context
        return render_context {
            model_type: "",
            layout_path: Option.None,
            includes: vec.new(),
            output: ""
        }
    end fn

    pub fn write(mut self, text: text)
        self.output = self.output + text
    end fn

    pub fn write_escaped(mut self, text: text)
        self.output = self.output + ml_html_escape(text)
    end fn
end impl

# =============================================================================
# Template cache
# Templates are parsed and cached on first use.
# In production, all templates are pre-compiled at startup.
# =============================================================================

pub struct template_cache
    entries: vec<template_cache_entry>
    templates_dir: text
    auto_reload: bool   # True in development: re-parses on file change
end struct

pub struct template_cache_entry
    path: text
    compiled: compiled_template
    last_modified: i64
end struct

impl template_cache
    pub fn new(templates_dir: text, auto_reload: bool) -> template_cache
        # Tell the C++ layer where templates live so layout paths resolve correctly
        ml_template_set_dir(templates_dir)
        return template_cache {
            entries: vec.new(),
            templates_dir: templates_dir,
            auto_reload: auto_reload
        }
    end fn

    pub fn dev(templates_dir: text) -> template_cache
        return template_cache.new(templates_dir, true)
    end fn

    pub fn prod(templates_dir: text) -> template_cache
        return template_cache.new(templates_dir, false)
    end fn
end impl

# =============================================================================
# Native template rendering — primitive-only @interop ABI
#
# ABI rules (CRITICAL — must match template_engine.cpp extern "C"):
#   text params  → const char*
#   text returns → const char* (thread-local buffer in C++)
#   i64 returns  → int64_t
#   void         → no return
# =============================================================================

# Returns the last error from any ml_nyweb call (thread-local).
@interop("ml_nyweb")
pub fn ml_nyweb_last_error() -> text
end fn

# Set the templates base directory (include/layout paths resolve relative to it).
@interop("ml_nyweb")
pub fn ml_template_set_dir(templates_dir: text)
end fn

# Render a .nyhtml template with layout/include/body support.
# model_json: flat JSON {"key":"value",...} for <%= key %> substitution.
# Returns rendered HTML, or empty string on error (call ml_nyweb_last_error()).
@interop("ml_nyweb")
pub fn ml_template_render(template_path: text, model_json: text) -> text
end fn

# HTML-escape a string (< > & " ' → entities).
@interop("ml_nyweb")
pub fn ml_html_escape(raw: text) -> text
end fn

# Build a minimal JSON model from page type and data (Phase 1.9: replace with real serialization).
@interop("ml_nyweb")
pub fn ml_page_to_json(page_type: text, page_data: text) -> text
end fn
