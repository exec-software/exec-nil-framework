@schema
@pragma once

import nil.std.io.{FileMode, FileAccess, FileShare, FileAttributes, FileInfo, DirectoryInfo}
import nil.std.io.{Stream, SeekOrigin, FileStream, MemoryStream}
import nil.std.io.{TextReader, TextWriter, StreamReader, StreamWriter}
import nil.std.io.{path, file, directory}
import test.{TestSuite, assert_true, assert_false, assert_equals, assert_not_null}

# =============================================================================
# Path Tests
# =============================================================================

pub fn test_path_operations() -> TestSuite
    let suite = TestSuite.new("Path Operations Tests")

    # Test 1: Get file name
    let path1 = string.from("/home/user/document.txt")
    let filename1 = path.get_file_name(path1)
    suite.add_test(assert_equals("document.txt", filename1.fmt(), "Get file name from Unix path"))

    let path2 = string.from("C:\\Users\\User\\document.txt")
    let filename2 = path.get_file_name(path2)
    suite.add_test(assert_equals("document.txt", filename2.fmt(), "Get file name from Windows path"))

    # Test 2: Get directory name
    let dir1 = path.get_directory_name(path1)
    suite.add_test(assert_equals("/home/user", dir1.fmt(), "Get directory name from Unix path"))

    let dir2 = path.get_directory_name(path2)
    suite.add_test(assert_equals("C:\\Users\\User", dir2.fmt(), "Get directory name from Windows path"))

    # Test 3: Get extension
    let ext1 = path.get_extension(path1)
    suite.add_test(assert_equals(".txt", ext1.fmt(), "Get file extension"))

    let path_no_ext = string.from("/home/user/file")
    let ext_no = path.get_extension(path_no_ext)
    suite.add_test(assert_equals("", ext_no.fmt(), "No extension returns empty"))

    # Test 4: Get file name without extension
    let name_no_ext = path.get_file_name_without_extension(path1)
    suite.add_test(assert_equals("document", name_no_ext.fmt(), "Get file name without extension"))

    # Test 5: Combine paths
    let combined1 = path.combine(string.from("/home/user"), string.from("document.txt"))
    suite.add_test(assert_equals("/home/user/document.txt", combined1.fmt(), "Combine two Unix paths"))

    let combined2 = path.combine(string.from("C:\\Users\\User"), string.from("document.txt"))
    suite.add_test(assert_equals("C:\\Users\\User\\document.txt", combined2.fmt(), "Combine two Windows paths"))

    # Test 6: Change extension
    let changed_ext = path.change_extension(path1, string.from(".pdf"))
    suite.add_test(assert_equals("/home/user/document.pdf", changed_ext.fmt(), "Change file extension"))

    let no_ext_change = path.change_extension(path_no_ext, string.from(".txt"))
    suite.add_test(assert_equals("/home/user/file.txt", no_ext_change.fmt(), "Add extension to file without one"))

    # Test 7: Is path rooted
    let rooted_unix = path.is_path_rooted(string.from("/absolute/path"))
    suite.add_test(assert_true(rooted_unix, "Unix absolute path is rooted"))

    let relative_unix = path.is_path_rooted(string.from("relative/path"))
    suite.add_test(assert_false(relative_unix, "Unix relative path is not rooted"))

    # Test 8: Has extension
    let has_ext = path.has_extension(string.from("file.txt"))
    suite.add_test(assert_true(has_ext, "File with extension has extension"))

    let no_has_ext = path.has_extension(string.from("file"))
    suite.add_test(assert_false(no_has_ext, "File without extension has no extension"))

    return suite
end fn

# =============================================================================
# File Info Tests
# =============================================================================

pub fn test_file_info() -> TestSuite
    let suite = TestSuite.new("FileInfo Tests")

    # Test 1: File info construction
    let file_path = string.from("/tmp/test_file.txt")
    let info = FileInfo.new(file_path)
    suite.add_test(assert_equals(file_path.fmt(), info.path.fmt(), "FileInfo stores correct path"))
    suite.add_test(assert_equals("test_file.txt", info.name().fmt(), "FileInfo extracts correct file name"))
    suite.add_test(assert_equals(".txt", info.extension().fmt(), "FileInfo extracts correct extension"))
    suite.add_test(assert_equals("/tmp", info.directory_name().fmt(), "FileInfo extracts correct directory"))

    # Test 2: File info with path without directory
    let simple_file = string.from("simple.txt")
    let simple_info = FileInfo.new(simple_file)
    suite.add_test(assert_equals("simple.txt", simple_info.name().fmt(), "Simple file name extraction"))
    suite.add_test(assert_equals("", simple_info.directory_name().fmt(), "Simple file has empty directory"))

    return suite
end fn

# =============================================================================
# Directory Info Tests
# =============================================================================

