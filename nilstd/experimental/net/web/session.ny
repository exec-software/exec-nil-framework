@pragma once
# Experimental: NyWeb depends on the unavailable HTTP-server provider.
module nil.std.net

import nil.std.core

# =============================================================================
# Session Management
# Server-side sessions keyed by a cookie token.
# Sessions are stored in the configured session store (memory, Redis, DB).
# =============================================================================

pub struct session
    id: text
    user_id: Option<text>
    tenant_id: Option<text>
    data: vec<session_entry>
    created_at: i64
    expires_at: i64
    is_dirty: bool          # True if session needs to be saved
end struct

pub struct session_entry
    key: text
    value: text
end struct

impl session
    pub fn new(id: text, ttl_seconds: i64) -> session
        let now = ml_unix_timestamp()
        return session {
            id: id,
            user_id: Option.None,
            tenant_id: Option.None,
            data: vec.new(),
            created_at: now,
            expires_at: now + ttl_seconds,
            is_dirty: false
        }
    end fn

    # Set a session value
    pub fn set(mut self, key: text, value: text)
        let mut i: i32 = 0
        while i < self.data.len()
            let entry = self.data.get(i).unwrap()
            if entry.key == key then
                self.data.set(i, session_entry { key: key, value: value })
                self.is_dirty = true
                return
            end if
            i = i + 1
        end while
        self.data.push(session_entry { key: key, value: value })
        self.is_dirty = true
    end fn

    # Get a session value
    pub fn get(self, key: text) -> Option<text>
        for entry in self.data
            if entry.key == key then
                return Option.Some(entry.value)
            end if
        end for
        return Option.None
    end fn

    # Remove a session value
    pub fn remove(mut self, key: text)
        let mut new_data: vec<session_entry> = vec.new()
        for entry in self.data
            if entry.key != key then
                new_data.push(entry)
            end if
        end for
        self.data = new_data
        self.is_dirty = true
    end fn

    # Whether this session has an authenticated user
    pub fn is_authenticated(self) -> bool
        return self.user_id.is_some()
    end fn

    # Sign in: bind a user_id and tenant_id to this session
    pub fn sign_in(mut self, user_id: text, tenant_id: text)
        self.user_id = Option.Some(user_id)
        self.tenant_id = Option.Some(tenant_id)
        self.is_dirty = true
    end fn

    # Sign out: clear authentication from this session
    pub fn sign_out(mut self)
        self.user_id = Option.None
        self.tenant_id = Option.None
        self.is_dirty = true
    end fn

    # Whether this session has expired
    pub fn is_expired(self) -> bool
        return ml_unix_timestamp() > self.expires_at
    end fn
end impl

# =============================================================================
# Session Store interface
# Concrete implementations: memory (dev), PostgreSQL (prod)
# =============================================================================

pub trait ISessionStore
    fn load(self, session_id: text) -> Result<session, text>
    fn save(mut self, session: session) -> Result<unit, text>
    fn delete(mut self, session_id: text) -> Result<unit, text>
    fn generate_id(self) -> text
end trait

# In-memory session store (development only — not suitable for multi-instance)
pub struct memory_session_store
    sessions: vec<session>
end struct

impl memory_session_store
    pub fn new() -> memory_session_store
        return memory_session_store { sessions: vec.new() }
    end fn
end impl

# =============================================================================
# Native bindings
# =============================================================================

@interop("ml_nyweb")
pub fn ml_unix_timestamp() -> i64
end fn

@interop("ml_nyweb")
pub fn ml_generate_session_id() -> text
end fn
