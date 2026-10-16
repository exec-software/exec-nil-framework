struct StartupFault
    code: i32
end struct
fn load_configuration() -> i32
    raise StartupFault { code: 15 }
    return 0
end fn
let configuration: i32 = load_configuration()
fn main() -> i32
    println("main-must-not-run")
    return configuration
end fn
