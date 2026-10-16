#include <windows.h>
#include <stdint.h>
#include <stdio.h>

// ML types mapping
typedef int32_t i32;
typedef int64_t i64;
typedef uint8_t u8;

// File mode enum mapping
// read=0, write=1, append=2, read_write=3
typedef enum {
  MODE_READ = 0,
  MODE_WRITE = 1,
  MODE_APPEND = 2,
  MODE_READ_WRITE = 3
} file_mode;

// Open file
// Returns handle (i64 cast of HANDLE) or -1 on error
i64 ml_file_open(const char *path, i32 mode) {
    DWORD desired_access = 0;
    DWORD share_mode = FILE_SHARE_READ | FILE_SHARE_WRITE;
    DWORD creation_disposition = 0;
    DWORD flags = FILE_ATTRIBUTE_NORMAL;

    switch (mode) {
    case MODE_READ:
        desired_access = GENERIC_READ;
        creation_disposition = OPEN_EXISTING;
        break;
    case MODE_WRITE:
        desired_access = GENERIC_WRITE;
        creation_disposition = CREATE_ALWAYS; // Truncate if exists
        break;
    case MODE_APPEND:
        desired_access = FILE_APPEND_DATA;
        creation_disposition = OPEN_ALWAYS; // Create if not exists
        break;
    case MODE_READ_WRITE:
        desired_access = GENERIC_READ | GENERIC_WRITE;
        creation_disposition = OPEN_ALWAYS;
        break;
    default:
        return -1;
    }

    HANDLE hFile = CreateFileA(
        path,
        desired_access,
        share_mode,
        NULL,
        creation_disposition,
        flags,
        NULL
    );

    if (hFile == INVALID_HANDLE_VALUE) {
        return -1;
    }

    return (i64)hFile;
}

// Close file
i32 ml_file_close(i64 handle) {
    if (CloseHandle((HANDLE)handle)) {
        return 0; // Success
    }
    return -1;
}

// Read bytes
// Returns bytes read or -1 on error
i32 ml_file_read(i64 handle, u8 *buffer, i32 len) {
    DWORD bytes_read = 0;
    if (ReadFile((HANDLE)handle, buffer, len, &bytes_read, NULL)) {
        return (i32)bytes_read;
    }
    return -1;
}

// Write bytes
// Returns bytes written or -1 on error
i32 ml_file_write(i64 handle, const u8 *buffer, i32 len) {
    DWORD bytes_written = 0;
    if (WriteFile((HANDLE)handle, buffer, len, &bytes_written, NULL)) {
        return (i32)bytes_written;
    }
    return -1;
}

// Seek
// Returns new position or -1 on error
i64 ml_file_seek(i64 handle, i64 pos) {
    LARGE_INTEGER distance;
    distance.QuadPart = pos;
    LARGE_INTEGER new_ptr;

    if (SetFilePointerEx((HANDLE)handle, distance, &new_ptr, FILE_BEGIN)) {
        return (i64)new_ptr.QuadPart;
    }
    return -1;
}

// Get position
i64 ml_file_position(i64 handle) {
    LARGE_INTEGER distance;
    distance.QuadPart = 0;
    LARGE_INTEGER new_ptr;

    // Move 0 from CURRENT to get current position
    if (SetFilePointerEx((HANDLE)handle, distance, &new_ptr, FILE_CURRENT)) {
        return (i64)new_ptr.QuadPart;
    }
    return -1;
}

// Get size
i64 ml_file_size(i64 handle) {
    LARGE_INTEGER size;
    if (GetFileSizeEx((HANDLE)handle, &size)) {
        return (i64)size.QuadPart;
    }
    return -1;
}

// Flush
i32 ml_file_flush(i64 handle) {
    if (FlushFileBuffers((HANDLE)handle)) {
        return 0;
    }
    return -1;
}

// Check if file exists
i32 ml_file_exists(const char *path) {
    DWORD attrib = GetFileAttributesA(path);
    return (attrib != INVALID_FILE_ATTRIBUTES && !(attrib & FILE_ATTRIBUTE_DIRECTORY));
}

// Delete file
i32 ml_file_delete(const char *path) {
    if (DeleteFileA(path)) {
        return 0; // Success (0 in this api implies logic true for int return, wait. existing linux impl returns 0 on success? Let's check.)
        // Linux unlink returns 0 on success. Yes.
    }
    return -1;
}
