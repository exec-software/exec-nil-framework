@schema
@pragma once

module nil.std.io

# =============================================================================
# File Operations (mirrors System.IO.File)
# =============================================================================

pub enum FileMode
    create_new      # Creates new file, fails if exists
    create          # Creates new file, overwrites if exists
    open            # Opens existing file, fails if not exists
    open_or_create  # Opens existing or creates new
    truncate        # Opens existing and truncates, creates if not exists
    append          # Opens existing and seeks to end, creates if not exists
end enum

pub enum FileAccess
    read
    write
    read_write
end enum

pub enum FileShare
    none
    read
    write
    read_write
    delete
end enum

pub enum FileAttributes
    readonly        = 0x00000001
    hidden          = 0x00000002
    system          = 0x00000004
    directory       = 0x00000010
    archive         = 0x00000020
    device          = 0x00000040
    normal          = 0x00000080
    temporary       = 0x00000100
    sparse_file     = 0x00000200
    reparse_point   = 0x00000400
    compressed      = 0x00000800
    offline         = 0x00001000
    not_content_indexed = 0x00002000
    encrypted       = 0x00004000
    integrity_stream = 0x00008000
    virtual         = 0x00010000
    no_scrub_data   = 0x00020000
end enum

pub struct FileInfo
    pub path: string
    pub exists: bool
    pub length: i64
    pub creation_time: i64  # Unix timestamp
    pub last_access_time: i64
    pub last_write_time: i64
    pub attributes: i32     # FileAttributes flags
    pub is_readonly: bool
    pub is_directory: bool
end struct

impl FileInfo
    pub fn new(path: string) -> FileInfo
        return FileInfo {
            path: path,
            exists: false,
            length: 0,
            creation_time: 0,
            last_access_time: 0,
            last_write_time: 0,
            attributes: 0,
            is_readonly: false,
            is_directory: false
        }
    end fn

    pub fn name(self: ref<Self>) -> string
        return path.get_file_name(self.path)
    end fn

    pub fn extension(self: ref<Self>) -> string
        return path.get_extension(self.path)
    end fn

    pub fn directory_name(self: ref<Self>) -> string
        return path.get_directory_name(self.path)
    end fn
end impl

# File operation functions
pub fn exists(path: string) -> bool
    # Platform-specific implementation would go here
    # For now, return false as placeholder
    return false
end fn

pub fn create(path: string) -> FileStream
    return FileStream.new(path, FileMode.create, FileAccess.write, FileShare.none)
end fn

pub fn open(path: string, mode: FileMode) -> FileStream
    return FileStream.new(path, mode, FileAccess.read_write, FileShare.none)
end fn

pub fn open_read(path: string) -> FileStream
    return FileStream.new(path, FileMode.open, FileAccess.read, FileShare.read)
end fn

pub fn open_write(path: string) -> FileStream
    return FileStream.new(path, FileMode.create, FileAccess.write, FileShare.none)
end fn

pub fn read_all_text(path: string) -> string
    let stream = open_read(path)
    let reader = StreamReader.new(stream)
    let result = reader.read_to_end()
    reader.dispose()
    stream.dispose()
    return result
end fn

pub fn write_all_text(path: string, contents: string) -> void
    let stream = create(path)
    let writer = StreamWriter.new(stream)
    writer.write(contents)
    writer.dispose()
    stream.dispose()
end fn

pub fn read_all_bytes(path: string) -> vec<i8>
    let stream = open_read(path)
    let length = stream.length()
    let mut buffer = vec<i8>.new()
    buffer.resize(length)

    let mut total_read = 0
    while total_read < length
        let bytes_read = stream.read(buffer, total_read, length - total_read)
        if bytes_read <= 0 then
            break
        end if
        total_read = total_read + bytes_read
    end while

    stream.dispose()
    return buffer
end fn

pub fn write_all_bytes(path: string, bytes: vec<i8>) -> void
    let stream = create(path)
    stream.write(bytes, 0, bytes.length())
    stream.dispose()
end fn

pub fn append_all_text(path: string, contents: string) -> void
    let stream = FileStream.new(path, FileMode.append, FileAccess.write, FileShare.none)
    let writer = StreamWriter.new(stream)
    writer.write(contents)
    writer.dispose()
    stream.dispose()
end fn

