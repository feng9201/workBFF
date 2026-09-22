#include "kvtable.h"

#include <cstdio>
#include <string>
#include <vector>

#ifndef KVTB_DEMO_FILE
#define KVTB_DEMO_FILE "kvtable_demo.kvt"
#endif

using kvtable::Status;
using kvtable::Store;
using kvtable::status_str;

static void show(const char *step, Status st) {
    std::printf("[%-28s] %s\n", step, status_str(st));
}

static std::string blob(char tag, std::size_t n) {
    std::string s;
    s.resize(n, tag);
    s.replace(0, 8, "BEGIN___");
    if (n >= 8) {
        s.replace(n - 8, 8, "___END");
    }
    return s;
}

int main() {
    const std::string path = KVTB_DEMO_FILE;
    std::printf("demo file: %s\n\n", path.c_str());

    Store db;
    Status st = db.open(path);
    show("open", st);
    if (st != Status::Ok) {
        return 1;
    }

    const std::string c2 = blob('A', 520);
    const std::string c3 = blob('B', 520);
    std::printf("row payload: c2=%zu bytes, c3=%zu bytes\n\n", c2.size(), c3.size());

    for (int i = 1; i <= 12; ++i) {
        const std::string key = "item-" + std::to_string(i);
        show(("insert " + key).c_str(), db.insert(key, c2, c3));
    }
    show("insert item-1 again", db.insert("item-1", "x", "y"));
    show("insert empty key", db.insert("", "x", "y"));

    std::string out2, out3;
    st = db.get("item-3", &out2, &out3);
    show("get item-3", st);
    if (st == Status::Ok) {
        std::printf("  c2 starts with \"%.8s\" len=%zu, c3 starts with \"%.8s\" len=%zu\n",
                    out2.c_str(), out2.size(), out3.c_str(), out3.size());
    }

    show("update item-3", db.update("item-3", "short-c2", "short-c3"));
    show("update missing", db.update("no-such", "a", "b"));
    show("remove item-12", db.remove("item-12"));
    show("remove item-12 again", db.remove("item-12"));

    std::size_t n = 0;
    show("count", db.count(&n));
    std::printf("  rows=%zu\n", n);

    std::vector<std::string> keys;
    show("list_keys", db.list_keys(&keys));
    std::printf("  keys:");
    for (const auto &k : keys) {
        std::printf(" %s", k.c_str());
    }
    std::printf("\n");

    db.close();

    Store again;
    show("reopen", again.open(path));
    show("persist get item-3", again.get("item-3", &out2, &out3));
    if (out2 == "short-c2" && out3 == "short-c3") {
        std::printf("  persisted update: ok\n");
    } else {
        std::printf("  persisted update: FAIL\n");
        return 1;
    }
    again.close();

    std::printf("\nopen the JSON file above in an editor to inspect rows.\n");
    return 0;
}
