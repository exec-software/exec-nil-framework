import nil.std.db
import nil.std.system

fn require(passed: bool, label: text)
    if not passed then panic(label) end if
end fn

fn main() -> i32
    require(db_available("postgresql"), "PostgreSQL provider available")
    let db: i64 = db_open("postgresql", arg(0), 3000).unwrap()
    let empty_text: array<text> = []
    let empty_flags: array<bool> = []
    let created: i64 = db_execute(db, "CREATE TEMP TABLE samples (name TEXT, optional TEXT)", empty_text, empty_flags, 3000).unwrap()
    require(db_release(created).unwrap() == 1, "release create")
    let literal: text = "O'Reilly; DROP TABLE samples; -- 世界"
    let inserted: i64 = db_execute(db, "INSERT INTO samples VALUES ($1, $2)", [literal, ""], [false, true], 3000).unwrap()
    require(db_affected(inserted).unwrap() == 1, "affected rows")
    require(db_release(inserted).unwrap() == 1, "release insert")
    let selected: i64 = db_execute(db, "SELECT name, optional, '' AS blank FROM samples", empty_text, empty_flags, 3000).unwrap()
    require(db_rows(selected).unwrap() == 1, "row count")
    require(db_columns(selected).unwrap() == 3, "column count")
    require(db_affected(selected).unwrap() == 0, "select affected rows")
    require(db_column_name(selected, 0).unwrap() == "name", "column name")
    require(db_cell(selected, 0, 0).unwrap() == literal, "parameter binding")
    require(db_is_null(selected, 0, 1).unwrap() == 1, "NULL indicator")
    require(db_is_null(selected, 0, 2).unwrap() == 0, "empty text not NULL")
    require(db_cell(selected, 0, 2).unwrap() == "", "empty text value")
    require(db_cell(selected, 0, 1).is_error(), "NULL text read checked")
    require(db_cell(selected, -1, 0).is_error(), "negative row checked")
    require(db_cell(selected, 1, 0).is_error(), "row upper bound checked")
    require(db_cell(selected, 0, 3).is_error(), "column upper bound checked")
    require(db_execute(db, "SELECT $1", ["x"], empty_flags, 3000).is_error(), "binding lengths checked")
    require(db_execute(db, "SELECT $1", empty_text, empty_flags, 3000).is_error(), "SQL placeholder count checked")
    require(db_execute(db, "INSERT INTO samples VALUES ('bad', NULL); SELECT 1", empty_text, empty_flags, 3000).is_error(), "multiple statements rejected")
    let count: i64 = db_execute(db, "SELECT count(*) FROM samples", empty_text, empty_flags, 3000).unwrap()
    require(db_cell(count, 0, 0).unwrap() == "1", "rejected SQL preserved database")
    require(db_release(count).unwrap() == 1, "release count")
    require(db_release(selected).unwrap() == 1, "release selected")
    require(db_release(selected).is_error(), "duplicate release checked")
    require(db_execute(db, "SELECT pg_sleep(2)", empty_text, empty_flags, 20).is_error(), "execution timeout")
    require(db_close(db).is_error(), "timeout closes connection")
    let fresh: i64 = db_open("postgresql", arg(0), 3000).unwrap()
    let last: i64 = db_execute(fresh, "SELECT 'usable'", empty_text, empty_flags, 3000).unwrap()
    require(db_close(fresh).unwrap() == 1, "close")
    require(db_rows(last).is_error(), "close invalidates results")
    require(db_close(fresh).is_error(), "duplicate close checked")
    println("database PostgreSQL checked runtime passed")
    return 0
end fn