pub fn copy(source_path: string, dest_path: string) -> void
    let source_bytes = read_all_bytes(source_path)
    write_all_bytes(dest_path, source_bytes)
end fn

pub fn copy_overwrite(source_path: string, dest_path: string) -> void
    if exists(dest_path) then
        delete(dest_path)
    end if
    copy(source_path, dest_path)
end fn

pub fn move(source_path: string, dest_path: string) -> void
    copy(source_path, dest_path)
    delete(source_path)
end fn

pub fn delete(path: string) -> bool
    # Platform-specific implementation
    return true
end fn

pub fn get_attributes(path: string) -> i32
    # Platform-specific implementation
    return FileAttributes.normal as i32
end fn

pub fn set_attributes(path: string, attributes: i32) -> void
    # Platform-specific implementation
end fn

pub fn get_creation_time(path: string) -> i64
    # Platform-specific implementation
    return 0
end fn

pub fn set_creation_time(path: string, time: i64) -> void
    # Platform-specific implementation
end fn

pub fn get_last_write_time(path: string) -> i64
    # Platform-specific implementation
    return 0
end fn

pub fn set_last_write_time(path: string, time: i64) -> void
    # Platform-specific implementation
end fn

pub fn get_last_access_time(path: string) -> i64
    # Platform-specific implementation
    return 0
end fn

# =============================================================================
# Directory Operations (mirrors System.IO.Directory)
# =============================================================================

pub struct DirectoryInfo
    pub path: string
    pub exists: bool
    pub creation_time: i64
    pub last_access_time: i64
    pub last_write_time: i64
    pub attributes: i32
end struct

impl DirectoryInfo
    pub fn new(path: string) -> DirectoryInfo
        return DirectoryInfo {
            path: path,
            exists: false,
            creation_time: 0,
            last_access_time: 0,
            last_write_time: 0,
            attributes: FileAttributes.directory as i32
        }
    end fn

    pub fn name(self: ref<Self>) -> string
        return path.get_file_name(self.path)
    end fn

    pub fn parent(self: ref<Self>) -> option<DirectoryInfo>
        let parent_path = path.get_directory_name(self.path)
        if parent_path.is_empty() then
            return option.none
        end if
        return option.some(DirectoryInfo.new(parent_path))
    end fn

    pub fn get_files(self: ref<Self>) -> vec<FileInfo>
        return directory.get_files(self.path)
    end fn

    pub fn get_directories(self: ref<Self>) -> vec<DirectoryInfo>
        return directory.get_directories(self.path)
    end fn

    pub fn create_subdirectory(self: ref<Self>, name: string) -> DirectoryInfo
        let sub_path = path.combine(self.path, name)
        return directory.create_directory(sub_path)
    end fn
end impl

# Directory operation functions
pub fn directory_exists(path: string) -> bool
    # Platform-specific implementation
    return false
end fn

pub fn create_directory(path: string) -> DirectoryInfo
    # Platform-specific implementation
    return DirectoryInfo.new(path)
end fn

pub fn delete_directory(path: string) -> bool
    # Platform-specific implementation
    return true
end fn

pub fn delete_directory_recursive(path: string) -> bool
    # Platform-specific implementation - delete directory and all contents
    return true
end fn

pub fn move_directory(source_path: string, dest_path: string) -> void
    # Platform-specific implementation
end fn

pub fn get_files(path: string) -> vec<FileInfo>
    # Platform-specific implementation
    return vec<FileInfo>.new()
end fn

pub fn get_directories(path: string) -> vec<DirectoryInfo>
    # Platform-specific implementation
    return vec<DirectoryInfo>.new()
end fn

pub fn get_file_system_entries(path: string) -> vec<string>
    # Platform-specific implementation
    return vec<string>.new()
end fn

pub fn get_current_directory() -> string
    # Platform-specific implementation
    return string.new()
end fn

pub fn set_current_directory(path: string) -> void
    # Platform-specific implementation
end fn

pub fn get_logical_drives() -> vec<string>
    # Platform-specific implementation
    return vec<string>.new()
end fn

# =============================================================================
# Path Operations (mirrors System.IO.Path)
# =============================================================================

pub struct path
end impl

