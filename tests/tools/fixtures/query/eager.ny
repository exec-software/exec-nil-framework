fn traced_source() -> array<i32>
println("source")
return [5,1,4,2,3]
end fn
fn traced_bound(amount: i64, label: text) -> i64
println(label)
return amount
end fn
fn accepted(item: i32) -> bool
println($"where {item}")
return item > 1
end fn
fn keyed(item: i32) -> i32
println($"key {item}")
return item % 2
end fn
fn projected(item: i32) -> i32
println($"give {item}")
return item * 10
end fn
fn mutate(items: array<i32>, item: i32) -> bool
items[1] = 99
return true
end fn
@intrinsic("math.sqrt")
fn root(x: f64) -> f64
end fn
fn changed_bound(items: array<i32>) -> i64
items[1] = 44
return 0
end fn
fn main() -> i32
let ordered = collect query
from item in traced_source()
where accepted(item)
order by keyed(item) ascending
skip traced_bound(1,"skip")
take traced_bound(2,"take")
select projected(item)
end query
println(ordered[0])
println(ordered[1])
let direct = collect query
from item in traced_source()
where accepted(item)
skip 1
take 2
select projected(item)
end query
println(direct[0])
println(direct[1])
let zero = collect query
from item in [1,2]
where accepted(item)
take 0
select projected(item)
end query
println(zero.len())
let empty: array<i32> = []
let missing = collect query
from item in empty
order by item
skip traced_bound(0,"empty skip")
take traced_bound(9,"empty take")
select item
end query
println(missing.len())
let rows = [(2,"two-first"),(1,"one"),(2,"two-last")]
let sorted = collect query
from item in rows
order by item.0 ascending
select (item.1,item.0)
end query
println(sorted[0].0)
println(sorted[1].0)
println(sorted[2].0)
let reverse = collect query
from item in rows
order by item.0 descending
select item.1
end query
println(reverse[0])
println(reverse[1])
println(reverse[2])
let words = collect query
from word in ["z","a","ä","a"]
order by word
select word
end query
println(words[0])
println(words[1])
println(words[2])
println(words[3])
let flags = collect query
from flag in [true,false,true,false]
order by flag
select flag
end query
println(flags[0])
println(flags[1])
println(flags[2])
println(flags[3])
let bytes = collect query
from byte in [255 as u8,1 as u8,128 as u8]
order by byte
select byte
end query
println(bytes[0])
println(bytes[1])
println(bytes[2])
let wide = collect query
from item in [9007199254740993,9007199254740992,-9007199254740993]
order by item
select item
end query
println(wide[0])
println(wide[1])
println(wide[2])
let singleton = collect query
from pair in [(9,),(2,)]
order by pair.0
select pair
end query
println(singleton[0].0)
let nested = collect query
from pair in [((2,"b"),true),((1,"a"),false)]
order by pair.0.0
select pair
end query
println(nested[0].0.1)
println(nested[1].1)
let inputs = [1,2,3]
let snapshot = collect query
from item in inputs
where mutate(inputs,item)
select item
end query
println(snapshot[1])
println(inputs[1])
let item = 77
let shadowed = collect query
from item in [10,20]
select (collect query
from item in [2,3]
where item > 2
select item
end query)[0] + item
end query
println(shadowed[0])
println(shadowed[1])
println(item)
let row = 2
let bound_scope = collect query
from row in [1,2,3]
skip row
take 1
select row
end query
println(bound_scope[0])
println(row)
let collect = 4
let query = 5
println(collect + query)
let nan = root(-1.0)
let floating = [(nan,0),(2.0,1),(-0.0,2),(0.0,3),(nan,4),(-1.0,5)]
let floats = collect query
from pair in floating
order by pair.0
select pair.1
end query
let floats_desc = collect query
from pair in floating
order by pair.0 descending
select pair.1
end query
for position in range(0,6)
println(floats[position])
end for
for position in range(0,6)
println(floats_desc[position])
end for
let changed = [1,2]
let after_bound = collect query
from row in changed
skip changed_bound(changed)
select row
end query
println(after_bound[1])
let sorted_zero = collect query
from row in [1,2]
where accepted(row)
order by keyed(row)
take 0
select projected(row)
end query
println(sorted_zero.len())
let huge_skip = collect query
from row in [1,2]
skip 9223372036854775807
select row
end query
println(huge_skip.len())
return 0
end fn