pub fn test_directory_info() -> TestSuite
    let suite = TestSuite.new("DirectoryInfo Tests")

    # Test 1: Directory info construction
    let dir_path = string.from("/home/user/documents")
    let dir_info = DirectoryInfo.new(dir_path)
    suite.add_test(assert_equals(dir_path.fmt(), dir_info.path.fmt(), "DirectoryInfo stores correct path"))
    suite.add_test(assert_equals("documents", dir_info.name().fmt(), "DirectoryInfo extracts correct name"))

    # Test 2: Parent directory
    let parent = dir_info.parent()
    suite.add_test(assert_not_null(parent, "Parent directory returns some value"))
    if parent.is_some() then
        let parent_dir = parent.unwrap()
        suite.add_test(assert_equals("user", parent_dir.name().fmt(), "Parent directory name is correct"))
    end if

    # Test 3: Root directory has no parent
    let root_dir = DirectoryInfo.new(string.from("/"))
    let root_parent = root_dir.parent()
    suite.add_test(assert_true(root_parent.is_none(), "Root directory has no parent"))

    return suite
end fn

# =============================================================================
# File Stream Tests
# =============================================================================

pub fn test_file_stream() -> TestSuite
    let suite = TestSuite.new("FileStream Tests")

    # Test 1: File stream construction
    let file_path = string.from("/tmp/test_stream.txt")
    let fs = FileStream.new(file_path, FileMode.create, FileAccess.write, FileShare.none)
    suite.add_test(assert_equals(file_path.fmt(), fs.name().fmt(), "FileStream stores correct path"))
    suite.add_test(assert_equals(FileMode.create, fs.mode, "FileStream stores correct mode"))
    suite.add_test(assert_equals(FileAccess.write, fs.access, "FileStream stores correct access"))

    fs.dispose()

    # Test 2: Simple file stream construction
    let fs2 = FileStream.new_simple(file_path, FileMode.open)
    suite.add_test(assert_equals(FileMode.open, fs2.mode, "Simple FileStream constructor sets mode"))

    fs2.dispose()

    return suite
end fn

# =============================================================================
# Memory Stream Tests
# =============================================================================

pub fn test_memory_stream() -> TestSuite
    let suite = TestSuite.new("MemoryStream Tests")

    # Test 1: Empty memory stream construction
    let ms1 = MemoryStream.new()
    suite.add_test(assert_equals(0, ms1.length, "New memory stream has zero length"))
    suite.add_test(assert_equals(0, ms1.capacity(), "New memory stream has default capacity"))
    suite.add_test(assert_true(ms1.expandable, "New memory stream is expandable"))

    # Test 2: Memory stream with capacity
    let ms2 = MemoryStream.new_with_capacity(100)
    suite.add_test(assert_equals(100, ms2.capacity(), "Memory stream with capacity has correct capacity"))

    # Test 3: Memory stream from data
    let data = vec<i8>.new()
    data.push(65)  # 'A'
    data.push(66)  # 'B'
    data.push(67)  # 'C'
    let ms3 = MemoryStream.from_data(data)
    suite.add_test(assert_equals(3, ms3.length, "Memory stream from data has correct length"))
    suite.add_test(assert_false(ms3.expandable, "Memory stream from data is not expandable"))

    # Test 4: Write to memory stream
    let ms4 = MemoryStream.new()
    let write_data = vec<i8>.new()
    write_data.push(72)  # 'H'
    write_data.push(101) # 'e'
    write_data.push(108) # 'l'
    write_data.push(108) # 'l'
    write_data.push(111) # 'o'

    ms4.write(write_data, 0, write_data.length())
    suite.add_test(assert_equals(5, ms4.length, "Memory stream length increases after write"))

    # Test 5: Read from memory stream
    let mut read_buffer = vec<i8>.new()
    read_buffer.resize(10)

    ms4.seek(0, SeekOrigin.begin)
    let bytes_read = ms4.read(read_buffer, 0, 10)
    suite.add_test(assert_equals(5, bytes_read, "Read returns correct number of bytes"))

    # Test 6: Seek operations
    ms4.seek(2, SeekOrigin.begin)
    let pos = ms4.stream.position
    suite.add_test(assert_equals(2, pos as i32, "Seek to beginning works"))

    ms4.seek(1, SeekOrigin.current)
    let pos2 = ms4.stream.position
    suite.add_test(assert_equals(3, pos2 as i32, "Seek from current works"))

    # Test 7: To array conversion
    let array_result = ms4.to_array()
    suite.add_test(assert_equals(5, array_result.length(), "To array returns correct length"))

    # Test 8: Get buffer
    let buffer_result = ms4.get_buffer()
    suite.add_test(assert_equals(ms4.capacity(), buffer_result.length(), "Get buffer returns underlying buffer"))

    return suite
end fn

# =============================================================================
# Stream Reader/Writer Tests
# =============================================================================