impl path
    # Directory separator characters
    pub fn directory_separator_char() -> i8
        return '/'  # Unix-style, would be OS-dependent
    end fn

    pub fn alt_directory_separator_char() -> i8
        return '\\'
    end fn

    pub fn path_separator() -> i8
        return ':'
    end fn

    pub fn volume_separator_char() -> i8
        return ':'
    end fn

    # Path combination and manipulation
    pub fn combine(path1: string, path2: string) -> string
        if path1.is_empty() then return path2 end if
        if path2.is_empty() then return path1 end if

        let sep = string.from(path.directory_separator_char().to_text())

        if path1.fmt().ends_with(sep.fmt()) or path2.fmt().starts_with(sep.fmt()) then
            return path1.concat(path2)
        else
            return path1.concat(sep).concat(path2)
        end if
    end fn

    pub fn combine_three(path1: string, path2: string, path3: string) -> string
        return path.combine(path.combine(path1, path2), path3)
    end fn

    pub fn combine_paths(paths: vec<string>) -> string
        if paths.is_empty() then return string.new() end if

        let mut result = paths.get(0).unwrap()
        let mut i = 1
        while i < paths.length()
            result = path.combine(result, paths.get(i).unwrap())
            i = i + 1
        end while

        return result
    end fn

    # Path component extraction
    pub fn get_file_name(path: string) -> string
        let text = path.fmt()
        let last_sep = text.last_index_of(path.directory_separator_char().to_text())
        if last_sep == -1 then
            return path
        end if

        return string.from(text.substr(last_sep + 1, text.len()))
    end fn

    pub fn get_directory_name(path: string) -> string
        let text = path.fmt()
        let last_sep = text.last_index_of(path.directory_separator_char().to_text())
        if last_sep == -1 then
            return string.new()
        end if

        return string.from(text.substr(0, last_sep))
    end fn

    pub fn get_extension(path: string) -> string
        let file_name = path.get_file_name()
        let text = file_name.fmt()
        let last_dot = text.last_index_of(".")

        if last_dot == -1 or last_dot == 0 then
            return string.new()
        end if

        return string.from(text.substr(last_dot, text.len()))
    end fn

    pub fn get_file_name_without_extension(path: string) -> string
        let file_name = path.get_file_name()
        let ext = path.get_extension(path)

        if ext.is_empty() then
            return file_name
        end if

        let file_text = file_name.fmt()
        let ext_text = ext.fmt()
        return string.from(file_text.substr(0, file_text.len() - ext_text.len()))
    end fn

    pub fn get_path_root(path: string) -> string
        let text = path.fmt()
        if text.len() == 0 then return string.new() end if

        # Unix root
        if text[0] == path.directory_separator_char() then
            return string.from("/")
        end if

        # Windows drive root (e.g., "C:\")
        if text.len() >= 2 and text[1] == ':' then
            return string.from(text.substr(0, 2))
        end if

        return string.new()
    end fn

    # Path validation and normalization
    pub fn is_path_rooted(path: string) -> bool
        let text = path.fmt()
        if text.len() == 0 then return false end if

        # Unix absolute path
        if text[0] == path.directory_separator_char() then
            return true
        end if

        # Windows absolute path
        if text.len() >= 2 and text[1] == ':' then
            return true
        end if

        return false
    end fn

    pub fn has_extension(path: string) -> bool
        return not path.get_extension(path).is_empty()
    end fn

    # Path transformation
    pub fn change_extension(path: string, extension: string) -> string
        let without_ext = path.get_file_name_without_extension(path)
        let dir = path.get_directory_name(path)

        let new_name = if extension.is_empty() then
            without_ext
        else
            let ext_text = extension.fmt()
            let new_ext = if ext_text.starts_with(".") then extension else string.from("." + ext_text)
            without_ext.concat(new_ext)
        end if

        if dir.is_empty() then
            return new_name
        else
            return path.combine(dir, new_name)
        end if
    end fn

    # Temp file and directory paths
    pub fn get_temp_path() -> string
        # Platform-specific implementation
        return string.from("/tmp/")
    end fn

    pub fn get_temp_file_name() -> string
        # Generate unique temporary file name
        return path.combine(path.get_temp_path(), string.from("tmp_file"))
    end fn

    pub fn get_random_file_name() -> string
        # Generate random file name
        return string.from("random_file.tmp")
    end fn

    # Special paths
    pub fn get_current_directory() -> string
        return string.from(".")
    end fn

    pub fn get_full_path(path: string) -> string
        if path.is_path_rooted(path) then
            return path
        end if

        return path.combine(path.get_current_directory(), path)
    end fn
