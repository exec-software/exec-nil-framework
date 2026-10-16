@pragma once

# Experimental: the libpq provider is optional and is not present in the
# current SDK build. This source is retained for capability-gated promotion.

module nil.std.db

import nil.std.core

# =============================================================================
# Database Connection
# =============================================================================

# Opaque handle to a PostgreSQL connection
pub struct db_connection
    _handle: i64  # Opaque pointer from libpq
end struct

# Result of a database query
pub struct db_result
    _handle: i64  # Opaque PGresult pointer
end struct

impl db_connection
    # Connect to a PostgreSQL database
    # @param conninfo - PostgreSQL connection string (e.g., "host=localhost dbname=mydb user=myuser")
    # @return db_connection on success
    @intrinsic("db.connect")
    pub fn connect(conninfo: text) -> result<db_connection, text>
    end fn

    # Connect using individual parameters
    # @return db_connection on success
    pub fn connect_with(
        host: text,
        port: i32,
        dbname: text,
        user: text,
        password: text
    ) -> result<db_connection, text>
        let conninfo: text =
            "host=" + host +
            " port=" + Convert.i32_to_text(port) +
            " dbname=" + dbname +
            " user=" + user +
            " password=" + password
        return self.connect(conninfo)
    end fn

    # Check if connection is valid
    @intrinsic("db.is_valid")
    pub fn is_valid(self) -> bool
    end fn

    # Get connection status message
    @intrinsic("db.status_message")
    pub fn status_message(self) -> text
    end fn

    # Close the connection
    @intrinsic("db.close")
    pub fn close(mut self)
    end fn

    # Execute a query (simple, no parameters)
    # @return db_result containing rows
    @intrinsic("db.exec")
    pub fn exec(self, query: text) -> result<db_result, text>
    end fn

    # Execute a parameterized query
    # @param query - SQL with $1, $2, etc. placeholders
    # @param params - Array of parameter values
    @intrinsic("db.exec_params")
    pub fn exec_params(self, query: text, params: vec<text>) -> result<db_result, text>
    end fn

    # Begin a transaction
    pub fn begin(self) -> result<db_result, text>
        return self.exec("BEGIN")
    end fn

    # Commit a transaction
    pub fn commit(self) -> result<db_result, text>
        return self.exec("COMMIT")
    end fn

    # Rollback a transaction
    pub fn rollback(self) -> result<db_result, text>
        return self.exec("ROLLBACK")
    end fn

    # Escape a string literal for safe inclusion in SQL
    @intrinsic("db.escape_literal")
    pub fn escape_literal(self, s: text) -> text
    end fn

    # Escape an identifier (table name, column name)
    @intrinsic("db.escape_identifier")
    pub fn escape_identifier(self, s: text) -> text
    end fn
end impl

# =============================================================================
# Database Result
# =============================================================================

impl db_result
    # Check if result is valid (not null)
    pub fn is_valid(self) -> bool
        return self._handle != 0
    end fn

    # Get number of rows affected
    @intrinsic("db.affected_rows")
    pub fn affected_rows(self) -> i64
    end fn

    # Get number of tuples (rows)
    @intrinsic("db.ntuples")
    pub fn ntuples(self) -> i32
    end fn

    # Get number of fields (columns)
    @intrinsic("db.nfields")
    pub fn nfields(self) -> i32
    end fn

    # Get field name by index
    @intrinsic("db.fname")
    pub fn fname(self, field_num: i32) -> text
    end fn

    # Get field type OID
    @intrinsic("db.ftype")
    pub fn ftype(self, field_num: i32) -> i32
    end fn

    # Get cell value as text
    @intrinsic("db.get_value")
    pub fn get_value(self, row_num: i32, field_num: i32) -> text
    end fn

    # Get cell value as text (by field name)
    pub fn get(self, row_num: i32, field_name: text) -> text
        let num_fields: i32 = self.nfields()
        let mut i: i32 = 0
        while i < num_fields
            if self.fname(i) == field_name then
                return self.get_value(row_num, i)
            end if
            i = i + 1
        end while
        return ""
    end fn

    # Check if cell is NULL
    @intrinsic("db.get_is_null")
    pub fn get_is_null(self, row_num: i32, field_num: i32) -> bool
    end fn

    # Get result status
    @intrinsic("db.res_status")
    pub fn res_status(self) -> text
    end fn

    # Get error message
    @intrinsic("db.error_message")
    pub fn error_message(self) -> text
    end fn

    # Clear result (free memory)
    @intrinsic("db.clear")
    pub fn clear(mut self)
    end fn
end impl

# =============================================================================
# Query Builder (parameterized queries)
# =============================================================================

# Represents a parameterized query
pub struct db_query
    sql: text
    params: vec<text>
end struct

impl db_query
    # Create a new query
    pub fn new(sql: text) -> db_query
        return db_query {
            sql: sql,
            params: vec.new()
        }
    end fn

    # Add a parameter ($1, $2, etc.)
    pub fn param(mut self, value: text) -> db_query
        self.params.push(value)
        return self
    end fn

    # Add an integer parameter
    pub fn param_i(mut self, value: i64) -> db_query
        self.params.push(Convert.i64_to_text(value))
        return self
    end fn

    # Add a boolean parameter
    pub fn param_b(mut self, value: bool) -> db_query
        if value then
            self.params.push("true")
        else
            self.params.push("false")
        end if
        return self
    end fn

    # Execute the query on a connection
    pub fn execute(self, conn: db_connection) -> result<db_result, text>
        return conn.exec_params(self.sql, self.params)
    end fn
end impl

# =============================================================================
# Convenience Functions
# =============================================================================

# Execute a simple one-off query
pub fn execute(conn: db_connection, sql: text) -> result<db_result, text>
    return conn.exec(sql)
end fn

# Execute a parameterized query
pub fn execute_params(conn: db_connection, sql: text, params: vec<text>) -> result<db_result, text>
    return conn.exec_params(sql, params)
end fn
