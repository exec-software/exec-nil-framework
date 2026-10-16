@pragma once

module nil.std.db

import nil.std.core
import nil.std.collections

fn decimal_i32(value: i32) -> text
    if value == 0 then return "0" end if
    let digits: text = "0123456789"
    let mut remaining: i64 = value
    let negative: bool = remaining < 0
    if negative then remaining = 0 - remaining end if
    let mut result: text = ""
    while remaining > 0
        let digit: i32 = (remaining % 10) as i32
        result = digits.substr(digit, 1) + result
        remaining = remaining / 10
    end while
    if negative then return "-" + result end if
    return result
end fn

# A transport-independent, parameterized SQL statement. Values are kept out
# of the SQL text and retain their positional order for a future driver.
pub struct sql_query
    pub sql: text
    pub params: vec<text>
end struct

impl sql_query
    pub fn parameter_count(self) -> i32
        return self.params.length()
    end fn

    pub fn parameter(self, index: i32) -> Option<text>
        return self.params.get(index)
    end fn
end impl

# Builders accept trusted schema identifiers and SQL join fragments. Runtime
# values always become PostgreSQL-style positional parameters ($1, $2, ...).
pub struct select_query
    _table: text
    _columns: vec<text>
    _where_clauses: vec<text>
    _params: vec<text>
    _order_by: vec<text>
    _joins: vec<text>
    _limit_count: Option<i32>
    _offset_count: Option<i32>
end struct

impl select_query
    pub fn from(table: text) -> select_query
        return select_query {
            _table: table,
            _columns: vec<text>.new(),
            _where_clauses: vec<text>.new(),
            _params: vec<text>.new(),
            _order_by: vec<text>.new(),
            _joins: vec<text>.new(),
            _limit_count: Option.None,
            _offset_count: Option.None
        }
    end fn

    pub fn column(mut self, column: text) -> select_query
        self._columns.push(column)
        return self
    end fn

    pub fn columns(mut self, columns: array<text>) -> select_query
        let mut i: i32 = 0
        while i < columns.len()
            self._columns.push(columns[i])
            i = i + 1
        end while
        return self
    end fn

    pub fn where_eq(mut self, column: text, value: text) -> select_query
        return self.where_value(column, "=", value)
    end fn

    pub fn where_ne(mut self, column: text, value: text) -> select_query
        return self.where_value(column, "<>", value)
    end fn

    pub fn where_gt(mut self, column: text, value: text) -> select_query
        return self.where_value(column, ">", value)
    end fn

    pub fn where_lt(mut self, column: text, value: text) -> select_query
        return self.where_value(column, "<", value)
    end fn

    pub fn where_like(mut self, column: text, value: text) -> select_query
        return self.where_value(column, "LIKE", value)
    end fn

    fn where_value(mut self, column: text, operator: text, value: text) -> select_query
        let position: i32 = self._params.length() + 1
        self._where_clauses.push(
            column + " " + operator + " $" + decimal_i32(position)
        )
        self._params.push(value)
        return self
    end fn

    pub fn where_null(mut self, column: text) -> select_query
        self._where_clauses.push(column + " IS NULL")
        return self
    end fn

    pub fn where_not_null(mut self, column: text) -> select_query
        self._where_clauses.push(column + " IS NOT NULL")
        return self
    end fn

    pub fn order_by(mut self, column: text, descending: bool) -> select_query
        if descending then
            self._order_by.push(column + " DESC")
        else
            self._order_by.push(column + " ASC")
        end if
        return self
    end fn

    pub fn limit(mut self, count: i32) -> select_query
        self._limit_count = Option.Some(count)
        return self
    end fn

    pub fn offset(mut self, count: i32) -> select_query
        self._offset_count = Option.Some(count)
        return self
    end fn

    pub fn join(mut self, table: text, on_clause: text) -> select_query
        self._joins.push("INNER JOIN " + table + " ON " + on_clause)
        return self
    end fn

    pub fn left_join(mut self, table: text, on_clause: text) -> select_query
        self._joins.push("LEFT JOIN " + table + " ON " + on_clause)
        return self
    end fn

    pub fn build(self) -> sql_query
        let mut sql: text = "SELECT "
        if self._columns.is_empty() then
            sql = sql + "*"
        else
            let mut i: i32 = 0
            while i < self._columns.length()
                if i > 0 then sql = sql + ", " end if
                sql = sql + self._columns.get(i).unwrap()
                i = i + 1
            end while
        end if

        sql = sql + " FROM " + self._table

        let mut i: i32 = 0
        while i < self._joins.length()
            sql = sql + " " + self._joins.get(i).unwrap()
            i = i + 1
        end while

        if not self._where_clauses.is_empty() then
            sql = sql + " WHERE "
            i = 0
            while i < self._where_clauses.length()
                if i > 0 then sql = sql + " AND " end if
                sql = sql + self._where_clauses.get(i).unwrap()
                i = i + 1
            end while
        end if

        if not self._order_by.is_empty() then
            sql = sql + " ORDER BY "
            i = 0
            while i < self._order_by.length()
                if i > 0 then sql = sql + ", " end if
                sql = sql + self._order_by.get(i).unwrap()
                i = i + 1
            end while
        end if

        match self._limit_count
            Option.Some(count) -> sql = sql + " LIMIT " + decimal_i32(count)
            Option.None -> {}
        end match
        match self._offset_count
            Option.Some(count) -> sql = sql + " OFFSET " + decimal_i32(count)
            Option.None -> {}
        end match

        return sql_query { sql: sql, params: self._params }
    end fn

    pub fn to_sql(self) -> text
        return self.build().sql
    end fn