end impl

# =============================================================================
# Stream Classes (mirrors System.IO.Stream)
# =============================================================================

pub enum SeekOrigin
    begin = 0
    current = 1
    end = 2
end enum

pub struct Stream
    pub can_read: bool
    pub can_write: bool
    pub can_seek: bool
    pub length: i64
    pub position: i64
end struct

impl Stream
    pub fn new() -> Stream
        return Stream {
            can_read: false,
            can_write: false,
            can_seek: false,
            length: 0,
            position: 0
        }
    end fn

    # Abstract methods - must be implemented by derived classes
    pub fn read(self: ref<Self>, buffer: vec<i8>, offset: i32, count: i32) -> i32
        return 0  # Placeholder
    end fn

    pub fn write(self: ref<Self>, buffer: vec<i8>, offset: i32, count: i32) -> void
        # Placeholder
    end fn

    pub fn seek(self: ref<Self>, offset: i64, origin: SeekOrigin) -> i64
        return 0  # Placeholder
    end fn

    pub fn flush(self: ref<Self>) -> void
        # Placeholder
    end fn

    pub fn set_length(self: ref<Self>, value: i64) -> void
        # Placeholder
    end fn

    pub fn close(self: ref<Self>) -> void
        # Placeholder
    end fn

    pub fn dispose(self: ref<Self>) -> void
        self.close()
    end fn

    # Utility methods
    pub fn read_byte(self: ref<Self>) -> i8
        let mut buffer = vec<i8>.new()
        buffer.resize(1)
        let bytes_read = self.read(buffer, 0, 1)
        if bytes_read == 0 then
            return -1  # End of stream
        end if
        return buffer.get(0).unwrap()
    end fn

    pub fn write_byte(self: ref<Self>, value: i8) -> void
        let mut buffer = vec<i8>.new()
        buffer.push(value)
        self.write(buffer, 0, 1)
    end fn

    pub fn copy_to(self: ref<Self>, destination: Stream) -> void
        self.copy_to_with_buffer_size(destination, 81920)  # Default buffer size
    end fn

    pub fn copy_to_with_buffer_size(self: ref<Self>, destination: Stream, buffer_size: i32) -> void
        let mut buffer = vec<i8>.new()
        buffer.resize(buffer_size)

        loop
            let bytes_read = self.read(buffer, 0, buffer_size)
            if bytes_read <= 0 then
                break
            end if
            destination.write(buffer, 0, bytes_read)
        end loop
    end fn
end impl

# =============================================================================
# FileStream (mirrors System.IO.FileStream)
# =============================================================================

pub struct FileStream
    pub stream: Stream
    pub path: string
    pub mode: FileMode
    pub access: FileAccess
    pub share: FileShare
    pub handle: any  # Platform-specific file handle
end struct

impl FileStream
    pub fn new(path: string, mode: FileMode, access: FileAccess, share: FileShare) -> FileStream
        return FileStream {
            stream: Stream.new(),
            path: path,
            mode: mode,
            access: access,
            share: share,
            handle: 0
        }
    end fn

    pub fn new_simple(path: string, mode: FileMode) -> FileStream
        return FileStream.new(path, mode, FileAccess.read_write, FileShare.none)
    end fn

    # Override stream methods with file-specific implementations
    pub fn read(self: ref<Self>, buffer: vec<i8>, offset: i32, count: i32) -> i32
        # Platform-specific file read implementation
        return 0
    end fn

    pub fn write(self: ref<Self>, buffer: vec<i8>, offset: i32, count: i32) -> void
        # Platform-specific file write implementation
    end fn

    pub fn seek(self: ref<Self>, offset: i64, origin: SeekOrigin) -> i64
        # Platform-specific file seek implementation
        return 0
    end fn

    pub fn flush(self: ref<Self>) -> void
        # Platform-specific file flush implementation
    end fn

    pub fn set_length(self: ref<Self>, value: i64) -> void
        # Platform-specific file truncate implementation
    end fn

    pub fn close(self: ref<Self>) -> void
        # Platform-specific file close implementation
        self.handle = 0
    end fn

    pub fn name(self: ref<Self>) -> string
        return self.path
    end fn
