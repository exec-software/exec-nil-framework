struct Point {
    x: i32
    y: i32
}

impl Point
    pub function new() -> Point
        return Point { x: 0, y: 0 }
    end function
end impl

pub function main()
    let p = Point::new()
end function
