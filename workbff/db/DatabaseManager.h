#pragma once

#include <QObject>
#include <QSqlDatabase>
#include <QVector>

#include "model/Item.h"

// 本地 SQLite 数据管理（Qt SQL 模块，QSQLITE 驱动）
// 单例，数据库文件位于 exe 同目录 workbff.db
class DatabaseManager : public QObject
{
    Q_OBJECT
public:
    static DatabaseManager& instance();

    // 初始化：打开数据库并建表
    bool init();

    // ── 密码（首次设置 + 登录校验，密码以加盐 SHA-256 存储）──
    bool hasPassword() const;
    bool setPassword(const QString& password);
    bool verifyPassword(const QString& password) const;

    // ── 增查删 ──
    // item.encrypted 时用 aesKey 加密 content 后入库；aesKey 为空则按明文存
    // newId 非空时回写新行的 id（绑定快捷键时需要用 id 注册热键）
    bool addItem(const wb::Item& item, const QByteArray& aesKey, qint64* newId = nullptr);
    // filter 匹配关键词；加密条目的 content 为密文，故只能按关键词命中。
    // aesKey 为空时隐藏加密条目；有 key 时解密返回明文内容。
    QVector<wb::Item> queryItems(const QString& filter, const QByteArray& aesKey);
    bool removeItem(qint64 id);

    // ── 快捷键 ──
    // 所有已绑定快捷键的条目（按定义：均为非加密条目，内容为明文）
    QVector<wb::Item> queryHotkeyItems();
    // 按 id 取单条；加密条目或未命中时返回 false
    bool itemById(qint64 id, wb::Item* out);
    // 该快捷键是否已被某条条目占用（excludeId 用于“改绑自身”场景）
    bool shortcutExists(const QString& shortcut, qint64 excludeId = -1);
    // 设置/清空某条目的快捷键（传空串即解绑）
    bool setItemShortcut(qint64 id, const QString& shortcut);

private:
    DatabaseManager() = default;
    QString dbPath() const;
    QSqlDatabase database() const;
};
