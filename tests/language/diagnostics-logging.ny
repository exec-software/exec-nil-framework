import ny.lang.diagnostics.*

fn main() -> i32
    println(log_level_name(LogLevel.error))
    println(log_level_value(LogLevel.critical))

    let logger: Logger = Logger.new_with_level("scope-test", LogLevel.warning)
    println(logger.is_enabled(LogLevel.info))
    return 0
end fn
