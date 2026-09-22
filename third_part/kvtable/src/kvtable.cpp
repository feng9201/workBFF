#include "kvtable.h"

#include <cerrno>
#include <cstddef>
#include <cstdio>
#include <cstring>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace kvtable {
namespace {

constexpr std::size_t kMaxKeyLen = 4096;
constexpr std::size_t kMaxColLen = 256 * 1024;
constexpr const char *kMagic = "kvtable";
constexpr int kVersion = 1;

Status check_fields(const std::string &key, const std::string &c2, const std::string &c3) {
    if (key.empty() || key.size() > kMaxKeyLen) {
        return Status::InvalidArg;
    }
    if (c2.size() > kMaxColLen || c3.size() > kMaxColLen) {
        return Status::InvalidArg;
    }
    return Status::Ok;
}

void append_json_string(std::string &out, const std::string &s) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
                break;
        }
    }
    out.push_back('"');
}

std::string encode_json(
    const std::vector<std::pair<std::string, std::pair<std::string, std::string>>> &rows) {
    std::string out;
    out.reserve(256 + rows.size() * 64);
    out += "{\n  \"magic\": \"";
    out += kMagic;
    out += "\",\n  \"version\": ";
    out += std::to_string(kVersion);
    out += ",\n  \"rows\": [";
    for (std::size_t i = 0; i < rows.size(); ++i) {
        out += "\n    {\n      \"key\": ";
        append_json_string(out, rows[i].first);
        out += ",\n      \"c2\": ";
        append_json_string(out, rows[i].second.first);
        out += ",\n      \"c3\": ";
        append_json_string(out, rows[i].second.second);
        out += "\n    }";
        if (i + 1 != rows.size()) {
            out += ",";
        }
    }
    if (!rows.empty()) {
        out += "\n  ";
    }
    out += "]\n}\n";
    return out;
}

struct Parser {
    const char *p;
    const char *end;
    bool fail = false;

    void skip_ws() {
        while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
            ++p;
        }
    }

    bool eat(char c) {
        skip_ws();
        if (p < end && *p == c) {
            ++p;
            return true;
        }
        return false;
    }

    bool consume(char c) {
        if (!eat(c)) {
            fail = true;
            return false;
        }
        return true;
    }

    bool parse_string(std::string *out) {
        skip_ws();
        if (p >= end || *p != '"') {
            fail = true;
            return false;
        }
        ++p;
        out->clear();
        while (p < end) {
            char c = *p++;
            if (c == '"') {
                return true;
            }
            if (c == '\\') {
                if (p >= end) {
                    fail = true;
                    return false;
                }
                char e = *p++;
                switch (e) {
                    case '"':
                    case '\\':
                    case '/':
                        out->push_back(e);
                        break;
                    case 'b':
                        out->push_back('\b');
                        break;
                    case 'f':
                        out->push_back('\f');
                        break;
                    case 'n':
                        out->push_back('\n');
                        break;
                    case 'r':
                        out->push_back('\r');
                        break;
                    case 't':
                        out->push_back('\t');
                        break;
                    case 'u': {
                        if (p + 4 > end) {
                            fail = true;
                            return false;
                        }
                        unsigned code = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = *p++;
                            code <<= 4;
                            if (h >= '0' && h <= '9') {
                                code |= static_cast<unsigned>(h - '0');
                            } else if (h >= 'a' && h <= 'f') {
                                code |= static_cast<unsigned>(h - 'a' + 10);
                            } else if (h >= 'A' && h <= 'F') {
                                code |= static_cast<unsigned>(h - 'A' + 10);
                            } else {
                                fail = true;
                                return false;
                            }
                        }
                        if (code < 0x80) {
                            out->push_back(static_cast<char>(code));
                        } else if (code < 0x800) {
                            out->push_back(static_cast<char>(0xC0 | (code >> 6)));
                            out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        } else {
                            out->push_back(static_cast<char>(0xE0 | (code >> 12)));
                            out->push_back(static_cast<char>(0x80 | ((code >> 6) & 0x3F)));
                            out->push_back(static_cast<char>(0x80 | (code & 0x3F)));
                        }
                        break;
                    }
                    default:
                        fail = true;
                        return false;
                }
            } else if (static_cast<unsigned char>(c) < 0x20) {
                fail = true;
                return false;
            } else {
                out->push_back(c);
            }
        }
        fail = true;
        return false;
    }

    bool parse_int(int *out) {
        skip_ws();
        if (p >= end || *p < '0' || *p > '9') {
            fail = true;
            return false;
        }
        int v = 0;
        while (p < end && *p >= '0' && *p <= '9') {
            v = v * 10 + (*p - '0');
            ++p;
        }
        *out = v;
        return true;
    }

    bool skip_value() {
        skip_ws();
        if (p >= end) {
            fail = true;
            return false;
        }
        if (*p == '"') {
            std::string tmp;
            return parse_string(&tmp);
        }
        if (*p == '{') {
            ++p;
            if (eat('}')) {
                return true;
            }
            for (;;) {
                std::string k;
                if (!parse_string(&k) || !consume(':') || !skip_value()) {
                    return false;
                }
                if (eat('}')) {
                    return true;
                }
                if (!consume(',')) {
                    return false;
                }
            }
        }
        if (*p == '[') {
            ++p;
            if (eat(']')) {
                return true;
            }
            for (;;) {
                if (!skip_value()) {
                    return false;
                }
                if (eat(']')) {
                    return true;
                }
                if (!consume(',')) {
                    return false;
                }
            }
        }
        if (p + 4 <= end && std::memcmp(p, "true", 4) == 0) {
            p += 4;
            return true;
        }
        if (p + 5 <= end && std::memcmp(p, "false", 5) == 0) {
            p += 5;
            return true;
        }
        if (p + 4 <= end && std::memcmp(p, "null", 4) == 0) {
            p += 4;
            return true;
        }
        if (*p == '-' || (*p >= '0' && *p <= '9')) {
            if (*p == '-') {
                ++p;
            }
            int dummy = 0;
            return parse_int(&dummy);
        }
        fail = true;
        return false;
    }
};

