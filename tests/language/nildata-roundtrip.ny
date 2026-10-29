import nil.std.core
import nil.std.nildata

fn require(ok: bool, message: text)
    if not ok then panic(message) end if
end fn

fn main() -> i32
    let source = "nildata 1\nobject\n  name is \"  title\\nend object\\r\\t\\\"\\\\  \"\n  big is 1234567890123456789012345678901234567890\n  decimal is 1.2300e+42\n  items is an array\n    item is 1\n    item is \"1\"\n    item is true\n    item is null\n    item is an array\n      item is 2.50e+3\n    end array\n  end array\nend object\n"
    let document = loads(source).unwrap()
    require(kind(document).unwrap() == "object", "root kind")
    require(size(document).unwrap() == 4, "object size")
    let original = to_json(document).unwrap()
    require(to_json(loads(dumps(document).unwrap()).unwrap()).unwrap() == original, "NILData round trip")
    require(to_json(from_json(original).unwrap()).unwrap() == original, "JSON round trip")
    require(text_value(object_get(document, "name").unwrap().unwrap()).unwrap() == "  title\nend object\r\t\"\\  ", "quoted whitespace and terminator text")
    require(numeric_text(object_get(document, "big").unwrap().unwrap()).unwrap() == "1234567890123456789012345678901234567890", "arbitrary precision integer")
    require(as_i64(object_get(document, "big").unwrap().unwrap()).is_error(), "i64 overflow is explicit")
    require(numeric_text(object_get(document, "decimal").unwrap().unwrap()).unwrap() == "1.2300e+42", "decimal lexical precision")
    require(kind(object_get(document, "decimal").unwrap().unwrap()).unwrap() == "decimal", "decimal differs from integer")
    require(object_key(document, 0).unwrap().unwrap() == "big", "sorted key enumeration")
    require(object_key(document, 4).unwrap().is_none(), "missing key position")
    require(object_key(document, -1).is_error(), "negative key position")
    let items = object_get(document, "items").unwrap().unwrap()
    require(size(items).unwrap() == 5, "array size")
    require(kind(array_get(items, 0).unwrap().unwrap()).unwrap() == "integer", "integer array item")
    require(text_value(array_get(items, 1).unwrap().unwrap()).unwrap() == "1", "numeric text remains text")
    require(bool_value(array_get(items, 2).unwrap().unwrap()).unwrap(), "boolean item")
    require(kind(array_get(items, 3).unwrap().unwrap()).unwrap() == "null", "null item")
    require(numeric_text(array_get(array_get(items, 4).unwrap().unwrap(), 0).unwrap().unwrap()).unwrap() == "2.50e+3", "nested arrays")
    require(array_get(items, 5).unwrap().is_none(), "missing item")
    require(array_get(items, -1).is_error(), "negative item index")
    require(object_get(document, "absent").unwrap().is_none(), "missing member")
    require(not object_has(document, "absent").unwrap(), "missing member predicate")
    println("NILData typed round trips passed")

    let zero = integer_value("-0").unwrap()
    let fractional = decimal_value("-0.000e+2").unwrap()
    require(numeric_text(zero).unwrap() == "-0", "integer signed zero")
    require(numeric_text(fractional).unwrap() == "-0.000e+2", "decimal signed zero")
    require(as_i64(integer_value("-9223372036854775808").unwrap()).unwrap() == -9223372036854775808, "checked i64 minimum")
    require(as_i64(decimal_value("1.0").unwrap()).is_error(), "decimal is not an implicit i64")
    let empty = new_object().unwrap()
    let changed = object_set(empty, "", string_value("  \nend object\n  ").unwrap()).unwrap()
    require(size(empty).unwrap() == 0, "object mutation leaves original empty")
    require(object_key(changed, 0).unwrap().unwrap() == "", "empty key is present")
    require(text_value(object_get(changed, "").unwrap().unwrap()).unwrap() == "  \nend object\n  ", "empty key round trip")
    let escaped_key = "quote\"slash\\line\n"
    let replaced = object_set(object_set(changed, escaped_key, zero).unwrap(), escaped_key, fractional).unwrap()
    require(size(replaced).unwrap() == 2, "object_set replaces one existing key")
    require(numeric_text(object_get(replaced, escaped_key).unwrap().unwrap()).unwrap() == "-0.000e+2", "key escaping and replacement")
    let first = new_array().unwrap()
    let second = array_append(first, null_value().unwrap()).unwrap()
    let third = array_append(second, boolean_value(false).unwrap()).unwrap()
    let fourth = array_append(third, replaced).unwrap()
    require(size(first).unwrap() == 0 and size(second).unwrap() == 1 and size(fourth).unwrap() == 3, "array updates are functional")
    require(not bool_value(array_get(third, 1).unwrap().unwrap()).unwrap(), "false stays boolean")
    require(to_json(array_get(fourth, 2).unwrap().unwrap()).unwrap() == to_json(replaced).unwrap(), "array object member")
    require(to_json(loads(dumps(fourth).unwrap()).unwrap()).unwrap() == to_json(fourth).unwrap(), "functional nested value round trip")
    let edited = array_set(third, 0, integer_value("7").unwrap()).unwrap()
    require(as_i64(array_get(edited, 0).unwrap().unwrap()).unwrap() == 7, "array element replacement")
    require(kind(array_get(third, 0).unwrap().unwrap()).unwrap() == "null", "array replacement preserves original")
    require(size(array_remove(edited, 0).unwrap()).unwrap() == 1, "array element removal")
    require(size(object_remove(replaced, "").unwrap()).unwrap() == 1, "object member removal")
    require(to_json(object_remove(replaced, "absent").unwrap()).unwrap() == to_json(replaced).unwrap(), "absent member removal")
    # Cross native collection thresholds and repeated VM collections while
    # retaining the original document and an independently extracted child.
    let mut large_text = "x"
    let mut count: i32 = 0
    while count < 14
        large_text = large_text + large_text
        count = count + 1
    end while
    count = 0
    while count < 400
        let transient = string_value(large_text).unwrap()
        require(text_value(transient).unwrap() == large_text, "transient text survives allocation")
        count = count + 1
    end while
    require(to_json(document).unwrap() == original, "document survives collection")
    require(numeric_text(array_get(array_get(items, 4).unwrap().unwrap(), 0).unwrap().unwrap()).unwrap() == "2.50e+3", "extracted child survives collection")
    println("NILData functional construction passed")

    let nul = from_json("\"a\\u0000b\"").unwrap()
    require(text_value(nul).is_error(), "NUL extraction fails explicitly")
    require(to_json(loads(dumps(nul).unwrap()).unwrap()).unwrap() == to_json(nul).unwrap(), "encoded NUL round trip")
    let nul_key = from_json("{\"a\\u0000b\":1}").unwrap()
    require(object_key(nul_key, 0).is_error(), "NUL key extraction fails explicitly")
    require(to_json(loads(dumps(nul_key).unwrap()).unwrap()).unwrap() == to_json(nul_key).unwrap(), "encoded NUL key round trip")
    println("NILData NUL representation passed")

    let invalid = ["", "object\nend object\n", "nildata 2\nnull\n", "nildata 1\nobject\n", "nildata 1\nobject\nx is 1\nx is 2\nend object\n", "nildata 1\narray\nitem is 1\nend object\n", "nildata 1\nnull\ntrue\n", "nildata 1\nobject\nx is 01\nend object\n", "nildata 1\nobject\nx is \"bad\\q\"\nend object\n"]
    for text in invalid
        require(loads(text).is_error(), "malformed NILData rejected")
    end for
    let invalid_json = ["", "{\"x\":1,\"x\":2}", "[1,]", "01", "NaN", "true false"]
    for text in invalid_json
        require(from_json(text).is_error(), "malformed JSON rejected")
    end for
    require(integer_value("1.0").is_error() and integer_value("+1").is_error(), "integer constructors validate JSON lexemes")
    require(decimal_value("1").is_error() and decimal_value("nan").is_error(), "decimal constructors validate JSON lexemes")
    require(text_value(zero).is_error() and bool_value(zero).is_error(), "scalar type errors")
    require(array_get(document, 0).is_error() and object_get(items, "x").is_error(), "container type errors")
    require(array_append(document, zero).is_error() and object_set(items, "x", zero).is_error(), "mutation type errors")
    require(array_set(items, -1, zero).is_error() and array_set(items, 5, zero).is_error(), "replacement bounds")
    require(array_remove(items, -1).is_error() and array_remove(items, 5).is_error(), "removal bounds")
    require(array_set(document, 0, zero).is_error() and object_remove(items, "x").is_error(), "replacement and removal container errors")
    let forged = Value { encoded: "[invalid" }
    require(to_json(forged).is_error() and dumps(forged).is_error(), "public representation is validated")
    require(object_set(document, "bad", forged).is_error() and array_append(items, forged).is_error(), "invalid child values rejected")
    println("NILData errors passed")

    let legacy = "object root\nfield 0\n001\nend 0\nfield truth\ntrue\nend truth\nlist rows\nitem\nhello\nend item\nend rows\nend root\n"
    let imported = import_nydata(legacy).unwrap()
    require(loads(legacy).is_error(), "legacy input requires explicit import")
    require(kind(imported).unwrap() == "object", "numeric key never guesses array")
    require(text_value(object_get(imported, "0").unwrap().unwrap()).unwrap() == "001", "legacy numeric text preserved")
    require(text_value(object_get(imported, "truth").unwrap().unwrap()).unwrap() == "true", "legacy boolean text preserved")
    require(text_value(array_get(object_get(imported, "rows").unwrap().unwrap(), 0).unwrap().unwrap()).unwrap() == "hello", "legacy explicit list")
    let compiler_legacy = import_nydata_compiler("field name\nhello\nend name\nfield 0\n001\nend 0\n").unwrap()
    require(text_value(object_get(compiler_legacy, "name").unwrap().unwrap()).unwrap() == "hello", "compiler legacy stream")
    require(kind(compiler_legacy).unwrap() == "object", "compiler legacy numeric key stays object")
    require(import_nydata("object root\nfield x\ny\nend root\n").is_error(), "malformed legacy rejected")
    println("NILData explicit legacy import passed")
    return 0
end fn