end impl

# =============================================================================
# TextReader and TextWriter (mirrors System.IO.TextReader/TextWriter)
# =============================================================================

pub struct TextReader
    pub stream: Stream
end struct

impl TextReader
    pub fn new(stream: Stream) -> TextReader
        return TextReader { stream: stream }
    end fn

    pub fn read(self: ref<Self>) -> i32
        # Read single character
        return -1  # End of stream
    end fn

    pub fn read_to_end(self: ref<Self>) -> string
        # Read entire stream as string
        return string.new()
    end fn

    pub fn read_line(self: ref<Self>) -> string
        # Read single line
        return string.new()
    end fn

    pub fn peek(self: ref<Self>) -> i32
        # Peek at next character without consuming
        return -1  # End of stream
    end fn

    pub fn close(self: ref<Self>) -> void
        self.stream.close()
    end fn

    pub fn dispose(self: ref<Self>) -> void
        self.close()
    end fn
end impl

pub struct TextWriter
    pub stream: Stream
    pub auto_flush: bool
    pub new_line: string
end struct

impl TextWriter
    pub fn new(stream: Stream) -> TextWriter
        return TextWriter {
            stream: stream,
            auto_flush: false,
            new_line: string.from("\n")
        }
    end fn

    pub fn write(self: ref<Self>, value: string) -> void
        let bytes = text.Encoding.utf8().get_bytes(value)
        self.stream.write(bytes, 0, bytes.length())

        if self.auto_flush then
            self.stream.flush()
        end if
    end fn

    pub fn write_line(self: ref<Self>, value: string) -> void
        self.write(value)
        self.write(self.new_line)
    end fn

    pub fn write_line(self: ref<Self>) -> void
        self.write(self.new_line)
    end fn

    pub fn flush(self: ref<Self>) -> void
        self.stream.flush()
    end fn

    pub fn close(self: ref<Self>) -> void
        self.flush()
        self.stream.close()
    end fn

    pub fn dispose(self: ref<Self>) -> void
        self.close()
    end fn
end impl

# =============================================================================
# StreamReader and StreamWriter (mirrors System.IO.StreamReader/StreamWriter)
# =============================================================================

pub struct StreamReader
    pub reader: TextReader
    pub encoding: text.Encoding
    pub buffer_size: i32
end struct

impl StreamReader
    pub fn new(stream: Stream) -> StreamReader
        return StreamReader {
            reader: TextReader.new(stream),
            encoding: text.Encoding.utf8(),
            buffer_size: 1024
        }
    end fn

    pub fn new_with_encoding(stream: Stream, encoding: text.Encoding) -> StreamReader
        return StreamReader {
            reader: TextReader.new(stream),
            encoding: encoding,
            buffer_size: 1024
        }
    end fn

    pub fn read(self: ref<Self>) -> i32
        return self.reader.read()
    end fn

    pub fn read_to_end(self: ref<Self>) -> string
        return self.reader.read_to_end()
    end fn

    pub fn read_line(self: ref<Self>) -> string
        return self.reader.read_line()
    end fn

    pub fn peek(self: ref<Self>) -> i32
        return self.reader.peek()
    end fn

    pub fn close(self: ref<Self>) -> void
        self.reader.close()
    end fn

    pub fn dispose(self: ref<Self>) -> void
        self.close()
    end fn
end impl

pub struct StreamWriter
    pub writer: TextWriter
    pub encoding: text.Encoding
    pub auto_flush: bool
end struct

impl StreamWriter
    pub fn new(stream: Stream) -> StreamWriter
        let text_writer = TextWriter.new(stream)
        return StreamWriter {
            writer: text_writer,
            encoding: text.Encoding.utf8(),
            auto_flush: false
        }
    end fn

    pub fn new_with_encoding(stream: Stream, encoding: text.Encoding) -> StreamWriter
        let text_writer = TextWriter.new(stream)
        return StreamWriter {
            writer: text_writer,
            encoding: encoding,
            auto_flush: false
        }
    end fn

    pub fn write(self: ref<Self>, value: string) -> void
        self.writer.write(value)
    end fn

    pub fn write_line(self: ref<Self>, value: string) -> void
        self.writer.write_line(value)
    end fn

    pub fn write_line(self: ref<Self>) -> void
        self.writer.write_line()
    end fn

    pub fn flush(self: ref<Self>) -> void
        self.writer.flush()
    end fn

    pub fn close(self: ref<Self>) -> void
        self.writer.close()
    end fn

    pub fn dispose(self: ref<Self>) -> void
        self.close()
    end fn