Status decode_json(
    const std::string &text,
    std::vector<std::pair<std::string, std::pair<std::string, std::string>>> *rows) {
    rows->clear();
    Parser ps;
    ps.p = text.data();
    ps.end = text.data() + text.size();
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        ps.p += 3;
    }
    if (!ps.consume('{')) {
        return Status::Corrupt;
    }
    bool got_magic = false;
    bool got_version = false;
    int version = 0;
    std::string magic;
    if (!ps.eat('}')) {
        for (;;) {
            std::string field;
            if (!ps.parse_string(&field) || !ps.consume(':')) {
                return Status::Corrupt;
            }
            if (field == "magic") {
                if (!ps.parse_string(&magic)) {
                    return Status::Corrupt;
                }
                got_magic = true;
            } else if (field == "version") {
                if (!ps.parse_int(&version)) {
                    return Status::Corrupt;
                }
                got_version = true;
            } else if (field == "rows") {
                if (!ps.consume('[')) {
                    return Status::Corrupt;
                }
                if (!ps.eat(']')) {
                    for (;;) {
                        if (!ps.consume('{')) {
                            return Status::Corrupt;
                        }
                        std::string key, c2, c3;
                        bool has_key = false, has_c2 = false, has_c3 = false;
                        if (!ps.eat('}')) {
                            for (;;) {
                                std::string rk;
                                if (!ps.parse_string(&rk) || !ps.consume(':')) {
                                    return Status::Corrupt;
                                }
                                if (rk == "key") {
                                    if (!ps.parse_string(&key)) {
                                        return Status::Corrupt;
                                    }
                                    has_key = true;
                                } else if (rk == "c2") {
                                    if (!ps.parse_string(&c2)) {
                                        return Status::Corrupt;
                                    }
                                    has_c2 = true;
                                } else if (rk == "c3") {
                                    if (!ps.parse_string(&c3)) {
                                        return Status::Corrupt;
                                    }
                                    has_c3 = true;
                                } else if (!ps.skip_value()) {
                                    return Status::Corrupt;
                                }
                                if (ps.eat('}')) {
                                    break;
                                }
                                if (!ps.consume(',')) {
                                    return Status::Corrupt;
                                }
                            }
                        }
                        if (!has_key || !has_c2 || !has_c3 || key.empty()) {
                            return Status::Corrupt;
                        }
                        for (const auto &row : *rows) {
                            if (row.first == key) {
                                return Status::Corrupt;
                            }
                        }
                        rows->push_back({key, {c2, c3}});
                        if (ps.eat(']')) {
                            break;
                        }
                        if (!ps.consume(',')) {
                            return Status::Corrupt;
                        }
                    }
                }
            } else if (!ps.skip_value()) {
                return Status::Corrupt;
            }
            if (ps.eat('}')) {
                break;
            }
            if (!ps.consume(',')) {
                return Status::Corrupt;
            }
        }
    }
    ps.skip_ws();
    if (ps.fail || ps.p != ps.end || !got_magic || !got_version || magic != kMagic ||
        version != kVersion) {
        return Status::Corrupt;
    }
    return Status::Ok;
}

