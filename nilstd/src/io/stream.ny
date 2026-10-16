@pragma once

module nil.std.io
import nil.std.core

# =============================================================================
# Stream Abstraction
# =============================================================================

pub trait Read
    fn read(mut self, buffer: ptr<u8>, len: i32) -> Result<i32, text>
end trait

pub trait Write
    fn write(mut self, buffer: ptr<u8>, len: i32) -> Result<i32, text>
    fn flush(mut self) -> Result<bool, text>
end trait

pub trait Seek
    fn seek(mut self, offset: i64, origin: i32) -> Result<i64, text>
end trait

pub trait Close
    fn close(mut self) -> Result<bool, text>
end trait

pub fn stream_copy<R: Read, W: Write>(mut reader: R, mut writer: W) -> Result<i32, text>
    let buf_size = 4096
    let buf = allocate<u8>(buf_size)
    let mut total: i32 = 0

    while true do
        let read_res = reader.read(buf, buf_size)
        let mut count = -1
        match read_res
            Result.Ok(c) -> count = (c as i32)
            Result.Error(_) -> count = -1
        end match

        if count < 0 then
            match read_res
                Result.Error(e) ->
                    deallocate(buf)
                    return Result.Error(e)
                Result.Ok(_) ->
                    deallocate(buf)
                    return Result.Error("Panic: expected error")
            end match
        end if

        if count > buf_size then
            deallocate(buf)
            return Result.Error("Read returned an invalid byte count")
        end if

        if count == 0 then
            let flush_res = writer.flush()
            match flush_res
                Result.Error(e) ->
                    deallocate(buf)
                    return Result.Error(e)
                Result.Ok(_) -> total = total
            end match
            deallocate(buf)
            return Result.Ok(total)
        end if

        let mut written: i32 = 0
        while written < count do
            let remaining = count - written
            let write_res = writer.write(buf + written, remaining)
            let mut w: i32 = -1
            match write_res
                Result.Error(e) ->
                    deallocate(buf)
                    return Result.Error(e)
                Result.Ok(n) -> w = (n as i32)
            end match

            if w <= 0 or w > remaining then
                deallocate(buf)
                return Result.Error("Write returned an invalid byte count")
            end if
            if total > 2147483647 - w then
                deallocate(buf)
                return Result.Error("Stream exceeds the i32 byte-count limit")
            end if
            written = written + w
            total = total + w
        end while
    end while

    deallocate(buf)
    return Result.Error("Stream copy terminated unexpectedly")
end fn
