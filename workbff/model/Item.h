#pragma once

#include <QString>

namespace wb {

// 条目类型
enum class ItemType { Text, Folder, Exe, Http, Scheme };

// 一条收藏/快捷项
struct Item {
    qint64 id = -1;          // 数据库自增 id
    QString keywords;        // 逗号分隔的搜索关键词（最多3个）
    QString content;         // 内容：文本 / 文件夹路径 / exe 路径 / http 地址 / scheme 协议
    ItemType type = ItemType::Text;
    int priority = 1;        // 级（旧优先级）1/2/3，默认 1：搜索框为空时只显示第1级
    int displayPriority = 1; // 显示优先级 1~5，默认 1：同级内排序用
    bool encrypted = false;  // 是否加密（登录后才能看到）
};

inline QString itemTypeToString(ItemType t)
{
    switch (t) {
    case ItemType::Folder: return QStringLiteral("folder");
    case ItemType::Exe:    return QStringLiteral("exe");
    case ItemType::Http:   return QStringLiteral("http");
    case ItemType::Scheme: return QStringLiteral("scheme");
    case ItemType::Text:
    default:               return QStringLiteral("text");
    }
}

inline ItemType stringToItemType(const QString& s)
{
    if (s == QLatin1String("folder")) return ItemType::Folder;
    if (s == QLatin1String("exe"))    return ItemType::Exe;
    if (s == QLatin1String("http"))   return ItemType::Http;
    if (s == QLatin1String("scheme")) return ItemType::Scheme;
    return ItemType::Text;
}

}