#ifdef _WIN32

std::wstring utf8_to_wide(const std::string &s) {
    if (s.empty()) {
        return std::wstring();
    }
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(),
                                static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0) {
        return std::wstring();
    }
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), static_cast<int>(s.size()),
                        &w[0], n);
    return w;
}

Status last_io() {
    return Status::IoError;
}

Status read_all(const std::string &path, std::string *out, bool *exists) {
    *exists = false;
    out->clear();
    std::wstring w = utf8_to_wide(path);
    if (w.empty() && !path.empty()) {
        return Status::IoError;
    }
    HANDLE h = CreateFileW(w.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) {
            return Status::Ok;
        }
        return last_io();
    }
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart < 0 || sz.QuadPart > 32 * 1024 * 1024) {
        CloseHandle(h);
        return Status::Corrupt;
    }
    out->resize(static_cast<std::size_t>(sz.QuadPart));
    DWORD got = 0;
    if (sz.QuadPart > 0 &&
        (!ReadFile(h, &(*out)[0], static_cast<DWORD>(out->size()), &got, nullptr) ||
         got != out->size())) {
        CloseHandle(h);
        return last_io();
    }
    CloseHandle(h);
    *exists = true;
    return Status::Ok;
}

Status write_atomic(const std::string &path, const std::string &data) {
    std::wstring w = utf8_to_wide(path);
    std::wstring tmp = utf8_to_wide(path + ".tmp");
    if ((w.empty() && !path.empty()) || tmp.empty()) {
        return Status::IoError;
    }
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return last_io();
    }
    DWORD wrote = 0;
    const char *p = data.data();
    std::size_t left = data.size();
    while (left > 0) {
        DWORD chunk = left > 0x40000000u ? 0x40000000u : static_cast<DWORD>(left);
        if (!WriteFile(h, p, chunk, &wrote, nullptr) || wrote != chunk) {
            CloseHandle(h);
            DeleteFileW(tmp.c_str());
            return last_io();
        }
        p += wrote;
        left -= wrote;
    }
    if (!FlushFileBuffers(h)) {
        CloseHandle(h);
        DeleteFileW(tmp.c_str());
        return last_io();
    }
    CloseHandle(h);
    if (!MoveFileExW(tmp.c_str(), w.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(tmp.c_str());
        return last_io();
    }
    return Status::Ok;
}

#else

Status read_all(const std::string &path, std::string *out, bool *exists) {
    *exists = false;
    out->clear();
    int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        if (errno == ENOENT) {
            return Status::Ok;
        }
        return Status::IoError;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 0 || st.st_size > 32 * 1024 * 1024) {
        ::close(fd);
        return Status::Corrupt;
    }
    out->resize(static_cast<std::size_t>(st.st_size));
    std::size_t got = 0;
    while (got < out->size()) {
        ssize_t n = ::read(fd, &(*out)[got], out->size() - got);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            ::close(fd);
            return Status::IoError;
        }
        if (n == 0) {
            break;
        }
        got += static_cast<std::size_t>(n);
    }
    ::close(fd);
    if (got != out->size()) {
        return Status::IoError;
    }
    *exists = true;
    return Status::Ok;
}

