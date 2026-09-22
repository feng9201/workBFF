#include "db/DatabaseManager.h"

#include "crypto/AesCipher.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QUuid>
#include <QVariant>

DatabaseManager& DatabaseManager::instance()
{
    static DatabaseManager s_instance;
    return s_instance;
}

QString DatabaseManager::dbPath() const
{
    // 用户个人目录：%LOCALAPPDATA%\workbff\workbff.db
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base + QStringLiteral("/workbff/workbff.db");
}

QSqlDatabase DatabaseManager::database() const
{
    return QSqlDatabase::database(QStringLiteral("workbff_conn"));
}

bool DatabaseManager::init()
{
    // 确保目录存在
    const QString dir = QFileInfo(dbPath()).absolutePath();
    if (!QDir().mkpath(dir)) {
        qWarning() << "[db] create dir failed:" << dir;
        return false;
    }

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"),
                                                QStringLiteral("workbff_conn"));
    db.setDatabaseName(dbPath());
    if (!db.open()) {
        qWarning() << "[db] open failed:" << db.lastError().text();
        return false;
    }

    QSqlQuery q(db);
    bool ok = q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS items("
        " id INTEGER PRIMARY KEY AUTOINCREMENT,"
        " keywords TEXT NOT NULL,"
        " content TEXT NOT NULL,"
        " type TEXT NOT NULL DEFAULT 'text',"
        " priority INTEGER NOT NULL DEFAULT 1,"
        " display_priority INTEGER NOT NULL DEFAULT 1,"
        " encrypted INTEGER NOT NULL DEFAULT 0,"
        " created_at TEXT DEFAULT (datetime('now','localtime')))"));
    if (!ok) {
        qWarning() << "[db] create items failed:" << q.lastError().text();
        return false;
    }

    // 旧库迁移：补充缺失列（已存在则跳过）
    {
        QSqlQuery check(db);
        QStringList cols;
        if (check.exec(QStringLiteral("PRAGMA table_info(items)"))) {
            while (check.next())
                cols << check.value(1).toString();
        }
        const auto ensureColumn = [&](const QString& name, const QString& ddl) {
            if (!cols.contains(name)) {
                QSqlQuery alter(db);
                if (!alter.exec(QStringLiteral("ALTER TABLE items ADD COLUMN %1").arg(ddl)))
                    qWarning() << "[db] add column" << name << "failed:" << alter.lastError().text();
            }
        };
        ensureColumn(QStringLiteral("priority"),
                     QStringLiteral("priority INTEGER NOT NULL DEFAULT 1"));
        ensureColumn(QStringLiteral("display_priority"),
                     QStringLiteral("display_priority INTEGER NOT NULL DEFAULT 1"));
    }

    ok = q.exec(QStringLiteral(
        "CREATE TABLE IF NOT EXISTS meta("
        " key TEXT PRIMARY KEY,"
        " value TEXT NOT NULL)"));
    if (!ok)
        qWarning() << "[db] create meta failed:" << q.lastError().text();

    // 迁移（仅一次）：旧的 1-3 优先级数据统一改为 1
    {
        QSqlQuery qm(db);
        qm.prepare(QStringLiteral("SELECT 1 FROM meta WHERE key=:k"));
        qm.bindValue(QStringLiteral(":k"), QStringLiteral("priority_v2"));
        if (!(qm.exec() && qm.next())) {
            QSqlQuery upd(db);
            if (upd.exec(QStringLiteral("UPDATE items SET priority = 1"))) {
                qm.prepare(QStringLiteral(
                    "INSERT OR REPLACE INTO meta(key,value) VALUES(:k,:v)"));
                qm.bindValue(QStringLiteral(":k"), QStringLiteral("priority_v2"));
                qm.bindValue(QStringLiteral(":v"), QStringLiteral("1"));
                qm.exec();
            }
        }
    }
    return ok;
}

bool DatabaseManager::hasPassword() const
{
    QSqlQuery q(database());
    q.prepare(QStringLiteral("SELECT 1 FROM meta WHERE key=:k"));
    q.bindValue(QStringLiteral(":k"), QStringLiteral("password_hash"));
    return q.exec() && q.next();
}

bool DatabaseManager::setPassword(const QString& password)
{
    const QByteArray salt = QUuid::createUuid().toByteArray();
    const QByteArray hash = QCryptographicHash::hash(salt + password.toUtf8(),
                                                     QCryptographicHash::Sha256).toHex();

    QSqlQuery q(database());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO meta(key,value) VALUES(:k,:v)"));
    q.bindValue(QStringLiteral(":k"), QStringLiteral("password_salt"));
    q.bindValue(QStringLiteral(":v"), QString::fromLatin1(salt));
    if (!q.exec())
        return false;

    q.prepare(QStringLiteral("INSERT OR REPLACE INTO meta(key,value) VALUES(:k,:v)"));
    q.bindValue(QStringLiteral(":k"), QStringLiteral("password_hash"));
    q.bindValue(QStringLiteral(":v"), QString::fromLatin1(hash));
    return q.exec();
}

