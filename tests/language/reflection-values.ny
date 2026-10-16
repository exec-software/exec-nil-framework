import nil.std.reflection
import ny.lang.core

fn main() -> i32
    let info = declaration_info {
        kind: "function", name: "temp_render", qualified_name: "scene::temp_render",
        type_name: "i32", source_path: "scene.matrix", line: 7, parent_id: -1,
        is_public: true, semantic_checked: true
    }
    let annotation = annotation_info { owner_id: 0, name: "ai_context", arguments: "draw a frame", line: 6 }
    println(is_function(info))
    println(is_record(info))
    println(is_public_api(info))
    println(has_checked_semantics(info))
    println(annotation.name)
    println(json_text(""))
    println(json_text("quote\" slash\\ tab\t newline\n form\f vertical\v return\r café"))
    println(diagnostic_json("EXAMPLE001", "warning", "Rename temporary API", info.source_path, info.line))
    println("ny.lang.reflection is still literal text")
    return 0
end fn