end impl

pub struct insert_query
    _table: text
    _columns: vec<text>
    _params: vec<text>
end struct

impl insert_query
    pub fn into(table: text) -> insert_query
        return insert_query {
            _table: table,
            _columns: vec<text>.new(),
            _params: vec<text>.new()
        }
    end fn

    pub fn value(mut self, column: text, value: text) -> insert_query
        self._columns.push(column)
        self._params.push(value)
        return self
    end fn

    pub fn build(self) -> sql_query
        if self._columns.is_empty() then
            return sql_query {
                sql: "INSERT INTO " + self._table + " DEFAULT VALUES",
                params: self._params
            }
        end if

        let mut sql: text = "INSERT INTO " + self._table + " ("
        let mut i: i32 = 0
        while i < self._columns.length()
            if i > 0 then sql = sql + ", " end if
            sql = sql + self._columns.get(i).unwrap()
            i = i + 1
        end while
        sql = sql + ") VALUES ("
        i = 0
        while i < self._params.length()
            if i > 0 then sql = sql + ", " end if
            sql = sql + "$" + decimal_i32(i + 1)
            i = i + 1
        end while
        sql = sql + ")"
        return sql_query { sql: sql, params: self._params }
    end fn
end impl

pub struct update_query
    _table: text
    _set_clauses: vec<text>
    _where_clauses: vec<text>
    _params: vec<text>
end struct

impl update_query
    pub fn table(table: text) -> update_query
        return update_query {
            _table: table,
            _set_clauses: vec<text>.new(),
            _where_clauses: vec<text>.new(),
            _params: vec<text>.new()
        }
    end fn

    pub fn set(mut self, column: text, value: text) -> update_query
        let position: i32 = self._params.length() + 1
        self._set_clauses.push(column + " = $" + decimal_i32(position))
        self._params.push(value)
        return self
    end fn

    pub fn where_eq(mut self, column: text, value: text) -> update_query
        let position: i32 = self._params.length() + 1
        self._where_clauses.push(column + " = $" + decimal_i32(position))
        self._params.push(value)
        return self
    end fn

    pub fn build(self) -> sql_query
        let mut sql: text = "UPDATE " + self._table + " SET "
        let mut i: i32 = 0
        while i < self._set_clauses.length()
            if i > 0 then sql = sql + ", " end if
            sql = sql + self._set_clauses.get(i).unwrap()
            i = i + 1
        end while
        if not self._where_clauses.is_empty() then
            sql = sql + " WHERE "
            i = 0
            while i < self._where_clauses.length()
                if i > 0 then sql = sql + " AND " end if
                sql = sql + self._where_clauses.get(i).unwrap()
                i = i + 1
            end while
        end if
        return sql_query { sql: sql, params: self._params }
    end fn
end impl

pub struct delete_query
    _table: text
    _where_clauses: vec<text>
    _params: vec<text>
end struct

impl delete_query
    pub fn from(table: text) -> delete_query
        return delete_query {
            _table: table,
            _where_clauses: vec<text>.new(),
            _params: vec<text>.new()
        }
    end fn

    pub fn where_eq(mut self, column: text, value: text) -> delete_query
        let position: i32 = self._params.length() + 1
        self._where_clauses.push(column + " = $" + decimal_i32(position))
        self._params.push(value)
        return self
    end fn

    pub fn build(self) -> sql_query
        let mut sql: text = "DELETE FROM " + self._table
        if not self._where_clauses.is_empty() then
            sql = sql + " WHERE "
            let mut i: i32 = 0
            while i < self._where_clauses.length()
                if i > 0 then sql = sql + " AND " end if
                sql = sql + self._where_clauses.get(i).unwrap()
                i = i + 1
            end while
        end if
        return sql_query { sql: sql, params: self._params }
    end fn
end impl
