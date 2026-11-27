import nil.std.collections

struct ReferenceCounter<T>
    value: T
end struct

impl<T> ReferenceCounter<T>
    fn read(self: ref<Self>) -> T
        return self.value
    end fn
end impl

struct ReferenceCounterHolder
    item: ReferenceCounter<i32>
end struct

fn counted_counter(calls: ref<ReferenceCounter<i32>>) -> ReferenceCounter<i32>
    calls.value = calls.value + 1
    return ReferenceCounter<i32> { value: 71 }
end fn

fn counted_holder(calls: ref<ReferenceCounter<i32>>) -> ReferenceCounterHolder
    calls.value = calls.value + 1
    return ReferenceCounterHolder { item: ReferenceCounter<i32> { value: 73 } }
end fn

class ArrayReader
    value: i32
    pub virtual fn read(self) -> i32
        return self.value
    end fn
    pub virtual fn read_ref(self: ref<Self>) -> i32
        return self.value
    end fn
end class

class ArrayOffsetReader inherits ArrayReader
    extra: i32
    pub override fn read(self) -> i32
        return self.value + self.extra
    end fn
    pub override fn read_ref(self: ref<Self>) -> i32
        return self.value + self.extra
    end fn
end class

struct ArrayReaderBundle
    readers: array<ArrayReader>
end struct

fn reader_array(reader: ArrayOffsetReader) -> array<ArrayReader>
    return [reader]
end fn

fn read_first(readers: array<ArrayReader>) -> i32
    return readers[0].read()
end fn

fn escaped_counter() -> array<ref<ReferenceCounter<i32>>>
    let local = ReferenceCounter<i32> { value: 47 }
    return [ref local]
end fn

fn main() -> i32
    let object = ArrayOffsetReader { value: 7, extra: 2 }
    let plain = ArrayReader { value: 11 }
    let readers: array<ArrayReader> = [object, plain]
    let alias = readers
    let retained = readers[0]
    readers[0] = plain
    println(alias[0].read())
    println(retained.read())
    readers[0] = object
    readers.push(object)
    readers.set(1, object)
    readers.reserve(33)
    println(readers[0].read() + readers[1].read() + readers[2].read())
    println(readers.len() == 3 and readers.capacity() >= 33)
    readers[-1] = plain
    readers[3] = plain
    println(readers.len())
    println(read_first([object]))
    println(reader_array(object)[0].read())
    let bundle = ArrayReaderBundle { readers: [object] }
    println(bundle.readers[0].read())

    let filled = new_with_value<ArrayReader>(3, object)
    array_set<ArrayReader>(filled, 1, plain)
    println(get<ArrayReader>(filled, 1).read())
    println(length<ArrayReader>(filled))
    println(try_copy_range<ArrayReader>(filled, 0, filled, 1, 2))
    println(filled[1].read())
    println(filled[2].read())
    let empty: array<ArrayReader> = []
    empty.push(object)
    let rows: array<array<ArrayReader>> = [readers, empty]
    rows[1][0].value = 17
    println(object.read())
    println(retained.read())

    let mut counter = ReferenceCounter<i32> { value: 23 }
    let references: array<ref<ReferenceCounter<i32>>> = [ref counter]
    let saved_reference = references[0]
    counter = ReferenceCounter<i32> { value: 29 }
    saved_reference.value = 31
    println(counter.value)
    println(references[0].value)
    references.push(ref counter)
    references.reserve(32)
    let reference_alias = references
    reference_alias[0] = ref counter
    references.set(1, saved_reference)
    println(references[0].value)
    println(references[1].value)
    let reference_fill = new_with_value<ref<ReferenceCounter<i32>>>(3, saved_reference)
    array_set<ref<ReferenceCounter<i32>>>(reference_fill, 1, ref counter)
    println(get<ref<ReferenceCounter<i32>>>(reference_fill, 1).value)
    println(try_copy_range<ref<ReferenceCounter<i32>>>(reference_fill, 0, reference_fill, 1, 2))
    println(reference_fill[1].value)
    println(reference_fill[2].value)
    let escaped = escaped_counter()
    escaped[0].value = 53
    println(escaped[0].value)
    let reference_rows: array<array<ref<ReferenceCounter<i32>>>> = [references, escaped]
    reference_rows[1][0].value = 59
    println(escaped[0].value)

    let view: ArrayReader = object
    let class_refs: array<ref<ArrayReader>> = [ref view]
    let class_ref_alias = class_refs
    class_ref_alias[0].value = 61
    println(class_refs[0].read_ref())
    let ref_fill = new_with_value<ref<ArrayReader>>(2, ref view)
    println(get<ref<ArrayReader>>(ref_fill, 1).read_ref())
    let empty_refs: array<ref<ReferenceCounter<i64>>> = []
    empty_refs.reserve(17)
    println(empty_refs.len() == 0 and empty_refs.capacity() >= 17)
    let calls = ReferenceCounter<i32> { value: 0 }
    println(counted_counter(ref calls).read())
    println(calls.value)
    println(counted_holder(ref calls).item.read())
    println(calls.value)
    return 0
end fn
