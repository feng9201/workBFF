#ifndef KVTB_KVTB_H
#define KVTB_KVTB_H

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// Single-file 3-column store (key, c2, c3). Linux is the primary target;
// Windows uses the same API so the file can be inspected while debugging.
// On-disk format is UTF-8 JSON (pretty-printed) for notepad/vs-code.
// Mutating APIs fsync+replace the file; Ok means the change is on disk.

namespace kvtable {

enum class Status {
    Ok = 0,
    NotFound,       // get / update / remove: key missing
    AlreadyExists,  // insert: key already present
    InvalidArg,     // empty path/key, oversize field, null out-param
    IoError,        // open / read / write / rename / flush failed
    Corrupt,        // magic/version/JSON invalid
    Busy            // exclusive lock not available
};

const char *status_str(Status s);

class Store {
public:
    Store();
    ~Store();

    Store(const Store &) = delete;
    Store &operator=(const Store &) = delete;

    // Creates the file if it does not exist. Exclusive lock until close().
    Status open(const std::string &path);
    void close();
    bool is_open() const;

    Status insert(const std::string &key, const std::string &c2, const std::string &c3);
    Status update(const std::string &key, const std::string &c2, const std::string &c3);
    Status upsert(const std::string &key, const std::string &c2, const std::string &c3);
    Status remove(const std::string &key);

    // c2 / c3 may be null if the caller only needs one column.
    Status get(const std::string &key, std::string *c2, std::string *c3) const;
    Status exists(const std::string &key, bool *out) const;
    Status count(std::size_t *out) const;
    Status list_keys(std::vector<std::string> *out) const;

    // Rewrites the file even if nothing changed. Usually unnecessary.
    Status flush();

    const std::string &path() const { return path_; }

private:
    Status load_or_create();
    Status save();
    Status lock_file();
    void unlock_file();

    std::string path_;
    std::vector<std::pair<std::string, std::pair<std::string, std::string>>> rows_;
    bool open_ = false;

#ifdef _WIN32
    void *lock_handle_ = nullptr;  // HANDLE
#else
    int lock_fd_ = -1;
#endif
};

}  // namespace kvtable

#endif
