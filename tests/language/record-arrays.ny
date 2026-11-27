import nil.std.collections

struct ArrayCounter
    value: i32
end struct

struct ArrayPacket<T>
    item: T
    label: text
    values: array<i32>
    link: ref<ArrayCounter>
end struct

struct EmptyArrayBox<T>
    value: T
end struct

struct AliasArrayBox<T>
    value: T
end struct

type ArrayBoxAlias<T> = AliasArrayBox<T>

struct ArrayAliasHolder
    item: AliasArrayBox<i32>
end struct

fn read_array_alias(item: AliasArrayBox<i32>) -> i32
    return item.value
end fn

fn require_record_array(condition: bool) -> void
    if not condition then panic("record array contract failed") end if
end fn

fn record_array_identity<T>(items: array<T>) -> array<T>
    return items
end fn

fn fresh_record_packets() -> array<ArrayPacket<ArrayCounter>>
    let result: array<ArrayPacket<ArrayCounter>> = []
    let mut index: i32 = 0
    while index < 257
        let counter = ArrayCounter { value: index }
        result.push(ArrayPacket<ArrayCounter> {
            item: counter,
            label: $"owned-{index}",
            values: [index, index + 1],
            link: ref counter
        })
        index = index + 1
    end while
    return result
end fn

fn main() -> i32
    let original = ArrayCounter { value: 3 }
    let items: array<ArrayCounter> = [original]
    let alias = record_array_identity<ArrayCounter>(items)
    let retained = items[0]
    let separate: array<ArrayCounter> = [items[0]]
    original.value = 5
    require_record_array(items[0].value == 5)
    retained.value = 7
    require_record_array(original.value == 7)
    items[0].value = 11
    require_record_array(retained.value == 11 and separate[0].value == 11)
    items[0] = ArrayCounter { value: 13 }
    require_record_array(alias[0].value == 13 and retained.value == 11)
    retained.value = 17
    require_record_array(separate[0].value == 17 and items[0].value == 13)
    alias.push(original)
    require_record_array(items.len() == 2 and items[1].value == 17)
    # Invalid writes retain the existing array no-op contract.
    items[-1] = ArrayCounter { value: 91 }
    items[2] = ArrayCounter { value: 92 }
    require_record_array(items.len() == 2 and items[0].value == 13)
    println("record-array-alias-replacement-ok")

    let empty: array<ArrayCounter> = []
    let independent: array<ArrayCounter> = []
    empty.reserve(32)
    require_record_array(empty.capacity() >= 32 and empty.len() == 0)
    empty.push(original)
    require_record_array(independent.len() == 0 and empty[0].value == 17)
    # This specialization exists only in an empty local array: no constructor,
    # parameter or return type can materialize its nominal storage schema.
    let boxes: array<EmptyArrayBox<i32>> = []
    boxes.reserve(8)
    require_record_array(boxes.len() == 0 and boxes.capacity() >= 8)
    let filled = new_with_value<ArrayCounter>(3, original)
    filled[0].value = 19
    require_record_array(filled[2].value == 19 and original.value == 19)
    array_set<ArrayCounter>(filled, 1, ArrayCounter { value: 23 })
    require_record_array(get<ArrayCounter>(filled, 1).value == 23)
    require_record_array(length<ArrayCounter>(filled) == 3)
    array_set<ArrayCounter>(filled, -1, original)
    array_set<ArrayCounter>(filled, 3, original)
    require_record_array(filled[1].value == 23)
    require_record_array(try_copy_range<ArrayCounter>(filled, 0, filled, 1, 2))
    require_record_array(filled[0].value == 19 and filled[1].value == 19 and filled[2].value == 23)
    require_record_array(try_copy_range<ArrayCounter>(filled, 1, filled, 0, 2))
    require_record_array(filled[0].value == 19 and filled[1].value == 23)
    require_record_array(not try_copy_range<ArrayCounter>(filled, 0, filled, 2, 2))
    println("record-array-capacity-intrinsics-copy-ok")

    # Alias constructors must retain the underlying schema when nested in
    # intrinsic arguments, ordinary calls, and another record's field.
    let alias_filled = new_with_value<ArrayBoxAlias<i32>>(2, ArrayBoxAlias<i32> { value: 61 })
    array_set<ArrayBoxAlias<i32>>(alias_filled, 1, ArrayBoxAlias<i32> { value: 67 })
    require_record_array(alias_filled[0].value == 61 and alias_filled[1].value == 67)
    require_record_array(read_array_alias(ArrayBoxAlias<i32> { value: 71 }) == 71)
    let alias_holder = ArrayAliasHolder { item: ArrayBoxAlias<i32> { value: 73 } }
    require_record_array(alias_holder.item.value == 73)
    let alias_wide = new_with_value<ArrayBoxAlias<i64>>(1, ArrayBoxAlias<i64> { value: 4294967296 })
    let alias_fraction = new_with_value<ArrayBoxAlias<f64>>(1, ArrayBoxAlias<f64> { value: 1.5 })
    require_record_array(alias_wide[0].value == 4294967296 and alias_fraction[0].value == 1.5)
    println("record-array-alias-constructors-ok")

    let child = ArrayCounter { value: 29 }
    let numbers = [31, 37]
    let packet = ArrayPacket<ArrayCounter> {
        item: child, label: "initial", values: numbers, link: ref child
    }
    let packets = record_array_identity<ArrayPacket<ArrayCounter>>([packet])
    let saved = packets[0]
    saved.item.value = 41
    require_record_array(child.value == 41)
    packets[0].link.value = 43
    require_record_array(child.value == 43 and saved.item.value == 43)
    packets[0].values[1] = 47
    require_record_array(numbers[1] == 47)
    packets[0].label = "changed"
    require_record_array(packet.label == "changed")
    packets[0] = ArrayPacket<ArrayCounter> {
        item: ArrayCounter { value: 53 }, label: "replacement", values: [59], link: ref child
    }
    require_record_array(saved.label == "changed" and packets[0].label == "replacement")
    require_record_array(packets[0].item.value == 53 and packets[0].link.value == 43)
    println("record-array-nested-generic-fields-ok")

    let returned_items = fresh_record_packets()
    let held = returned_items[128]
    require_record_array(returned_items.len() == 257)
    require_record_array(returned_items[0].label == "owned-0" and returned_items[256].label == "owned-256")
    require_record_array(returned_items[256].values[1] == 257)
    returned_items[128] = packet
    require_record_array(held.label == "owned-128" and held.link.value == 128)
    require_record_array(held.values[1] == 129)
    println("record-array-owned-return-growth-ok")
    return 0
end fn
