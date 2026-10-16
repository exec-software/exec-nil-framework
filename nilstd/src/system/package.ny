@pragma once
# =============================================================================
# ny.lang.system — System Package
# =============================================================================
# Process environment, arguments, and configuration.
#
# Provides:
#   - Environment: checked get/set/list, cwd, host identity (environment.ny)
#   - Config parser: INI/key-value config files (config_parser.ny)
#   - sys: exit, arg_count, arg (std_sys.ny)
#
# Cross-refs:
#   ny.lang.core       — text, Result<T,E>
#   ny.lang.io         — file I/O for config loading
# Experimental process execution and agent markers remain outside the SDK
# until their bounded, portable runtime contracts are implemented.
# =============================================================================

package nil.std.system

import nil.std.system.environment
import nil.std.system.config_parser
import nil.std.system.std_sys
