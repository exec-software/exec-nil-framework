@schema
@pragma once

module nil.std.diagnostics

import nil.std.core
import nil.std.datetime.std_time
import nil.std.io.console

# =============================================================================
# Logging - Structured Logging Framework (mirrors Microsoft.Extensions.Logging)
# =============================================================================
# Provides structured logging with:
#   - Log levels: Trace, Debug, Info, Warning, Error, Critical
#   - Named loggers with level filtering
#   - Timestamped output
#   - Console and file output targets
#   - Scoped logging context
#
# Usage:
#   import ny.lang.diagnostics
#   let logger = Logger.new("MyApp")
#   logger.info("Application started")
#   logger.error("Something went wrong: " + error_msg)
# =============================================================================

# =============================================================================
# LogLevel Enum
# =============================================================================

pub enum LogLevel
    trace
    debug
    info
    warning
    error
    critical
    none
end enum

# =============================================================================
# LogLevel Utilities
# =============================================================================

pub fn log_level_name(level: LogLevel) -> text
    match level
        LogLevel.trace -> return "TRACE"
        LogLevel.debug -> return "DEBUG"
        LogLevel.info -> return "INFO"
        LogLevel.warning -> return "WARN"
        LogLevel.error -> return "ERROR"
        LogLevel.critical -> return "CRIT"
        LogLevel.none -> return "NONE"
    end match
end fn

pub fn log_level_value(level: LogLevel) -> i32
    match level
        LogLevel.trace -> return 0
        LogLevel.debug -> return 1
        LogLevel.info -> return 2
        LogLevel.warning -> return 3
        LogLevel.error -> return 4
        LogLevel.critical -> return 5
        LogLevel.none -> return 6
    end match
end fn

# =============================================================================
# LogEntry Struct
# =============================================================================

pub struct LogEntry
    pub timestamp: text
    pub level: LogLevel
    pub category: text
    pub message: text
    pub scope: text
end struct

impl LogEntry
    pub fn new(level: LogLevel, category: text, message: text) -> LogEntry
        let now_ms = nil::std::datetime::now()
        return LogEntry {
            timestamp: Convert.i64_to_text(now_ms),
            level: level,
            category: category,
            message: message,
            scope: ""
        }
    end fn

    pub fn format(self: ref<Self>) -> text
        let level_str = log_level_name(self.level)
        let mut result = "[" + self.timestamp + "] "
        result = result + "[" + level_str + "] "
        if self.category.len() > 0 then
            result = result + self.category + ": "
        end if
        if self.scope.len() > 0 then
            result = result + "(" + self.scope + ") "
        end if
        result = result + self.message
        return result
    end fn
end impl

# =============================================================================
# Logger Struct
# =============================================================================

pub struct Logger
    pub name: text
    pub min_level: LogLevel
    pub include_timestamp: bool
    pub include_level: bool
    pub scope_stack: text
end struct

impl Logger
    # Constructors
    pub fn new(name: text) -> Logger
        return Logger {
            name: name,
            min_level: LogLevel.info,
            include_timestamp: true,
            include_level: true,
            scope_stack: ""
        }
    end fn

    pub fn new_with_level(name: text, level: LogLevel) -> Logger
        return Logger {
            name: name,
            min_level: level,
            include_timestamp: true,
            include_level: true,
            scope_stack: ""
        }
    end fn

    # Check if level is enabled
    pub fn is_enabled(self: ref<Self>, level: LogLevel) -> bool
        return log_level_value(level) >= log_level_value(self.min_level)
    end fn

    # Set minimum log level
    pub fn set_level(mut self, level: LogLevel)
        self.min_level = level
    end fn

    # =========================================================================
    # Core Logging Method
    # =========================================================================

    pub fn log(self: ref<Self>, level: LogLevel, message: text)
        if not self.is_enabled(level) then
            return
        end if

        let mut entry = LogEntry.new(level, self.name, message)
        entry.scope = self.scope_stack

        let formatted = entry.format()
        nil::std::io::writeln(formatted)
    end fn

    # =========================================================================
    # Level-Specific Methods
    # =========================================================================

    pub fn trace(self: ref<Self>, message: text)
        self.log(LogLevel.trace, message)
    end fn

    pub fn debug(self: ref<Self>, message: text)
        self.log(LogLevel.debug, message)
    end fn

    pub fn info(self: ref<Self>, message: text)
        self.log(LogLevel.info, message)
    end fn

    pub fn warning(self: ref<Self>, message: text)
        self.log(LogLevel.warning, message)
    end fn

    pub fn warn(self: ref<Self>, message: text)
        self.log(LogLevel.warning, message)
    end fn

    pub fn error(self: ref<Self>, message: text)
        self.log(LogLevel.error, message)
    end fn

    pub fn critical(self: ref<Self>, message: text)
        self.log(LogLevel.critical, message)
    end fn

    # =========================================================================
    # Scoped Logging
    # =========================================================================

    pub fn begin_scope(mut self, scope_name: text) -> Logger
        let mut scoped = Logger {
            name: self.name,
            min_level: self.min_level,
            include_timestamp: self.include_timestamp,
            include_level: self.include_level,
            scope_stack: scope_name
        }
        if self.scope_stack.len() > 0 then
            scoped.scope_stack = self.scope_stack + " > " + scope_name
        end if
        return scoped
    end fn
end impl

# =============================================================================
# LoggerFactory - Create Named Loggers
# =============================================================================

pub struct LoggerFactory
    pub default_level: LogLevel
end struct

impl LoggerFactory
    pub fn new() -> LoggerFactory
        return LoggerFactory { default_level: LogLevel.info }
    end fn

    pub fn new_with_level(level: LogLevel) -> LoggerFactory
        return LoggerFactory { default_level: level }
    end fn

    pub fn create_logger(self: ref<Self>, name: text) -> Logger
        return Logger.new_with_level(name, self.default_level)
    end fn
end impl

# =============================================================================
# Global Convenience Functions
# =============================================================================

pub fn log_trace(message: text)
    let logger = Logger.new_with_level("", LogLevel.trace)
    logger.trace(message)
end fn

pub fn log_debug(message: text)
    let logger = Logger.new_with_level("", LogLevel.debug)
    logger.debug(message)
end fn

pub fn log_info(message: text)
    let logger = Logger.new("")
    logger.info(message)
end fn

pub fn log_warning(message: text)
    let logger = Logger.new("")
    logger.warning(message)
end fn

pub fn log_error(message: text)
    let logger = Logger.new("")
    logger.error(message)
end fn

pub fn log_critical(message: text)
    let logger = Logger.new("")
    logger.critical(message)
end fn
