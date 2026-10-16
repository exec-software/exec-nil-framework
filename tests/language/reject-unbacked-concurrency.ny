import ny.lang.threading
import ny.lang.concurrency

fn main() -> i32
    let atom = atomic_i32.new(0)
    let channel_id = create()
    interlocked.memory_barrier()
    let guarded = mutex.new(0)
    let event = auto_reset_event.new(false)
    let current_thread = thread.current()
    let task = Task.of(1)
    return 0
end fn
