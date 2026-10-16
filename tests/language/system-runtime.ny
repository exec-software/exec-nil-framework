import ny.lang.core.*
import ny.lang.system.*

fn main() -> i32
    let variable_name = "NY_STDLIB_CONTROLLED_ENV_TEST_7F3A"
    unset_environment_variable(variable_name)
    println(get_environment_variable(variable_name).is_none())
    println(set_environment_variable(variable_name, "present").is_ok())

    let value = get_environment_variable(variable_name)
    match value
        Option.Some(text_value) -> println(text_value)
        Option.None -> println("missing")
    end match

    println(set_environment_variable(variable_name, "").is_ok())
    match get_environment_variable(variable_name)
        Option.Some(empty_value) -> println(empty_value.len())
        Option.None -> println(-1)
    end match

    let mut found = false
    let environment = get_environment_variables()
    let mut i = 0
    while i < environment.length() do
        if environment[i].name == variable_name then
            found = true
        end if
        i = i + 1
    end while
    println(found)
    println(set_environment_variable("bad=name", "value").is_error())
    println(set_environment_variable(variable_name, Convert.i32_to_char(0)).is_error())
    println(get_current_directory().len() > 0)
    println(set_current_directory(".").is_ok())
    println(get_machine_name().len() > 0)
    println(get_user_name().len() > 0)
    println(try_arg(-1).is_none())
    println(try_arg(arg_count()).is_none())

    let config = parse_config("# ignored\nname: Ny\nurl: https://example.test:9443\nname: newer\n")
    println(config.count)
    println(config_get(config, "name", "missing"))
    println(config_get(config, "url", "missing"))
    println(config_get(config, "absent", "fallback"))
    let malformed_config = config_data { entries: [], count: 99 }
    println(config_get(malformed_config, "missing", "bounded"))
    println(config_has(malformed_config, "missing"))
    println(load_config_file("__ny_missing_config_7f3a__.nyp").is_error())

    println(unset_environment_variable(variable_name).is_ok())
    println(get_environment_variable(variable_name).is_none())
    return 0
end fn
