/* A Windows stand-in for the "#!" line.
 *
 * Several contracts use a Python script as a stand-in compiler. POSIX runs
 * such a script directly because the kernel honours its shebang; Windows has
 * no equivalent, and nilbuild deliberately never invokes a shell, so pointing
 * --compiler at the script yields an honest "%1 is not a valid Win32
 * application". Teaching a build driver to read an interpreter name out of a
 * file it is about to execute would be a real behaviour change, and not one a
 * test should motivate -- so the test supplies what the platform needs
 * instead: a real executable that forwards to the interpreter.
 *
 * The harness compiles this with FORWARD_TO defined to an already-quoted
 * "<interpreter> <script>" command-line prefix. Only the arguments the driver
 * passes at run time are quoted here, and they must be: these fixtures put
 * their work in a directory whose name deliberately contains a space and a
 * semicolon.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#ifndef FORWARD_TO
#error "FORWARD_TO must be defined to a quoted interpreter and script"
#endif

static char line[32768];
static size_t used;

static void push(char c) {
  if (used + 1 < sizeof(line)) line[used++] = c;
}

static void push_text(const char *text) {
  while (*text) push(*text++);
}

/* Quote one argument the way CommandLineToArgvW parses it back, which is the
 * rule the callee applies. Windows passes a single command-line string and the
 * callee splits it, so an argument holding a space or a quote has to be
 * escaped here or it silently becomes two arguments. */
static void push_quoted(const char *argument) {
  size_t i = 0;
  if (argument[0] != '\0' && strpbrk(argument, " \t\n\v\"") == NULL) {
    push_text(argument);
    return;
  }
  push('"');
  while (argument[i] != '\0') {
    size_t slashes = 0;
    size_t n;
    while (argument[i] == '\\') { ++i; ++slashes; }
    if (argument[i] == '\0') {
      /* Trailing backslashes precede the closing quote, so they double. */
      for (n = 0; n < slashes * 2; ++n) push('\\');
      break;
    }
    if (argument[i] == '"') {
      /* A literal quote needs its backslashes doubled and itself escaped. */
      for (n = 0; n < slashes * 2 + 1; ++n) push('\\');
    } else {
      for (n = 0; n < slashes; ++n) push('\\');
    }
    push(argument[i++]);
  }
  push('"');
}

int main(int argc, char **argv) {
  STARTUPINFOA startup;
  PROCESS_INFORMATION information;
  DWORD status = 1;
  int i;

  push_text(FORWARD_TO);
  for (i = 1; i < argc; ++i) {
    push(' ');
    push_quoted(argv[i]);
  }
  line[used] = '\0';

  memset(&startup, 0, sizeof(startup));
  startup.cb = sizeof(startup);
  memset(&information, 0, sizeof(information));
  /* Inherit this process's streams so the script's output reaches whatever
   * pipes the driver set up, and claim no job or process group of our own. */
  if (!CreateProcessA(NULL, line, NULL, NULL, TRUE, 0, NULL, NULL, &startup,
                      &information)) {
    fprintf(stderr, "launcher: cannot run: %s\n", line);
    return 127;
  }
  CloseHandle(information.hThread);
  WaitForSingleObject(information.hProcess, INFINITE);
  if (!GetExitCodeProcess(information.hProcess, &status)) status = 1;
  CloseHandle(information.hProcess);
  return (int)status;
}
