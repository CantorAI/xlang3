#include "xlang3/builtins.h"
#include "xlang3/functional_iterators.h"
#include "xlang3/module_object.h"
#include "xlang3/sequence.h"

#if !defined(_WIN32)
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <grp.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#else
extern char** environ;
#endif
#if defined(__linux__)
#include <sys/syscall.h>
#endif

namespace xlang3 {
namespace {
char** process_environment() {
#if defined(__APPLE__)
  return *_NSGetEnviron();
#else
  return ::environ;
#endif
}

bool os_error(Runtime& runtime, std::string& error) {
  const int code = errno;
  error = std::strerror(code);
  runtime.raise_class_error(code == ECHILD ? "ChildProcessError" : "OSError", error);
  return false;
}

bool integer(const Value& value, int& out, std::string& error) {
  int64_t number;
  if (!value_int_like_to_i64(value, number) || number < INT_MIN || number > INT_MAX) {
    error = "expected a C integer";
    return false;
  }
  out = static_cast<int>(number);
  return true;
}

bool text_arg(Runtime& runtime, const Value& value, std::string& out, std::string& error) {
  if (auto* text = value_as_string(value)) out = string_object_to_string(*text);
  else if (auto* bytes = value_as_bytes(value)) out = bytes_object_to_string(*bytes);
  else { error = "process argument must be str or bytes"; return false; }
  if (out.find('\0') != std::string::npos) {
    error = "embedded null in process argument";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  return true;
}

bool string_array(Runtime& runtime, const Value& value, std::vector<std::string>& strings,
                  std::vector<char*>& pointers, std::string& error) {
  std::vector<Value> items;
  if (!runtime_collect_iterable(runtime, value, items, error)) return false;
  strings.resize(items.size());
  for (size_t i = 0; i < items.size(); ++i)
    if (!text_arg(runtime, items[i], strings[i], error)) return false;
  for (auto& text : strings) pointers.push_back(text.data());
  pointers.push_back(nullptr);
  return true;
}

// Only async-signal-safe operations are permitted between fork and exec.
[[noreturn]] void child_error(int fd, int code, const char* detail) {
  char buffer[96] = "OSError:";
  size_t size = 8;
  char digits[16];
  size_t count = 0;
  unsigned int number = static_cast<unsigned int>(code);
  do { digits[count++] = "0123456789abcdef"[number & 15]; number >>= 4; } while (number);
  while (count) buffer[size++] = digits[--count];
  buffer[size++] = ':';
  while (*detail && size < sizeof(buffer)) buffer[size++] = *detail++;
  size_t written = 0;
  while (written < size) {
    const auto n = ::write(fd, buffer + written, size - written);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) break;
    written += static_cast<size_t>(n);
  }
  _exit(255);
}

void close_range_except(int first, int last) {
  if (first > last) return;
#if defined(__linux__) && defined(SYS_close_range)
  if (::syscall(SYS_close_range, static_cast<unsigned int>(first),
                static_cast<unsigned int>(last), 0) == 0) return;
#endif
  for (int fd = first; fd < last; ++fd) ::close(fd);
  ::close(last);
}

bool fork_exec(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  if (argc != 22) { error = "fork_exec() expected 22 arguments"; return false; }
  if (args[21].tag != ValueTag::None) {
    error = "preexec_fn is not supported in the multithreaded XLang3 runtime";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  std::vector<std::string> arguments, executables, environment;
  std::vector<char*> argv, paths, envp;
  if (!string_array(runtime, args[0], arguments, argv, error) ||
      !string_array(runtime, args[1], executables, paths, error)) return false;
  if (arguments.empty() || executables.empty()) { error = "empty process arguments"; return false; }
  const bool inherit_environment = args[5].tag == ValueTag::None;
  char** inherited_environment = process_environment();
  if (!inherit_environment && !string_array(runtime, args[5], environment, envp, error)) return false;
  std::string cwd;
  if (args[4].tag != ValueTag::None && !text_arg(runtime, args[4], cwd, error)) return false;
  int fds[8];
  for (size_t i = 0; i < 8; ++i) if (!integer(args[i + 6], fds[i], error)) return false;
  const int errpipe = fds[7];
  if (errpipe < 3) { error = "error pipe descriptor must be >= 3"; return false; }
  std::vector<Value> keep_values;
  if (!runtime_collect_iterable(runtime, args[3], keep_values, error)) return false;
  std::vector<int> keep;
  for (const auto& value : keep_values) {
    int fd;
    if (!integer(value, fd, error) || fd < 0) { error = "invalid pass_fds descriptor"; return false; }
    keep.push_back(fd);
  }
  keep.push_back(errpipe);
  std::sort(keep.begin(), keep.end());
  keep.erase(std::unique(keep.begin(), keep.end()), keep.end());
  int group, mask, uid = -1, gid = -1;
  if (!integer(args[16], group, error) || !integer(args[20], mask, error)) return false;
  if (args[17].tag != ValueTag::None && !integer(args[17], gid, error)) return false;
  if (args[19].tag != ValueTag::None && !integer(args[19], uid, error)) return false;
  if ((args[17].tag != ValueTag::None && gid < 0) ||
      (args[19].tag != ValueTag::None && uid < 0)) {
    error = "user and group IDs must be non-negative";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  std::vector<gid_t> groups;
  const bool set_groups = args[18].tag != ValueTag::None;
  if (set_groups) {
    std::vector<Value> values;
    if (!runtime_collect_iterable(runtime, args[18], values, error)) return false;
    for (const auto& value : values) {
      int id;
      if (!integer(value, id, error) || id < 0) { error = "invalid group ID"; return false; }
      groups.push_back(static_cast<gid_t>(id));
    }
  }
  const bool close_fds = value_truthy(args[2]);
  const bool restore_signals = value_truthy(args[14]);
  const bool new_session = value_truthy(args[15]);
  struct rlimit limit;
  if (::getrlimit(RLIMIT_NOFILE, &limit) < 0) return os_error(runtime, error);
  rlim_t descriptor_limit = limit.rlim_cur;
  if (descriptor_limit == RLIM_INFINITY || descriptor_limit > static_cast<rlim_t>(INT_MAX)) {
    const long open_max = ::sysconf(_SC_OPEN_MAX);
    descriptor_limit = open_max > 0 ? static_cast<rlim_t>(open_max) : 256;
  }
  const int max_fd = static_cast<int>(descriptor_limit);
  const pid_t pid = ::fork();
  if (pid < 0) return os_error(runtime, error);
  if (pid == 0) {
    if (::fcntl(errpipe, F_SETFD, FD_CLOEXEC) < 0) child_error(errpipe, errno, "noexec");
    // Duplicate all sources before replacing stdio, including crossed 0/1/2 descriptors.
    int sources[3] = {fds[0], fds[3], fds[5]};
    for (int& fd : sources) {
      if (fd >= 0) {
        fd = ::fcntl(fd, F_DUPFD_CLOEXEC, 3);
        if (fd < 0) child_error(errpipe, errno, "noexec");
      }
    }
    for (int i = 0; i < 3; ++i) {
      if (sources[i] >= 0) {
        if (::dup2(sources[i], i) < 0) child_error(errpipe, errno, "noexec");
        ::close(sources[i]);
      }
    }
    for (int fd : {fds[1], fds[2], fds[4], fds[6]}) if (fd >= 3 && fd != errpipe) ::close(fd);
    for (int fd : {fds[0], fds[3], fds[5]}) {
      if (fd >= 3 && fd != errpipe && !std::binary_search(keep.begin(), keep.end(), fd)) ::close(fd);
    }
    for (int fd : keep) {
      if (fd != errpipe && ::fcntl(fd, F_SETFD, 0) < 0) child_error(errpipe, errno, "noexec");
    }
    if (!cwd.empty() && ::chdir(cwd.c_str()) < 0) child_error(errpipe, errno, "noexec:chdir");
    if (mask >= 0) ::umask(static_cast<mode_t>(mask));
    if (restore_signals) {
      struct sigaction action {};
      action.sa_handler = SIG_DFL;
      sigemptyset(&action.sa_mask);
      if (::sigaction(SIGPIPE, &action, nullptr) < 0) child_error(errpipe, errno, "noexec");
#ifdef SIGXFZ
      ::sigaction(SIGXFZ, &action, nullptr);
#endif
#ifdef SIGXFSZ
      ::sigaction(SIGXFSZ, &action, nullptr);
#endif
    }
    if (new_session && ::setsid() < 0) child_error(errpipe, errno, "noexec");
    if (group >= 0 && ::setpgid(0, group) < 0) child_error(errpipe, errno, "noexec");
    if (set_groups && ::setgroups(groups.size(), groups.data()) < 0) child_error(errpipe, errno, "noexec");
    if (gid >= 0 && ::setgid(static_cast<gid_t>(gid)) < 0) child_error(errpipe, errno, "noexec");
    if (uid >= 0 && ::setuid(static_cast<uid_t>(uid)) < 0) child_error(errpipe, errno, "noexec");
    if (close_fds) {
      int first = 3;
      for (int fd : keep) {
        if (fd < first) continue;
        close_range_except(first, fd - 1);
        first = fd == INT_MAX ? INT_MAX : fd + 1;
      }
      if (first < max_fd) close_range_except(first, max_fd - 1);
    }
    int saved_errno = 0;
    for (const auto& executable : executables) {
      ::execve(executable.c_str(), argv.data(),
               inherit_environment ? inherited_environment : envp.data());
      if (errno != ENOENT && errno != ENOTDIR && saved_errno == 0) saved_errno = errno;
    }
    child_error(errpipe, saved_errno ? saved_errno : errno, "");
  }
  out = Value::int64(pid);
  return true;
}

bool wait_pid(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  int pid, options;
  if (argc != 2 || !integer(args[0], pid, error) || !integer(args[1], options, error)) return false;
  int status = 0;
  pid_t result;
  do { result = ::waitpid(pid, &status, options); } while (result < 0 && errno == EINTR);
  if (result < 0) return os_error(runtime, error);
  out = Value::tuple({Value::int64(result), Value::int64(status)});
  return true;
}

bool kill_process(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void*) {
  int pid, signal;
  if (argc != 2 || !integer(args[0], pid, error) || !integer(args[1], signal, error)) return false;
  if (::kill(pid, signal) < 0) return os_error(runtime, error);
  out = Value::none();
  return true;
}

bool wait_status(Runtime& runtime, const Value* args, uint32_t argc, Value& out, std::string& error, void* data) {
  int status;
  if (argc != 1 || !integer(args[0], status, error)) return false;
  const auto operation = reinterpret_cast<intptr_t>(data);
  if (operation == 1) out = Value::boolean(WIFSTOPPED(status));
  else if (operation == 2) out = Value::int64(WSTOPSIG(status));
  else if (WIFEXITED(status)) out = Value::int64(WEXITSTATUS(status));
  else if (WIFSIGNALED(status)) out = Value::int64(-WTERMSIG(status));
  else {
    error = "invalid wait status";
    runtime.raise_class_error("ValueError", error);
    return false;
  }
  return true;
}
}

void register_posix_process_module(Runtime& runtime) {
  NativeModuleBuilder process(runtime, "_posixsubprocess");
  process.function("fork_exec", fork_exec);
  runtime.register_module("_posixsubprocess", process.finish());
  Value os;
  std::string error;
  if (!runtime.import_module("posix", os, error)) return;
  module_set_attr(os, "waitpid", runtime.make_native_function("os.waitpid", wait_pid), error);
  module_set_attr(os, "kill", runtime.make_native_function("os.kill", kill_process), error);
  module_set_attr(os, "waitstatus_to_exitcode", runtime.make_native_function("os.waitstatus_to_exitcode", wait_status), error);
  module_set_attr(os, "WIFSTOPPED", runtime.make_native_function("os.WIFSTOPPED", wait_status, reinterpret_cast<void*>(1)), error);
  module_set_attr(os, "WSTOPSIG", runtime.make_native_function("os.WSTOPSIG", wait_status, reinterpret_cast<void*>(2)), error);
  module_set_attr(os, "WNOHANG", Value::int64(WNOHANG), error);
}
}
#endif
