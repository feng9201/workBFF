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
    bool addItem(const wb::Item& item, const QByteArray& aesKey);
    // filter 匹配关键词；加密条目的 content 为密文，故只能按关键词命中。
    // aesKey 为空时隐藏加密条目；有 key 时解密返回明文内容。
    QVector<wb::Item> queryItems(const QString& filter, const QByteArray& aesKey);
    bool removeItem(qint64 id);

private:
    DatabaseManager() = default;
    QString dbPath() const;
    QSqlDatabase database() const;
};
