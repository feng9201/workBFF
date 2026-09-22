#include "kvtable.h"

#include <cstdio>
#include <cstdlib>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

using kvtable::Status;
using kvtable::Store;
using kvtable::status_str;

static int g_failed = 0;

static void expect(const char *name, Status got, Status want) {
    if (got != want) {
        std::fprintf(stderr, "FAIL %s: got %s want %s\n", name, status_str(got), status_str(want));
        ++g_failed;
    } else {
        std::printf("OK   %s (%s)\n", name, status_str(got));
    }
}

static std::string test_dir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD n = GetTempPathA(MAX_PATH, buf);
    if (n == 0 || n >= MAX_PATH) {
        return std::string(".\\");
    }
    return std::string(buf);
#else
    return std::string("/tmp/");
#endif
}

static void remove_path(const std::string &p) {
#ifdef _WIN32
    DeleteFileA(p.c_str());
#else
    unlink(p.c_str());
#endif
}

int main() {
    const std::string path = test_dir() + "kvtable_demo.kvt";
    remove_path(path);
    remove_path(path + ".lock");
    remove_path(path + ".tmp");

    Store db;
    expect("open empty", db.open(path), Status::Ok);

    expect("insert k1", db.insert("k1", "hello", "world"), Status::Ok);
    expect("insert dup", db.insert("k1", "x", "y"), Status::AlreadyExists);
    expect("insert empty key", db.insert("", "a", "b"), Status::InvalidArg);

    std::string c2, c3;
    expect("get k1", db.get("k1", &c2, &c3), Status::Ok);
    if (c2 != "hello" || c3 != "world") {
        std::fprintf(stderr, "FAIL get payload %s / %s\n", c2.c_str(), c3.c_str());
        ++g_failed;
    }

    expect("update k1", db.update("k1", "line1\nline2", "中文&\"quote\""), Status::Ok);
    expect("update missing", db.update("nope", "a", "b"), Status::NotFound);
    expect("get missing", db.get("nope", &c2, &c3), Status::NotFound);

    expect("upsert new", db.upsert("k2", "col2", "col3"), Status::Ok);
    expect("upsert old", db.upsert("k2", "col2b", "col3b"), Status::Ok);

    std::size_t n = 0;
    expect("count", db.count(&n), Status::Ok);
    if (n != 2) {
        std::fprintf(stderr, "FAIL count=%zu\n", n);
        ++g_failed;
    }

    expect("remove k2", db.remove("k2"), Status::Ok);
    expect("remove missing", db.remove("k2"), Status::NotFound);

    db.close();

    Store again;
    expect("reopen", again.open(path), Status::Ok);
    expect("persist get", again.get("k1", &c2, &c3), Status::Ok);
    if (c2 != "line1\nline2" || c3 != "中文&\"quote\"") {
        std::fprintf(stderr, "FAIL persist payload\n");
        ++g_failed;
    }
    bool has = false;
    expect("exists k1", again.exists("k1", &has), Status::Ok);
    if (!has) {
        std::fprintf(stderr, "FAIL exists\n");
        ++g_failed;
    }

    Store busy;
    expect("second open busy", busy.open(path), Status::Busy);
    again.close();

    // Corrupt file should be reported, not treated as empty.
    {
#ifdef _WIN32
        FILE *fp = nullptr;
        fopen_s(&fp, path.c_str(), "wb");
#else
        FILE *fp = std::fopen(path.c_str(), "wb");
#endif
        if (fp) {
            std::fputs("{not json", fp);
            std::fclose(fp);
        }
        Store bad;
        expect("open corrupt", bad.open(path), Status::Corrupt);
    }

    again.close();
    remove_path(path);
    remove_path(path + ".lock");
    remove_path(path + ".tmp");

    if (g_failed) {
        std::fprintf(stderr, "%d test(s) failed\n", g_failed);
        return 1;
    }
    std::printf("all tests passed\n");
    return 0;
}
