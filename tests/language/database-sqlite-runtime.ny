import nil.std.db
import nil.std.collections

fn require(passed: bool, label: text)
    if not passed then panic(label) end if
end fn

fn main() -> i32
    require(db_available("sqlite"), "SQLite provider available")
    require(not db_available("unknown"), "unknown provider unavailable")
    require(db_open("unknown", ":memory:", 1000).is_error(), "unsupported provider checked")
    require(db_open("sqlite", ":memory:", 0).is_error(), "timeout checked")
    let db: i64 = db_open("sqlite", ":memory:", 1000).unwrap()
    let empty_text: array<text> = []
    let empty_flags: array<bool> = []
    let created: i64 = db_execute(db, "CREATE TABLE samples (name TEXT, optional TEXT)", empty_text, empty_flags, 1000).unwrap()
    require(db_affected(created).unwrap() == 0, "DDL count")
    require(db_release(created).unwrap() == 1, "release create")
    let literal: text = "O'Reilly; DROP TABLE samples; --"
    let inserted: i64 = db_execute(db, "INSERT INTO samples VALUES ($1, $2)", [literal, ""], [false, true], 1000).unwrap()
    require(db_affected(inserted).unwrap() == 1, "affected rows")
    require(db_release(inserted).unwrap() == 1, "release insert")
    let selected: i64 = db_execute(db, "SELECT name, optional, '' AS blank FROM samples; -- allowed comment", empty_text, empty_flags, 1000).unwrap()
    require(db_rows(selected).unwrap() == 1, "row count")
    require(db_columns(selected).unwrap() == 3, "column count")
    require(db_affected(selected).unwrap() == 0, "select affected rows")
    require(db_column_name(selected, 0).unwrap() == "name", "column name")
    require(db_cell(selected, 0, 0).unwrap() == literal, "parameter binding does not interpolate SQL")
    require(db_is_null(selected, 0, 1).unwrap() == 1, "NULL indicator")
    require(db_is_null(selected, 0, 2).unwrap() == 0, "empty text not NULL")
    require(db_cell(selected, 0, 2).unwrap() == "", "empty text value")
    require(db_cell(selected, 0, 1).is_error(), "NULL text read checked")
    require(db_cell(selected, -1, 0).is_error(), "negative row checked")
    require(db_cell(selected, 1, 0).is_error(), "row upper bound checked")
    require(db_cell(selected, 0, 3).is_error(), "column upper bound checked")
    require(db_column_name(selected, -1).is_error(), "column lower bound checked")
    require(db_execute(db, "SELECT $1", ["x"], empty_flags, 1000).is_error(), "binding lengths checked")
    let many_values: array<text> = new_with_value<text>(257, "x")
    let many_flags: array<bool> = new_with_value<bool>(257, false)
    require(db_execute(db, "SELECT $1", many_values, many_flags, 1000).is_error(), "parameter ceiling is a checked error")
    require(db_execute(db, "SELECT $1", empty_text, empty_flags, 1000).is_error(), "SQL placeholder count checked")
    require(db_execute(db, "INSERT INTO samples VALUES ('bad', NULL); SELECT 1", empty_text, empty_flags, 1000).is_error(), "multiple statements rejected before effects")
    let count: i64 = db_execute(db, "SELECT count(*) FROM samples", empty_text, empty_flags, 1000).unwrap()
    require(db_cell(count, 0, 0).unwrap() == "1", "rejected SQL preserved database")
    require(db_release(count).unwrap() == 1, "release count")
    require(db_execute(db, "SELECT x'00'", empty_text, empty_flags, 1000).is_error(), "binary cell rejected")
    require(db_execute(db, "SELECT char(0)", empty_text, empty_flags, 1000).is_error(), "NUL cell rejected")
    require(db_execute(db, "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n WHERE x<10001) SELECT x FROM n", empty_text, empty_flags, 1000).is_error(), "row bound")
    require(db_execute(db, "WITH RECURSIVE n(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM n) SELECT sum(x) FROM n", empty_text, empty_flags, 1).is_error(), "CPU timeout")
    require(db_release(selected).unwrap() == 1, "release selected")
    require(db_release(selected).is_error(), "duplicate release checked")
    require(db_rows(selected).is_error(), "released handle checked")
    let last: i64 = db_execute(db, "SELECT 'still usable'", empty_text, empty_flags, 1000).unwrap()
    require(db_close(db).unwrap() == 1, "close")
    require(db_rows(last).is_error(), "close invalidates results")
    require(db_close(db).is_error(), "duplicate close checked")
    require(db_execute(db, "SELECT 1", empty_text, empty_flags, 1000).is_error(), "closed connection checked")
    println("database SQLite checked runtime passed")
    return 0
end fn
