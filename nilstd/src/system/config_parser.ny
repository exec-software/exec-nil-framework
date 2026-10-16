module nil.std.system

import nil.std.core
import nil.std.io.file
import nil.std.text.string_utils

// Simple key-value config parser for .nyp project files
// Format: "key: value" lines, # comments, blank lines ignored

pub struct config_entry
    pub key: text
    pub value: text
end struct

pub struct config_data
    pub entries: array<config_entry>
    pub count: i32
end struct

// Create an empty config
pub fn empty_config() -> config_data
    return config_data {
        entries: [],
        count: 0
    }
end fn

// Parse config content from a string (key: value format)
pub fn parse_config(content: text) -> config_data
    let mut entries: array<config_entry> = []
    let mut count: i32 = 0

    let content_len = content.len()
    let mut line_start = 0

    while line_start <= content_len do
        let mut line_end = line_start
        while line_end < content_len and content.substr(line_end, 1) != "\n" do
            line_end = line_end + 1
        end while

        let line = nil::std::text::trim(content.substr(line_start, line_end - line_start))
        let len = nil::std::text::length(line)

        // Skip empty lines and comments
        if len > 0 and not nil::std::text::starts_with(line, "#") then
            // Find the first colon separator
            let colon_pos = nil::std::text::index_of(line, ":")
            if colon_pos > 0 then
                let key = nil::std::text::trim(nil::std::text::substring(line, 0, colon_pos))
                let value = nil::std::text::trim(nil::std::text::substring(line, colon_pos + 1, len))

                if nil::std::text::length(key) > 0 then
                    let entry = config_entry {
                        key: key,
                        value: value
                    }
                    # Duplicate keys use the last value, matching layered .nyp
                    # configuration while keeping count equal to unique keys.
                    let mut replaced = false
                    let mut existing = 0
                    while existing < count do
                        if entries[existing].key == key then
                            entries[existing] = entry
                            replaced = true
                            break
                        end if
                        existing = existing + 1
                    end while
                    if not replaced then
                        entries = entries + [entry]
                        count = count + 1
                    end if
                end if
            end if
        end if

        if line_end >= content_len then break end if
        line_start = line_end + 1
    end while

    return config_data {
        entries: entries,
        count: count
    }
end fn

// Load and parse a config file
# File errors stay observable. An empty file is a successful empty config.
pub fn load_config_file(file_path: text) -> Result<config_data, text>
    let read_result = nil::std::io::read_text(file_path)
    match read_result
        Result.Ok(content) ->
            if nil::std::text::length(content) == 0 then
                return Result.Ok(empty_config())
            end if
            return Result.Ok(parse_config(content))
        Result.Error(message) -> return Result.Error(message)
    end match
end fn

// Get a config value by key (returns fallback if not found)
pub fn config_get(cfg: config_data, key: text, fallback: text) -> text
    let mut i = 0
    # config_data is public, so do not trust a caller-supplied count when
    # indexing. entries is the authoritative collection boundary.
    while i < cfg.entries.length() do
        if cfg.entries[i].key == key then
            return cfg.entries[i].value
        end if
        i = i + 1
    end while
    return fallback
end fn

// Check if a key exists in config
pub fn config_has(cfg: config_data, key: text) -> bool
    let mut i = 0
    while i < cfg.entries.length() do
        if cfg.entries[i].key == key then
            return true
        end if
        i = i + 1
    end while
    return false
end fn
