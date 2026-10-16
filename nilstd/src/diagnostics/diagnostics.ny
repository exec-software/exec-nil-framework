@schema
@pragma once

module nil.std.diagnostics

import nil.std.core
import nil.std.datetime.std_time

# =============================================================================
# TimeSpan Struct
# =============================================================================

pub struct TimeSpan
    pub ticks: i64
end struct

impl TimeSpan
    pub fn ticks_per_millisecond() -> i64 return 10000 end fn
    pub fn ticks_per_second() -> i64 return 10000000 end fn
    pub fn ticks_per_minute() -> i64 return 600000000 end fn
    pub fn ticks_per_hour() -> i64 return 36000000000 end fn
    pub fn ticks_per_day() -> i64 return 864000000000 end fn

    pub fn from_ticks(ticks: i64) -> TimeSpan
        return TimeSpan { ticks: ticks }
    end fn

    pub fn total_milliseconds(self: ref<Self>) -> i64
        return self.ticks / TimeSpan.ticks_per_millisecond()
    end fn
end impl

# =============================================================================
# Stopwatch Class
# =============================================================================

pub struct Stopwatch
    pub is_running: bool
    pub elapsed_ticks: i64
    pub start_time: i64
end struct

impl Stopwatch
    pub fn new() -> Stopwatch
        return Stopwatch { is_running: false, elapsed_ticks: 0, start_time: 0 }
    end fn

    pub fn start(self: ref<Self>)
        if not self.is_running then
            self.start_time = get_current_ticks()
            self.is_running = true
        end if
    end fn

    pub fn stop(self: ref<Self>)
        if self.is_running then
            let now = get_current_ticks()
            self.elapsed_ticks = self.elapsed_ticks + (now - self.start_time)
            self.is_running = false
        end if
    end fn

    pub fn elapsed(self: ref<Self>) -> TimeSpan
        return TimeSpan.from_ticks(self.elapsed_ticks)
    end fn
end impl

pub fn get_current_ticks() -> i64
    # TimeSpan ticks are 100 ns. The interpreter wall clock is milliseconds,
    # so retain that public unit while being honest about clock resolution.
    return nil::std::datetime::now() * TimeSpan.ticks_per_millisecond()
end fn
