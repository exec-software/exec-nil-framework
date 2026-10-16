@pragma once

module nil.std.threading

import nil.std.core
import nil.std.threading

# =============================================================================
# Timer - Periodic Timer (mirrors System.Threading.Timer)
# =============================================================================

pub type timer_callback = fn(ref<any>) -> void

pub struct timer
    callback: timer_callback
    state: ref<any>
    due_time: i32
    period: i32
    enabled: bool
end struct

impl timer
    # Initialize a new timer with the specified callback and state
    pub fn new(callback: timer_callback, state: ref<any>, due_time: i32, period: i32) -> timer
        let mut timer = timer {
            callback: callback,
            state: state,
            due_time: due_time,
            period: period,
            enabled: true
        }

        # Schedule the first execution
        if due_time >= 0 then
            thread_pool.queue_user_work_item(|| timer.timer_proc())
        end if

        return timer
    end fn

    # Change the timer interval
    pub fn change(mut self, due_time: i32, period: i32) -> bool
        self.due_time = due_time
        self.period = period

        if due_time >= 0 and not self.enabled then
            self.enabled = true
            thread_pool.queue_user_work_item(|| self.timer_proc())
        end if

        return true
    end fn

    # Dispose of the timer
    pub fn dispose(mut self) -> void
        self.enabled = false
    end fn

    # Internal timer procedure
    fn timer_proc(mut self) -> void
        if not self.enabled then
            return
        end if

        # Wait for due time
        if self.due_time > 0 then
            thread.sleep_ms(self.due_time)
        end if

        # Execute callback
        self.callback(self.state)

        # Schedule next execution if periodic
        if self.period > 0 then
            self.due_time = self.period
            thread_pool.queue_user_work_item(|| self.timer_proc())
        else
            self.enabled = false
        end if
    end fn
end impl

# =============================================================================
# TimerHelper - Static helper functions
# =============================================================================

pub fn create_timer(callback: timer_callback, state: ref<any>, due_time: i32, period: i32) -> timer
    return timer.new(callback, state, due_time, period)
end fn