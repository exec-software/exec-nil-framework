module regression.model

pub struct error
    pub value: i32
end struct

impl error
    pub fn new(value: i32) -> error
        return error { value: value }
    end fn
end impl