Status write_atomic(const std::string &path, const std::string &data) {
    const std::string tmp = path + ".tmp";
    int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return Status::IoError;
    }
    std::size_t off = 0;
    while (off < data.size()) {
        ssize_t n = ::write(fd, data.data() + off, data.size() - off);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            ::close(fd);
            ::unlink(tmp.c_str());
            return Status::IoError;
        }
        off += static_cast<std::size_t>(n);
    }
    if (fsync(fd) != 0) {
        ::close(fd);
        ::unlink(tmp.c_str());
        return Status::IoError;
    }
    if (::close(fd) != 0) {
        ::unlink(tmp.c_str());
        return Status::IoError;
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return Status::IoError;
    }
    return Status::Ok;
}

#endif

}  // namespace

const char *status_str(Status s) {
    switch (s) {
        case Status::Ok:
            return "Ok";
        case Status::NotFound:
            return "NotFound";
        case Status::AlreadyExists:
            return "AlreadyExists";
        case Status::InvalidArg:
            return "InvalidArg";
        case Status::IoError:
            return "IoError";
        case Status::Corrupt:
            return "Corrupt";
        case Status::Busy:
            return "Busy";
        default:
            return "Unknown";
    }
}

Store::Store() = default;

Store::~Store() { close(); }

bool Store::is_open() const { return open_; }

void Store::unlock_file() {
#ifdef _WIN32
    if (lock_handle_) {
        HANDLE h = static_cast<HANDLE>(lock_handle_);
        OVERLAPPED ov;
        std::memset(&ov, 0, sizeof(ov));
        UnlockFileEx(h, 0, 1, 0, &ov);
        CloseHandle(h);
        lock_handle_ = nullptr;
    }
#else
    if (lock_fd_ >= 0) {
        flock(lock_fd_, LOCK_UN);
        ::close(lock_fd_);
        lock_fd_ = -1;
    }
#endif
}

Status Store::lock_file() {
#ifdef _WIN32
    std::wstring w = utf8_to_wide(path_ + ".lock");
    if (w.empty()) {
        return Status::IoError;
    }
    HANDLE h = CreateFileW(w.c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return Status::IoError;
    }
    OVERLAPPED ov;
    std::memset(&ov, 0, sizeof(ov));
    if (!LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)) {
        CloseHandle(h);
        if (GetLastError() == ERROR_LOCK_VIOLATION || GetLastError() == ERROR_IO_PENDING) {
            return Status::Busy;
        }
        return Status::IoError;
    }
    lock_handle_ = h;
    return Status::Ok;
#else
    lock_fd_ = ::open((path_ + ".lock").c_str(), O_RDWR | O_CREAT, 0644);
    if (lock_fd_ < 0) {
        return Status::IoError;
    }
    if (flock(lock_fd_, LOCK_EX | LOCK_NB) != 0) {
        int e = errno;
        ::close(lock_fd_);
        lock_fd_ = -1;
        if (e == EWOULDBLOCK) {
            return Status::Busy;
        }
        return Status::IoError;
    }
    return Status::Ok;
#endif
}

Status Store::load_or_create() {
    bool exists = false;
    std::string text;
    Status st = read_all(path_, &text, &exists);
    if (st != Status::Ok) {
        return st;
    }
    if (!exists || text.empty()) {
        rows_.clear();
        return save();
    }
    return decode_json(text, &rows_);
}

Status Store::save() {
    if (!open_) {
        return Status::IoError;
    }
    return write_atomic(path_, encode_json(rows_));
}

Status Store::open(const std::string &path) {
    if (path.empty()) {
        return Status::InvalidArg;
    }
    close();
    path_ = path;
    Status     st = lock_file();
    if (st != Status::Ok) {
        path_.clear();
        return st;
    }
    open_ = true;
    st = load_or_create();
    if (st != Status::Ok) {
        close();
        return st;
    }
    return Status::Ok;
}

