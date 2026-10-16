// Test for fn keyword support
# This should compile with updated Ny compiler

fn test_function()
    println("fn keyword test - should work")
end fn

function test_function_traditional()
    println("function keyword test - should work")
end function

fn main()
    println("Testing fn and function keywords...")
    test_function()
    test_function_traditional()
    println("Both syntax tests should work!")
end fn