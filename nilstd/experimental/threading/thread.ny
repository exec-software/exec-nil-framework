@schema
# Experimental design only: thread creation and the ml_concurrency bridge are
# not built or shipped by the SDK.
@pragma once
module nil.std.threading

import nil.std.core

pub struct thread
    id: i64
    name: text
end struct

impl thread
    @intrinsic("thread.spawn")
    pub fn spawn(func: fn()) -> thread
    end fn

    @intrinsic("thread.spawn_named")
    pub fn spawn_named(name: text, func: fn()) -> thread
    end fn

    @intrinsic("thread.join")
    pub fn join(self)
    end fn

    @intrinsic("thread.is_running")
    pub fn is_running(self) -> bool
    end fn

    @intrinsic("thread.current")
    pub fn current() -> thread
    end fn

    pub fn get_id(self) -> i64
        return self.id
    end fn

    pub fn get_name(self) -> text
        return self.name
    end fn
end impl

@interop("ml_concurrency")
pub fn ml_thread_sleep(ms: i64)
end fn

@interop("ml_concurrency")
pub fn ml_thread_yield()
end fn

@interop("ml_concurrency")
pub fn ml_thread_cpu_count() -> i32
end fn

pub fn sleep_ms(ms: i64)
    ml_thread_sleep(ms)
end fn

pub fn sleep_sec(seconds: i32)
    ml_thread_sleep(seconds as i64 * 1000)
end fn

pub fn yield_now()
    ml_thread_yield()
end fn

pub fn cpu_count() -> i32
    return ml_thread_cpu_count()
end fn
