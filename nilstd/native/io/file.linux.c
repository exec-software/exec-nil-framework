#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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

// Result type simulation for C (simplified)
// In ML: result<T, E> is a tagged union.
// For interop, we might need a specific struct layout or return primitive
// codes. For this MVP, let's assume the ML side handles the result wrapping if
// we return a handle or error code, OR we return a struct that matches ML
// layout.
//
// However, the simplest interop is usually primitive types.
// Let's define the C functions to return handles (i64) or error codes (i32).
// The ML wrapper will convert these to result<T, E>.

// Open file
// Returns handle (fd) or -1 on error
i64 ml_file_open(const char *path, i32 mode) {
  int flags = 0;
  int access_mode = 0644;

  switch (mode) {
  case MODE_READ:
    flags = O_RDONLY;
    break;
  case MODE_WRITE:
    flags = O_WRONLY | O_CREAT | O_TRUNC;
    break;
  case MODE_APPEND:
    flags = O_WRONLY | O_CREAT | O_APPEND;
    break;
  case MODE_READ_WRITE:
    flags = O_RDWR | O_CREAT;
    break;
  default:
    return -1;
  }

  int fd = open(path, flags, access_mode);
  return (i64)fd;
}

// Close file
i32 ml_file_close(i64 handle) { return close((int)handle); }

// Read bytes
// Returns bytes read or -1 on error
i32 ml_file_read(i64 handle, u8 *buffer, i32 len) {
  return read((int)handle, buffer, len);
}

// Write bytes
// Returns bytes written or -1 on error
i32 ml_file_write(i64 handle, const u8 *buffer, i32 len) {
  return write((int)handle, buffer, len);
}

// Seek
// Returns new position or -1 on error
i64 ml_file_seek(i64 handle, i64 pos) {
  return lseek((int)handle, pos, SEEK_SET);
}

// Get position
i64 ml_file_position(i64 handle) { return lseek((int)handle, 0, SEEK_CUR); }

// Get size
i64 ml_file_size(i64 handle) {
  struct stat st;
  if (fstat((int)handle, &st) == 0) {
    return (i64)st.st_size;
  }
  return -1;
}

// Flush
i32 ml_file_flush(i64 handle) { return fsync((int)handle); }

// Check if file exists
i32 ml_file_exists(const char *path) { return access(path, F_OK) == 0; }

// Delete file
i32 ml_file_delete(const char *path) { return unlink(path); }
