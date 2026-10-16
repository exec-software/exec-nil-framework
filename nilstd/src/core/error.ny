# Ny standard library - error types
# Standard error type infrastructure for recoverable errors

module nil.std.core


# =============================================================================
# Error Kind Enum
# =============================================================================

# Categorizes errors by their type
pub enum error_kind
    io_error           # File/IO related errors
    parse_error        # Parsing/deserialization errors
    network_error      # Network/HTTP related errors
    validation_error   # Data validation failures
    not_found          # Resource not found
    permission_denied  # Access denied
    timeout            # Operation timed out
    custom(text)       # User-defined error type
end enum

# =============================================================================
# Standard Error Struct
# =============================================================================

# Generic error type for chainable error handling
pub struct error
    pub message: text             # Human-readable error message
    pub kind: error_kind          # Error category
    pub source: Option<any>       # Optional source error (for chaining)
end struct

# =============================================================================
# Error Methods
# =============================================================================

impl error
    # Create a new error with a message and kind
    pub fn new(message: text, kind: error_kind) -> error
        return error {
            message: message,
            kind: kind,
            source: Option.None
        }
    end fn

    # Create an error with a source error for chaining
    pub fn with_source(message: text, kind: error_kind, source: any) -> error
        return error {
            message: message,
            kind: kind,
            source: Option.Some(source)
        }
    end fn

    # Create a simple IO error
    pub fn io(message: text) -> error
        return error.new(message, error_kind.io_error)
    end fn

    # Create a simple parse error
    pub fn parse(message: text) -> error
        return error.new(message, error_kind.parse_error)
    end fn

    # Create a validation error
    pub fn validation(message: text) -> error
        return error.new(message, error_kind.validation_error)
    end fn

    # Create a not found error
    pub fn not_found(message: text) -> error
        return error.new(message, error_kind.not_found)
    end fn
end impl

# =============================================================================
# Display Implementation
# =============================================================================

impl Display for error
    pub fn to_string(self) -> text
        return "[" + self.kind.to_string() + "] " + self.message
    end fn
end impl

impl Display for error_kind
    pub fn to_string(self) -> text
        match self
            error_kind.io_error -> return "IO"
            error_kind.parse_error -> return "Parse"
            error_kind.network_error -> return "Network"
            error_kind.validation_error -> return "Validation"
            error_kind.not_found -> return "NotFound"
            error_kind.permission_denied -> return "PermissionDenied"
            error_kind.timeout -> return "Timeout"
            error_kind.custom(name) -> return name
        end match
    end fn
end impl
