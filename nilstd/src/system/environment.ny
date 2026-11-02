@pragma once
# ny.lang.system - Environment variable and process environment access
# Interpreter-backed process environment access. Native-route lowering is not
# yet available and is tracked separately from this executable contract.

module nil.std.system

import nil.std.core

# A single name/value pair returned by environment enumeration.
pub struct environment_variable
    pub name: text
    pub value: text
end struct

# Get an environment variable without conflating an absent variable with an
# explicitly present empty value.
@intrinsic("environment.get")
pub fn get_environment_variable(name: text) -> Option<text>
end fn

# Set or remove an environment variable in this process. Names are validated;
# host failures are returned with a diagnostic rather than silently ignored.
@intrinsic("environment.set")
pub fn set_environment_variable(name: text, value: text) -> Result<bool, text>
end fn

@intrinsic("environment.unset")
pub fn unset_environment_variable(name: text) -> Result<bool, text>
end fn

# Enumerate a bounded snapshot. The runtime rejects hostile ambient
# environments that exceed its item or byte budget.
@intrinsic("environment.list")
pub fn get_environment_variables() -> array<environment_variable>
end fn

# Get current working directory
@intrinsic("environment.current_directory")
pub fn get_current_directory() -> text
end fn

# Set current working directory with an explicit error channel.
@intrinsic("environment.set_current_directory")
pub fn set_current_directory(path: text) -> Result<bool, text>
end fn

# Get the host machine name
@intrinsic("environment.machine_name")
pub fn get_machine_name() -> text
end fn

# Get the current OS user name
@intrinsic("environment.user_name")
pub fn get_user_name() -> text
end fn

# The host's own line terminator: "\r\n" on Windows, "\n" everywhere else.
#
# Nothing in the framework uses this by default, and that is the point.
# println and the whole io surface emit "\n" on every host, so identical Ny
# source produces identical bytes everywhere and a recorded transcript
# describes every platform at once. That is the right default for data a
# program reads back, hashes, or compares.
#
# It is the wrong default for a file a *person* will open in a Windows editor,
# or for a protocol that specifies CRLF. Ask for the terminator in those
# cases, the way C# code reaches for Environment.NewLine -- explicitly, where
# a reader can see the choice was made.
@intrinsic("environment.newline")
pub fn newline() -> text
end fn
