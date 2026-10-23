import nil.std.collections
import nil.std.core
fn main() -> i32
let selected = collect query
from item in [3,1,2]
order by item
select (item,$"row {item}")
end query
println(query_count<(i32,text)>(selected))
println(query_any<(i32,text)>(selected))
println(query_first<(i32,text)>(selected).unwrap().1)
println(query_last<(i32,text)>(selected).unwrap().1)
println(query_single<(i32,text)>(selected).unwrap_error())
let just_one = collect query
from pair in selected
where pair.0 == 2
select pair
end query
println(query_single<(i32,text)>(just_one).unwrap().1)
let no_rows = collect query
from pair in selected
take 0
select pair
end query
println(query_count<(i32,text)>(no_rows))
println(query_any<(i32,text)>(no_rows))
println(query_first<(i32,text)>(no_rows).is_none())
println(query_last<(i32,text)>(no_rows).is_none())
println(query_single<(i32,text)>(no_rows).unwrap_error())
return 0
end fn
