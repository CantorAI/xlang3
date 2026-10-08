/*
Copyright (C) 2026 CantorAI Inc. and The XLang Foundation
Licensed under the Apache License, Version 2.0
*/
#include "xlang3/xlang3.h"
#include "sqlite_windows_file_path.h"
#include <algorithm>
#include <cwchar>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void require(bool value, const char* reason) {
  if (!value) throw std::runtime_error(reason);
}
std::string utf8(const std::wstring& path) { return std::filesystem::path(path).u8string(); }
std::wstring extended(const std::wstring& path) { return L"\\\\?\\" + path; }

struct Owned {
  std::wstring root, previous;
  std::vector<std::wstring> directories, files;
  Owned() {
    root = (std::filesystem::canonical(std::filesystem::temp_directory_path()) /
            ("xlang3-sqlite-long-" + std::to_string(std::random_device{}()))).native();
    require(root.size() >= 3 && root[1] == L':' && root[2] == L'\\' &&
            root.size() < 180 && CreateDirectoryW(root.c_str(), nullptr), "owned root creation failed");
    directories.push_back(root);
    previous.resize(32768);
    const DWORD count = GetCurrentDirectoryW(DWORD(previous.size()), previous.data());
    require(count && count < previous.size(), "cwd capture failed"); previous.resize(count);
    require(SetCurrentDirectoryW(root.c_str()), "cwd activation failed");
  }
  ~Owned() { if (!previous.empty()) SetCurrentDirectoryW(previous.c_str()); }
  bool contains(const std::wstring& path) const {
    const auto prefix = root + L"\\";
    return path.size() > prefix.size() && path.compare(0, prefix.size(), prefix) == 0;
  }
  void own(const std::wstring& path) {
    require(contains(path), "owned path escaped root");
    if (std::find(files.begin(), files.end(), path) == files.end()) files.push_back(path);
  }
  std::wstring directory(size_t length) {
    std::wstring result = root;
    while (result.size() < length) {
      const auto remaining = length - result.size();
      require(remaining >= 2, "directory boundary invalid");
      size_t part = (std::min)(size_t(60), remaining - 1);
      if (remaining - part - 1 == 1) --part;
      result += L"\\" + std::wstring(part, L'd');
      if (std::find(directories.begin(), directories.end(), result) == directories.end()) {
        require(CreateDirectoryW(extended(result).c_str(), nullptr), "owned directory creation failed");
        directories.push_back(result);
      }
    }
    return result;
  }
  void database(const std::wstring& path) {
    own(path); own(path + L"-wal"); own(path + L"-shm"); own(path + L"-journal");
  }
  void control(const std::wstring& path, const std::string& bytes) {
    own(path);
    HANDLE file = CreateFileW(extended(path).c_str(), GENERIC_WRITE, 0, nullptr,
                             CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    require(file != INVALID_HANDLE_VALUE, "control file creation failed");
    DWORD written = 0;
    const bool ok = WriteFile(file, bytes.data(), DWORD(bytes.size()), &written, nullptr) != 0;
    const bool closed = CloseHandle(file) != 0;
    require(ok && written == bytes.size() && closed, "control bytes failed");
  }
  void private_error(const std::string& message) noexcept {
    const auto path = root + L"\\private-native-error.log";
    HANDLE file = CreateFileW(extended(path).c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written = 0;
    WriteFile(file, message.data(), DWORD((std::min)(message.size(), size_t(16384))), &written, nullptr);
    CloseHandle(file);
  }
  void cleanup() {
    require(SetCurrentDirectoryW(previous.c_str()), "cwd restoration failed");
    for (const auto& path : files) {
      require(contains(path), "cleanup path escaped root");
      SetFileAttributesW(extended(path).c_str(), FILE_ATTRIBUTE_NORMAL);
      if (!DeleteFileW(extended(path).c_str()))
        require(GetLastError() == ERROR_FILE_NOT_FOUND, "owned file cleanup failed");
    }
    for (auto i = directories.rbegin(); i != directories.rend(); ++i) {
      require(*i == root || contains(*i), "cleanup directory escaped root");
      require(RemoveDirectoryW(extended(*i).c_str()), "owned directory cleanup failed");
    }
  }
};

struct Db {
  sqlite3* value = nullptr;
  explicit Db(const std::string& path, int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE) {
    const int result = xlang3_sqlite::open_native_database(path.c_str(), &value, flags);
    if (result != SQLITE_OK) {
      if (value) sqlite3_close(value); value = nullptr;
      throw std::runtime_error("native SQLite connection failed");
    }
  }
  ~Db() { if (value) sqlite3_close(value); }
  void exec(const char* sql) { require(sqlite3_exec(value, sql, nullptr, nullptr, nullptr) == SQLITE_OK, "native SQLite SQL failed"); }
  int scalar(const char* sql) {
    sqlite3_stmt* statement = nullptr;
    require(sqlite3_prepare_v2(value, sql, -1, &statement, nullptr) == SQLITE_OK, "native scalar prepare failed");
    const int step = sqlite3_step(statement);
    const int result = step == SQLITE_ROW ? sqlite3_column_int(statement, 0) : -1;
    require(sqlite3_finalize(statement) == SQLITE_OK && step == SQLITE_ROW, "native scalar failed");
    return result;
  }
};

// Independent synthetic system-table fault: all original pointers must be
// restored if the third configurable syscall rejects installation.
struct Fake {
  std::array<sqlite3_syscall_ptr, 4> values;
  std::array<sqlite3_syscall_ptr, 4> original;
  bool fail_once = true;
};
sqlite3_syscall_ptr fake_get(sqlite3_vfs* vfs, const char* name) {
  auto* data = static_cast<Fake*>(vfs->pAppData);
  for (size_t i = 0; i < xlang3_sqlite::windows_path_detail::names().size(); ++i)
    if (!std::strcmp(name, xlang3_sqlite::windows_path_detail::names()[i])) return data->values[i];
  return nullptr;
}
int fake_set(sqlite3_vfs* vfs, const char* name, sqlite3_syscall_ptr replacement) {
  auto* data = static_cast<Fake*>(vfs->pAppData);
  for (size_t i = 0; i < xlang3_sqlite::windows_path_detail::names().size(); ++i) {
    if (std::strcmp(name, xlang3_sqlite::windows_path_detail::names()[i])) continue;
    if (i == 2 && data->fail_once && replacement != data->original[i]) {
      data->fail_once = false; return SQLITE_ERROR;
    }
    data->values[i] = replacement; return SQLITE_OK;
  }
  return SQLITE_NOTFOUND;
}
void installation_contract() {
  using namespace xlang3_sqlite::windows_path_detail;
  sqlite3_vfs* actual = sqlite3_vfs_find("win32-longpath");
  require(actual && actual->iVersion == 3, "required native VFS unavailable");
  Fake fake{};
  for (size_t i = 0; i < names().size(); ++i) fake.values[i] = fake.original[i] = actual->xGetSystemCall(actual, names()[i]);
  sqlite3_vfs tested = *actual; tested.pAppData = &fake;
  tested.xGetSystemCall = &fake_get; tested.xSetSystemCall = &fake_set;
  Hooks state;
  require(install(nullptr, state) == SQLITE_CANTOPEN, "missing VFS admitted");
  tested.iVersion = 2;
  require(install(&tested, state) == SQLITE_CANTOPEN, "wrong VFS version admitted");
  tested.iVersion = 3;
  require(install(&tested, state) == SQLITE_CANTOPEN && !state.installed && fake.values == fake.original,
          "partial hook installation did not restore originals");
  tested.xGetSystemCall = nullptr;
  require(install(&tested, state) == SQLITE_CANTOPEN && fake.values == fake.original,
          "missing syscall lookup admitted");
  sqlite3_vfs* default_vfs = sqlite3_vfs_find(nullptr);
  const auto locking_open = actual->xOpen;
  require(xlang3_sqlite::initialize_native_database_paths() == SQLITE_OK &&
      sqlite3_vfs_find(nullptr) == default_vfs && actual->xOpen == locking_open,
      "default VFS or locking methods changed");
  require(xlang3_sqlite::initialize_native_database_paths() == SQLITE_OK, "repeat initialization failed");
  char too_small[8]{};
  require(actual->xFullPathname(actual, "a-native-file.sqlite", sizeof(too_small), too_small) == SQLITE_CANTOPEN,
          "truncated canonical filename accepted");
  std::string oversized(size_t(actual->mxPathname), 'x');
  std::vector<char> canonical(size_t(actual->mxPathname) + 1);
  require(actual->xFullPathname(actual, oversized.c_str(), int(canonical.size()), canonical.data()) == SQLITE_CANTOPEN,
          "oversized filename accepted");
}

void retry_error_contract(const std::wstring& path) {
  using xlang3_sqlite::windows_path_detail::retry_path;
  int calls = 0;
  bool original_path = false, retry_path_valid = false, retry_error_valid = false;
  const auto expected = extended(path);
  const BOOL failed = retry_path(path.c_str(), FALSE, [&](LPCWSTR value) {
    ++calls;
    if (calls == 1) {
      original_path = std::wcscmp(value, path.c_str()) == 0;
      SetLastError(ERROR_PATH_NOT_FOUND); errno = ENOENT;
    } else {
      retry_error_valid = GetLastError() == ERROR_PATH_NOT_FOUND && errno == ENOENT;
      retry_path_valid = std::wcscmp(value, expected.c_str()) == 0;
      SetLastError(ERROR_ACCESS_DENIED); errno = EACCES;
    }
    return FALSE;
  });
  require(!failed && calls == 2 && original_path && retry_path_valid && retry_error_valid &&
          GetLastError() == ERROR_ACCESS_DENIED && errno == EACCES,
          "actual retry failure error was lost");
  calls = 0;
  original_path = false;
  const BOOL passed = retry_path(path.c_str(), FALSE, [&](LPCWSTR value) {
    original_path = std::wcscmp(value, path.c_str()) == 0;
    ++calls; SetLastError(ERROR_SUCCESS); return TRUE;
  });
  require(passed && original_path && calls == 1 && GetLastError() == ERROR_SUCCESS, "successful syscall was retried");
  calls = 0;
  original_path = false;
  retry_path(L"NUL", FALSE, [&](LPCWSTR value) {
    original_path = std::wcscmp(value, L"NUL") == 0;
    ++calls; SetLastError(ERROR_ACCESS_DENIED); return FALSE;
  });
  require(original_path && calls == 1 && GetLastError() == ERROR_ACCESS_DENIED, "device failure was retried");
}

void native_sqlite(const std::wstring& path) {
  const auto name = utf8(path);
  {
    Db first(name), second(name);
    require(std::string(sqlite3_db_filename(first.value, "main")) == name,
            "public canonical filename was rewritten");
    first.exec("PRAGMA journal_mode=DELETE");
    first.exec("CREATE TABLE locked(value INTEGER)");
    first.exec("BEGIN IMMEDIATE; INSERT INTO locked VALUES(7)");
    require(GetFileAttributesW(extended(path + L"-journal").c_str()) != INVALID_FILE_ATTRIBUTES,
            "actual rollback journal missing");
    const int busy = sqlite3_exec(second.value, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr);
    require(busy == SQLITE_BUSY, "SQLite write locking weakened");
    first.exec("ROLLBACK");
    require(GetFileAttributesW(extended(path + L"-journal").c_str()) == INVALID_FILE_ATTRIBUTES,
            "rollback journal deletion failed");
    second.exec("BEGIN IMMEDIATE; INSERT INTO locked VALUES(11); COMMIT");
    require(first.scalar("SELECT value FROM locked") == 11, "journal commit/reopen data failed");
    first.exec("PRAGMA journal_mode=WAL");
    first.exec("INSERT INTO locked VALUES(13)");
    require(GetFileAttributesW(extended(path + L"-wal").c_str()) != INVALID_FILE_ATTRIBUTES &&
            GetFileAttributesW(extended(path + L"-shm").c_str()) != INVALID_FILE_ATTRIBUTES,
            "actual WAL/SHM files missing");
    require(second.scalar("SELECT count(*) FROM locked") == 2, "WAL visibility failed");
  }
  {
    Db reopened(name);
    require(reopened.scalar("SELECT count(*) FROM locked") == 2, "actual closed database reopen failed");
  }
  auto forward = path; std::replace(forward.begin(), forward.end(), L'\\', L'/');
  {
    Db readonly("file:///" + utf8(forward) + "?mode=ro", SQLITE_OPEN_READONLY | SQLITE_OPEN_URI);
    require(sqlite3_db_readonly(readonly.value, "main") == 1 && readonly.scalar("SELECT count(*) FROM locked") == 2,
            "explicit native C read-only URI failed");
    const char* canonical = sqlite3_db_filename(readonly.value, "main");
    const char* mode = canonical ? sqlite3_uri_parameter(canonical, "mode") : nullptr;
    require(canonical && std::string(canonical) == name &&
            mode && std::strcmp(mode, "ro") == 0,
            "URI metadata or public filename changed");
    require(sqlite3_exec(readonly.value, "INSERT INTO locked VALUES(99)", nullptr, nullptr, nullptr) == SQLITE_READONLY,
            "read-only URI permitted write");
  }
  {
    // A read-only WAL connection cannot checkpoint/delete its sidecars on
    // close. Exercise actual deletion with a separate writable owner.
    Db cleanup(name);
    require(cleanup.scalar("SELECT count(*) FROM locked") == 2 &&
            cleanup.scalar("SELECT sum(value) FROM locked") == 24,
            "writable cleanup reopen changed retained data");
    int log_frames = -1, checkpointed_frames = -1;
    const int checkpoint = sqlite3_wal_checkpoint_v2(cleanup.value, "main",
        SQLITE_CHECKPOINT_TRUNCATE, &log_frames, &checkpointed_frames);
    require(checkpoint == SQLITE_OK && log_frames == 0 && checkpointed_frames == 0,
            "actual truncate checkpoint failed");
    sqlite3_stmt* mode = nullptr;
    require(sqlite3_prepare_v2(cleanup.value, "PRAGMA journal_mode=DELETE", -1,
                              &mode, nullptr) == SQLITE_OK, "delete journal mode prepare failed");
    const int mode_step = sqlite3_step(mode);
    const auto* mode_text = mode_step == SQLITE_ROW ? sqlite3_column_text(mode, 0) : nullptr;
    const bool mode_delete = mode_text && std::strcmp(reinterpret_cast<const char*>(mode_text), "delete") == 0;
    const int mode_finalized = sqlite3_finalize(mode);
    require(mode_step == SQLITE_ROW && mode_delete && mode_finalized == SQLITE_OK,
            "actual delete journal mode failed");
  }
  require(GetFileAttributesW(extended(path + L"-wal").c_str()) == INVALID_FILE_ATTRIBUTES &&
          GetFileAttributesW(extended(path + L"-shm").c_str()) == INVALID_FILE_ATTRIBUTES &&
          GetFileAttributesW(extended(path + L"-journal").c_str()) == INVALID_FILE_ATTRIBUTES,
          "closed journal/WAL/SHM cleanup failed");
}

void actual_module(Owned& owned, const std::wstring& db280, const std::wstring& schema288,
                   const std::wstring& unicode, const char* modules) {
  std::string control = "Native schema UTF-8 \xe9\x9b\xaa"; control.push_back('\0'); control += "tail";
  owned.control(schema288, control);
  X::Runtime runtime; runtime.AddImportRoot(modules);
  auto globals = runtime.Dict();
  const auto at = db280.find_last_of(L'\\');
  const auto dotted = db280.substr(0, at) + L"\\.\\unused\\.." + db280.substr(at);
  require(globals.Set("db280", X::Value(runtime, utf8(db280))) &&
          globals.Set("schema288", X::Value(runtime, utf8(schema288))) &&
          globals.Set("unicode_db", X::Value(runtime, utf8(unicode))) &&
          globals.Set("relative_db", X::Value(runtime, utf8(db280.substr(owned.root.size() + 1)))) &&
          globals.Set("dotted_db", X::Value(runtime, utf8(dotted))) &&
          globals.Set("control", X::Value::Bytes(runtime.host(), control.data(), control.size())),
          "actual module globals failed");
  const char* source = R"fixture(
fixture_ordinal = 0
fixture_legacy_rc = -1
fixture_stage = 'imports'
import sqlite3
from xlang_sqlite3 import sqlite
fixture_stage = 'schema_read'
with open(schema288, 'rb') as stream:
    assert stream.name == schema288 and stream.read() == control
for fixture_ordinal, filename in enumerate((db280, unicode_db, ':memory:')):
    fixture_stage = 'python_open'
    connection = sqlite3.connect(filename, isolation_level=None)
    fixture_stage = 'python_isolation'
    assert connection.isolation_level is None and not connection.in_transaction
    fixture_stage = 'python_create'
    connection.execute('CREATE TABLE text_roundtrip(value TEXT)')
    text = '\u96ea\0tail'
    fixture_stage = 'python_insert'
    connection.execute('INSERT INTO text_roundtrip VALUES(?)', (text,))
    fixture_stage = 'python_text'
    read_cursor = connection.execute('SELECT value FROM text_roundtrip')
    try:
        assert read_cursor.fetchone()[0] == text
    finally:
        read_cursor.close()
    fixture_stage = 'python_isolation_set'
    connection.isolation_level = 'IMMEDIATE'
    fixture_stage = 'python_transaction'
    connection.execute('INSERT INTO text_roundtrip VALUES(?)', ('rolled-back',))
    assert connection.in_transaction
    fixture_stage = 'python_rollback'
    connection.rollback()
    fixture_stage = 'python_count'
    read_cursor = connection.execute('SELECT count(*) FROM text_roundtrip')
    try:
        assert read_cursor.fetchone()[0] == 1
    finally:
        read_cursor.close()
    fixture_stage = 'python_close'
    connection.close()
for fixture_ordinal, filename in enumerate((db280, unicode_db)):
    fixture_stage = 'python_reopen'
    connection = sqlite3.connect(filename)
    fixture_stage = 'python_reopen_text'
    read_cursor = connection.execute('SELECT value FROM text_roundtrip')
    try:
        assert read_cursor.fetchone()[0] == '\u96ea\0tail'
    finally:
        read_cursor.close()
    fixture_stage = 'python_reopen_close'
    connection.close()
    fixture_stage = 'legacy_open'
    legacy = sqlite.Database(filename)
    fixture_stage = 'legacy_create'
    fixture_legacy_rc = legacy.exec('CREATE TABLE legacy(value INTEGER)')
    assert fixture_legacy_rc == sqlite.OK
    fixture_stage = 'legacy_begin'
    assert legacy.beginTransaction()
    fixture_stage = 'legacy_insert'
    assert legacy.exec('INSERT INTO legacy VALUES(19)') == sqlite.OK
    fixture_stage = 'legacy_commit_close'
    assert legacy.endTransaction() and legacy.close()
    fixture_stage = 'queryonly_open'
    connection = sqlite3.connect(filename, isolation_level=None)
    connection.execute('PRAGMA query_only=ON')
    fixture_stage = 'queryonly_read'
    read_cursor = connection.execute('SELECT value FROM legacy')
    try:
        assert read_cursor.fetchone()[0] == 19
    finally:
        read_cursor.close()
    fixture_stage = 'queryonly_close'
    connection.close()
for fixture_ordinal, filename in enumerate((relative_db, dotted_db, db280.replace('\\', '/'), '\\\\?\\' + db280)):
    fixture_stage = 'pathforms_open'
    connection = sqlite3.connect(filename, isolation_level=None)
    fixture_stage = 'pathforms_read'
    read_cursor = connection.execute('SELECT value FROM text_roundtrip')
    try:
        assert read_cursor.fetchone()[0] == '\u96ea\0tail'
    finally:
        read_cursor.close()
    fixture_stage = 'pathforms_close'
    connection.close()
fixture_stage = 'uri_keyword'
try:
    sqlite3.connect(db280, uri=True)
except TypeError:
    pass
else:
    assert False
fixture_stage = 'uri_default_policy'
try:
    sqlite3.connect('file:' + db280 + '?mode=ro')
except sqlite3.OperationalError:
    pass
else:
    assert False
)fixture";
  X::Module builtins(runtime, "builtins"); X::Value result;
  if (!builtins["exec"].Call({X::Value(runtime, source), globals}, result)) {
    const auto actual_error = runtime.LastError();
    const auto stage_value = globals.Get("fixture_stage");
    const auto ordinal_value = globals.Get("fixture_ordinal");
    const auto legacy_rc_value = globals.Get("fixture_legacy_rc");
    const std::array<const char*, 28> stages = {{
      "imports", "schema_read", "python_open", "python_isolation", "python_create",
      "python_insert", "python_text", "python_isolation_set", "python_transaction",
      "python_rollback", "python_count", "python_close", "python_reopen",
      "python_reopen_text", "python_reopen_close", "legacy_open", "legacy_create",
      "legacy_begin", "legacy_insert", "legacy_commit_close", "queryonly_open",
      "queryonly_read", "queryonly_close", "pathforms_open", "pathforms_read",
      "pathforms_close", "uri_keyword", "uri_default_policy"
    }};
    std::string stage = "unknown";
    if (stage_value.IsString()) {
      const auto candidate = stage_value.ToString();
      for (const auto* allowed : stages) if (candidate == allowed) stage = allowed;
    }
    const auto ordinal = ordinal_value.IsInt64() ? ordinal_value.ToInt64() : -1;
    const auto bounded_ordinal = ordinal >= 0 && ordinal <= 3 ? ordinal : -1;
    const auto legacy_rc = legacy_rc_value.IsInt64() ? legacy_rc_value.ToInt64() : -1;
    const auto bounded_legacy_rc = legacy_rc >= 0 && legacy_rc <= 255 ? legacy_rc : -1;
    owned.private_error("stage=" + stage + ";ordinal=" + std::to_string(bounded_ordinal) +
                        ";legacy_rc=" + std::to_string(bounded_legacy_rc) +
                        "\n" + actual_error);
    throw std::runtime_error("actual SQLite package fixture failed");
  }
}
} // namespace