void Store::close() {
    if (!open_ &&
#ifdef _WIN32
        !lock_handle_
#else
        lock_fd_ < 0
#endif
    ) {
        path_.clear();
        rows_.clear();
        return;
    }
    open_ = false;
    unlock_file();
    path_.clear();
    rows_.clear();
}

Status Store::insert(const std::string &key, const std::string &c2, const std::string &c3) {
    if (!open_) {
        return Status::IoError;
    }
    Status chk = check_fields(key, c2, c3);
    if (chk != Status::Ok) {
        return chk;
    }
    for (const auto &row : rows_) {
        if (row.first == key) {
            return Status::AlreadyExists;
        }
    }
    rows_.push_back({key, {c2, c3}});
    Status st = save();
    if (st != Status::Ok) {
        rows_.pop_back();
        return st;
    }
    return Status::Ok;
}

Status Store::update(const std::string &key, const std::string &c2, const std::string &c3) {
    if (!open_) {
        return Status::IoError;
    }
    Status chk = check_fields(key, c2, c3);
    if (chk != Status::Ok) {
        return chk;
    }
    for (auto &row : rows_) {
        if (row.first == key) {
            auto old = row.second;
            row.second = {c2, c3};
            Status st = save();
            if (st != Status::Ok) {
                row.second = old;
                return st;
            }
            return Status::Ok;
        }
    }
    return Status::NotFound;
}

Status Store::upsert(const std::string &key, const std::string &c2, const std::string &c3) {
    if (!open_) {
        return Status::IoError;
    }
    Status chk = check_fields(key, c2, c3);
    if (chk != Status::Ok) {
        return chk;
    }
    for (auto &row : rows_) {
        if (row.first == key) {
            auto old = row.second;
            row.second = {c2, c3};
            Status st = save();
            if (st != Status::Ok) {
                row.second = old;
                return st;
            }
            return Status::Ok;
        }
    }
    rows_.push_back({key, {c2, c3}});
    Status st = save();
    if (st != Status::Ok) {
        rows_.pop_back();
        return st;
    }
    return Status::Ok;
}

Status Store::remove(const std::string &key) {
    if (!open_) {
        return Status::IoError;
    }
    if (key.empty()) {
        return Status::InvalidArg;
    }
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].first == key) {
            auto erased = rows_[i];
            rows_.erase(rows_.begin() + static_cast<std::ptrdiff_t>(i));
            Status st = save();
            if (st != Status::Ok) {
                rows_.insert(rows_.begin() + static_cast<std::ptrdiff_t>(i), erased);
                return st;
            }
            return Status::Ok;
        }
    }
    return Status::NotFound;
}

Status Store::get(const std::string &key, std::string *c2, std::string *c3) const {
    if (!open_) {
        return Status::IoError;
    }
    if (key.empty()) {
        return Status::InvalidArg;
    }
    for (const auto &row : rows_) {
        if (row.first == key) {
            if (c2) {
                *c2 = row.second.first;
            }
            if (c3) {
                *c3 = row.second.second;
            }
            return Status::Ok;
        }
    }
    return Status::NotFound;
}

Status Store::exists(const std::string &key, bool *out) const {
    if (!open_) {
        return Status::IoError;
    }
    if (!out || key.empty()) {
        return Status::InvalidArg;
    }
    *out = false;
    for (const auto &row : rows_) {
        if (row.first == key) {
            *out = true;
            break;
        }
    }
    return Status::Ok;
}

Status Store::count(std::size_t *out) const {
    if (!open_) {
        return Status::IoError;
    }
    if (!out) {
        return Status::InvalidArg;
    }
    *out = rows_.size();
    return Status::Ok;
}

Status Store::list_keys(std::vector<std::string> *out) const {
    if (!open_) {
        return Status::IoError;
    }
    if (!out) {
        return Status::InvalidArg;
    }
    out->clear();
    out->reserve(rows_.size());
    for (const auto &row : rows_) {
        out->push_back(row.first);
    }
    return Status::Ok;
}

Status Store::flush() { return save(); }

}  // namespace kvtable
