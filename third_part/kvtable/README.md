# kvtable

单文件三列表（`key` / `c2` / `c3`），给嵌入式 Linux 存约 100KB 配置用，不引入 SQLite、不依赖 Qt。Windows 用同一套 API，方便本机调试。磁盘是 UTF-8 JSON，可用记事本打开。

增删改成功返回 `Ok` 时，数据已经落盘。写盘失败会回滚内存，避免和文件不一致。

## 文件格式

`demo_out/app.kvt` 示例：

```json
{
  "magic": "kvtable",
  "version": 1,
  "rows": [
    {
      "key": "item-1",
      "c2": "...",
      "c3": "..."
    }
  ]
}
```

同目录会有 `app.kvt.lock`（占用期间排他锁）。崩溃安全：先写 `*.tmp`，`fsync` / `FlushFileBuffers` 后再原子替换（Linux `rename`，Windows `MoveFileEx`）。

## 限制

| 项 | 上限 | 超限返回值 |
|----|------|------------|
| `key` | 4096 字节，且不能为空 | `InvalidArg` |
| `c2` / `c3` 各列 | 256 KB | `InvalidArg` |
| 整个文件 | 32 MB | 打开时 `Corrupt` |

单条 500 字节、总量 100KB 都在限制内。

## 返回值

| Status | 何时出现 |
|--------|----------|
| `Ok` | 成功，且变更已写盘 |
| `NotFound` | `get` / `update` / `remove` 时 key 不存在 |
| `AlreadyExists` | `insert` 时 key 已存在（不会覆盖，覆盖用 `update` 或 `upsert`） |
| `InvalidArg` | 空路径、空 key、字段超长、输出指针为空 |
| `IoError` | 未 `open`、打开/读写/替换失败 |
| `Corrupt` | magic / version / JSON 非法 |
| `Busy` | 文件已被另一个 `Store` 打开（`.lock`） |

`status_str(st)` 可转成以上英文名。

## 构建

C++14，无第三方依赖。静态库 `kvtable`。

Windows（VS 2022）：

```bat
cd kvtable
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Linux：

```bash
cd kvtable
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

交叉编译时关掉测试和 demo：

```bash
cmake -B build -DKVTB_BUILD_TESTS=OFF -DKVTB_BUILD_DEMO=OFF
```

## 跑测试 / demo

```bat
.\build\Release\kvtable_test.exe
.\build\Release\kvtable_demo.exe
```

Linux 对应 `build/kvtable_test`、`build/kvtable_demo`。

- `kvtable_test`：空库、重复插入、缺 key、换行/中文/引号、重开持久化、Busy、Corrupt。跑完会删临时文件。
- `kvtable_demo`：写入 12 条、每列约 520 字节，演示 insert/get/update/remove，JSON 留在 `demo_out/app.kvt`，可直接打开查看。

## 用法

```cpp
#include "kvtable.h"

kvtable::Store db;
kvtable::Status st = db.open("/data/app.kvt");
if (st != kvtable::Status::Ok) {
    // IoError / Corrupt / Busy
    return;
}

st = db.insert("id1", "payload-a", "payload-b");
if (st == kvtable::Status::AlreadyExists) {
    db.update("id1", "payload-a", "payload-b");
}

std::string c2, c3;
st = db.get("id1", &c2, &c3);   // NotFound 或 Ok

db.upsert("id2", "x", "y");     // 有则改，无则增
db.remove("id2");               // 没有则 NotFound
db.close();
```

CMake 接入：

```cmake
add_subdirectory(kvtable)
target_link_libraries(your_app PRIVATE kvtable)
```

`open()` 期间独占该文件，析构会 `close()` 并释放锁。不要多进程同时写同一个 `.kvt`。