int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "native_sqlite_longpath_modules_required\n"; return 2; }
  std::unique_ptr<Owned> owned;
  try {
    owned = std::make_unique<Owned>();
    installation_contract();
    const auto directory = owned->directory(267);
    const auto db280 = directory + L"\\state.sqlite";
    const auto schema288 = directory + L"\\expected-schema.json";
    require(db280.size() == 280 && schema288.size() == 288, "exact long boundaries changed");
    retry_error_contract(db280);
    owned->database(db280);
    std::wstring unicode_directory = owned->root;
    for (size_t i = 0; i < 7; ++i) {
      unicode_directory += L"\\" + std::wstring(60, L'\u754c');
      require(CreateDirectoryW(extended(unicode_directory).c_str(), nullptr), "Unicode directory creation failed");
      owned->directories.push_back(unicode_directory);
    }
    const auto unicode = unicode_directory + L"\\unicode.sqlite";
    require(utf8(unicode).size() > 1040, "Unicode path did not exceed old UTF-8 VFS bound");
    owned->database(unicode);
    native_sqlite(db280); native_sqlite(unicode);
    actual_module(*owned, db280, schema288, unicode, argv[1]);
    { Db memory(":memory:"); memory.exec("CREATE TABLE memory(value INTEGER)"); }
    sqlite3* absent = nullptr;
    const auto missing = utf8(owned->root + L"\\never-created\\missing.sqlite");
    require(xlang3_sqlite::open_native_database(missing.c_str(), &absent) == SQLITE_CANTOPEN,
            "missing directory unexpectedly opened");
    if (absent) sqlite3_close(absent);
    owned->cleanup();
    std::cout << "Native SQLite exact280 DB/exact288 schema, Unicode>1040 UTF-8 bytes, actual module/legacy API/text-NUL/isolation, write locking/journal/WAL/SHM/reopen/read-only C URI/memory and owned cleanup passed. No CPython.\n";
    return 0;
  } catch (const std::exception& error) {
    if (owned) owned->private_error(error.what());
    std::cerr << "native_sqlite_longpath_contract_failed; disposable fixture retained\n";
    return 1;
  }
}