bool DatabaseManager::verifyPassword(const QString& password) const
{
    QSqlQuery q(database());
    q.prepare(QStringLiteral("SELECT value FROM meta WHERE key=:k"));
    q.bindValue(QStringLiteral(":k"), QStringLiteral("password_salt"));
    if (!q.exec() || !q.next())
        return false;
    const QByteArray salt = q.value(0).toString().toLatin1();

    q.prepare(QStringLiteral("SELECT value FROM meta WHERE key=:k"));
    q.bindValue(QStringLiteral(":k"), QStringLiteral("password_hash"));
    if (!q.exec() || !q.next())
        return false;
    const QByteArray stored = q.value(0).toString().toLatin1();

    const QByteArray computed = QCryptographicHash::hash(salt + password.toUtf8(),
                                                         QCryptographicHash::Sha256).toHex();
    return stored == computed;
}

bool DatabaseManager::addItem(const wb::Item& item, const QByteArray& aesKey)
{
    QString content = item.content;
    if (item.encrypted && !aesKey.isEmpty()) {
        const QByteArray cipher = AesCipher::encrypt(item.content.toUtf8(), aesKey);
        if (cipher.isEmpty()) {
            qWarning() << "[db] encrypt content failed";
            return false;
        }
        content = QString::fromLatin1(cipher.toBase64());
    }

    QSqlQuery q(database());
    q.prepare(QStringLiteral(
        "INSERT INTO items(keywords,content,type,priority,display_priority,encrypted) "
        "VALUES(:kw,:c,:t,:p,:dp,:e)"));
    q.bindValue(QStringLiteral(":kw"), item.keywords);
    q.bindValue(QStringLiteral(":c"), content);
    q.bindValue(QStringLiteral(":t"), wb::itemTypeToString(item.type));
    q.bindValue(QStringLiteral(":p"), item.priority);
    q.bindValue(QStringLiteral(":dp"), item.displayPriority);
    q.bindValue(QStringLiteral(":e"), item.encrypted ? 1 : 0);
    return q.exec();
}

QVector<wb::Item> DatabaseManager::queryItems(const QString& filter, const QByteArray& aesKey)
{
    QVector<wb::Item> result;
    QSqlQuery q(database());

    // 按空格 / 逗号（中英文）/ 分号 分隔多个关键词，逐词匹配，需全部命中
    const QStringList kws = filter.split(
        QRegularExpression(QStringLiteral("[\\s,，;；]+")), Qt::SkipEmptyParts);

    QString sql = QStringLiteral(
        "SELECT id,keywords,content,type,encrypted,priority,display_priority FROM items");
    if (kws.isEmpty()) {
        // 搜索框为空：只显示第 1 级，级内按显示优先级 1→5 排序（同级最新在前）
        sql += QStringLiteral(" WHERE priority = 1"
                              " ORDER BY display_priority ASC, id DESC");
    } else {
        // 搜索框有内容：全部级别，先按级排、级内再按显示优先级排
        QStringList conds;
        for (int i = 0; i < kws.size(); ++i) {
            conds << QStringLiteral(
                "(keywords LIKE :kw%1 OR (encrypted=0 AND content LIKE :kw%1))").arg(i);
        }
        sql += QStringLiteral(" WHERE ") + conds.join(QStringLiteral(" AND "))
               + QStringLiteral(" ORDER BY priority ASC, display_priority ASC, id DESC");
    }

    q.prepare(sql);
    for (int i = 0; i < kws.size(); ++i)
        q.bindValue(QStringLiteral(":kw%1").arg(i), QStringLiteral("%%1%").arg(kws.at(i)));

    if (!q.exec()) {
        qWarning() << "[db] query failed:" << q.lastError().text();
        return result;
    }

    while (q.next()) {
        wb::Item item;
        item.id = q.value(0).toLongLong();
        item.keywords = q.value(1).toString();
        item.type = wb::stringToItemType(q.value(3).toString());
        item.encrypted = q.value(4).toInt() != 0;
        item.priority = q.value(5).toInt();
        item.displayPriority = q.value(6).toInt();
        if (item.encrypted) {
            if (aesKey.isEmpty())
                continue; // 未登录：隐藏加密条目
            const QByteArray cipher = QByteArray::fromBase64(q.value(2).toString().toLatin1());
            const QByteArray plain = AesCipher::decrypt(cipher, aesKey);
            if (plain.isEmpty())
                continue; // 解密失败（密钥不符），跳过
            item.content = QString::fromUtf8(plain);
        } else {
            item.content = q.value(2).toString();
        }
        result.append(item);
    }
    return result;
}

bool DatabaseManager::removeItem(qint64 id)
{
    QSqlQuery q(database());
    q.prepare(QStringLiteral("DELETE FROM items WHERE id=:id"));
    q.bindValue(QStringLiteral(":id"), id);
    return q.exec();
}