pub fn test_stream_reader_writer() -> TestSuite
    let suite = TestSuite.new("StreamReader/Writer Tests")

    # Test 1: StreamReader construction
    let ms = MemoryStream.new()
    let reader = StreamReader.new(ms.stream)
    suite.add_test(assert_not_null(option.some(reader), "StreamReader construction succeeds"))

    # Test 2: StreamWriter construction
    let ms2 = MemoryStream.new()
    let writer = StreamWriter.new(ms2.stream)
    suite.add_test(assert_not_null(option.some(writer), "StreamWriter construction succeeds"))

    # Test 3: Write and read text
    let test_text = string.from("Hello, World!")

    # Write text
    writer.write(test_text)
    writer.write_line(string.from(" This is a test."))
    writer.flush()

    # Reset position for reading
    ms2.stream.position = 0

    # Read text
    let reader2 = StreamReader.new(ms2.stream)
    let read_text = reader2.read_to_end()
    let expected = "Hello, World! This is a test.\n"
    suite.add_test(assert_equals(expected, read_text.fmt(), "Round trip text conversion works"))

    return suite
end fn

# =============================================================================
# Stream Utility Tests
# =============================================================================

pub fn test_stream_utilities() -> TestSuite
    let suite = TestSuite.new("Stream Utility Tests")

    # Test 1: Stream read byte
    let ms1 = MemoryStream.new()
    let write_data = vec<i8>.new()
    write_data.push(65)  # 'A'
    write_data.push(66)  # 'B'

    ms1.write(write_data, 0, 2)
    ms1.seek(0, SeekOrigin.begin)

    let byte1 = ms1.read_byte()
    let byte2 = ms1.read_byte()
    let byte3 = ms1.read_byte()  # Should be end of stream

    suite.add_test(assert_equals(65, byte1, "Read byte returns first byte"))
    suite.add_test(assert_equals(66, byte2, "Read byte returns second byte"))
    suite.add_test(assert_equals(-1, byte3, "Read byte returns -1 at end of stream"))

    # Test 2: Stream write byte
    let ms2 = MemoryStream.new()
    ms2.write_byte(90)  # 'Z'
    ms2.write_byte(89)  # 'Y'

    let mut read_buffer = vec<i8>.new()
    read_buffer.resize(2)
    ms2.seek(0, SeekOrigin.begin)
    let bytes_read = ms2.read(read_buffer, 0, 2)

    suite.add_test(assert_equals(2, bytes_read, "Write byte and read back works"))
    suite.add_test(assert_equals(90, read_buffer.get(0).unwrap(), "First written byte matches"))
    suite.add_test(assert_equals(89, read_buffer.get(1).unwrap(), "Second written byte matches"))

    # Test 3: Stream copy to
    let source_ms = MemoryStream.new()
    let dest_ms = MemoryStream.new()

    let source_data = vec<i8>.new()
    source_data.push(1)
    source_data.push(2)
    source_data.push(3)
    source_data.push(4)
    source_data.push(5)

    source_ms.write(source_data, 0, 5)
    source_ms.seek(0, SeekOrigin.begin)

    source_ms.copy_to(dest_ms.stream)

    dest_ms.seek(0, SeekOrigin.begin)
    let mut copy_buffer = vec<i8>.new()
    copy_buffer.resize(5)
    let copy_bytes_read = dest_ms.read(copy_buffer, 0, 5)

    suite.add_test(assert_equals(5, copy_bytes_read, "Copy to transfers all bytes"))

    return suite
end fn

# =============================================================================
# File System Integration Tests
# =============================================================================

pub fn test_file_system_integration() -> TestSuite
    let suite = TestSuite.new("File System Integration Tests")

    # Test 1: File operations (placeholder tests)
    let test_file = string.from("/tmp/integration_test.txt")
    let test_content = string.from("Integration test content")

    # Write all text
    file.write_all_text(test_file, test_content)
    suite.add_test(assert_true(file.exists(test_file), "File exists after write_all_text"))

    # Read all text
    let read_content = file.read_all_text(test_file)
    suite.add_test(assert_equals(test_content.fmt(), read_content.fmt(), "Read content matches written content"))

    # Test 2: Directory operations (placeholder tests)
    let test_dir = string.from("/tmp/integration_test_dir")
    let dir_info = directory.create_directory(test_dir)
    suite.add_test(assert_equals(test_dir.fmt(), dir_info.path.fmt(), "Directory created with correct path"))

    return suite
end fn

# =============================================================================
# Integration Test - Run all IO module tests
# =============================================================================

pub fn run_all_io_tests() -> bool
    print("=== Running IO Module Tests ===")

    let mut all_passed = true

    all_passed = all_passed and test.run_test_suite("Path Operations", test_path_operations)
    all_passed = all_passed and test.run_test_suite("FileInfo", test_file_info)
    all_passed = all_passed and test.run_test_suite("DirectoryInfo", test_directory_info)
    all_passed = all_passed and test.run_test_suite("FileStream", test_file_stream)
    all_passed = all_passed and test.run_test_suite("MemoryStream", test_memory_stream)
    all_passed = all_passed and test.run_test_suite("StreamReader/Writer", test_stream_reader_writer)
    all_passed = all_passed and test.run_test_suite("Stream Utilities", test_stream_utilities)
    all_passed = all_passed and test.run_test_suite("File System Integration", test_file_system_integration)

    print("=== IO Module Tests Complete ===")
    if all_passed then
        print("✅ All IO module tests PASSED")
    else
        print("❌ Some IO module tests FAILED")
    end if

    return all_passed
end fn