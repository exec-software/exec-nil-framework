fn choose_larger<T>(left: T, right: T) -> T
    if left > right then
        return left
    end if
    return right
end fn

fn main() -> i32
    let first_region: i32 = choose_larger<i32>(17, 29)
    let second_region: i32 = choose_larger<i32>(31, 23)
    let deployment_score: i32 = first_region + second_region

    println("GENERIC SELECTOR")
    println(first_region)
    println(second_region)
    println(deployment_score)

    if deployment_score >= 60 then
        println("DEPLOY")
    else
        println("HOLD")
    end if
    return 0
end fn