end impl

# =============================================================================
# MemoryStream (mirrors System.IO.MemoryStream)
# =============================================================================

pub struct MemoryStream
    pub stream: Stream
    pub buffer: vec<i8>
    pub expandable: bool
    pub origin: i32
    pub capacity: i32
end struct

impl MemoryStream
    pub fn new() -> MemoryStream
        let buffer = vec<i8>.new()
        return MemoryStream.from_buffer(buffer, true)
    end fn

    pub fn new_with_capacity(capacity: i32) -> MemoryStream
        let buffer = vec<i8>.new()
        buffer.resize(capacity)
        return MemoryStream.from_buffer(buffer, true)
    end fn

    pub fn from_data(data: vec<i8>) -> MemoryStream
        return MemoryStream.from_buffer(data, false)
    end fn

    private fn from_buffer(buffer: vec<i8>, expandable: bool) -> MemoryStream
        let stream = Stream.new()
        stream.can_read = true
        stream.can_write = expandable
        stream.can_seek = true
        stream.length = buffer.length() as i64
        stream.position = 0

        return MemoryStream {
            stream: stream,
            buffer: buffer,
            expandable: expandable,
            origin: 0,
            capacity: buffer.length()
        }
    end fn

    pub fn read(self: ref<Self>, buffer: vec<i8>, offset: i32, count: i32) -> i32
        let mut bytes_to_read = count
        let mut buffer_pos = self.origin + (self.stream.position as i32)

        if buffer_pos + bytes_to_read > self.buffer.length() then
            bytes_to_read = self.buffer.length() - buffer_pos
        end if

        if bytes_to_read <= 0 then
            return 0
        end if

        let mut i = 0
        while i < bytes_to_read
            buffer.set(offset + i, self.buffer.get(buffer_pos + i).unwrap())
            i = i + 1
        end while

        self.stream.position = self.stream.position + (bytes_to_read as i64)
        return bytes_to_read
    end fn

    pub fn write(self: ref<Self>, buffer: vec<i8>, offset: i32, count: i32) -> void
        if not self.expandable then
            return
        end if

        let required_pos = self.origin + (self.stream.position as i32) + count
        if required_pos > self.buffer.length() then
            self.buffer.resize(required_pos)
        end if

        let mut i = 0
        while i < count
            let buffer_pos = self.origin + (self.stream.position as i32) + i
            self.buffer.set(buffer_pos, buffer.get(offset + i).unwrap())
            i = i + 1
        end while

        self.stream.position = self.stream.position + (count as i64)
        if self.stream.position > self.stream.length then
            self.stream.length = self.stream.position
        end if
    end fn

    pub fn seek(self: ref<Self>, offset: i64, origin: SeekOrigin) -> i64
        let mut new_position = 0 as i64

        match origin
            SeekOrigin.begin -> new_position = offset
            SeekOrigin.current -> new_position = self.stream.position + offset
            SeekOrigin.end -> new_position = self.stream.length + offset
        end match

        if new_position < 0 then
            new_position = 0
        end if

        self.stream.position = new_position
        return new_position
    end fn

    pub fn to_array(self: ref<Self>) -> vec<i8>
        let mut result = vec<i8>.new()
        let mut i = 0
        while i < self.stream.length
            result.push(self.buffer.get(i).unwrap())
            i = i + 1
        end while
        return result
    end fn

    pub fn get_buffer(self: ref<Self>) -> vec<i8>
        return self.buffer
    end fn

    pub fn capacity(self: ref<Self>) -> i32
        return self.capacity
    end fn

    pub fn set_length(self: ref<Self>, value: i64) -> void
        if value < 0 then return end if

        let new_length = value as i32
        if self.expandable and new_length > self.buffer.length() then
            self.buffer.resize(new_length)
        end if

        self.stream.length = value
        if self.stream.position > value then
            self.stream.position = value
        end if
    end fn

    pub fn close(self: ref<Self>) -> void
        # Memory streams don't need to close anything
        self.stream.position = 0
    end fn
end impl