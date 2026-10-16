# EXPERIMENTAL DESIGN SOURCE — excluded from nilstd and nillib.
#
# The former active agent marker API returned success without creating or
# reading any marker. Keep the intended public shape here while a portable,
# atomic and path-confined implementation is designed. Do not import this file
# from production code.

module nil.std.system.experimental

pub struct agent_marker
    pub status: text
    pub message: text
end struct

# Planned operations:
#   initialize a caller-selected workspace
#   atomically replace one structured marker
#   read and validate that marker under a bounded input budget
#   clear only the marker owned by this application
