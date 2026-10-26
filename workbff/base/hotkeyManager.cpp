#include "base/hotkeyManager.h"

#include <QAbstractEventDispatcher>
#include <QKeyEvent>
#include <QLatin1String>
#include <QStringList>
#include <QDebug>
#include <Windows.h>

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000 // Win7+：按住不放不连续触发
#endif

namespace {

struct ModifierInfo {
    const char* name;         // 规范化串里的写法
    Qt::Modifier qtModifier;  // 录制时从 QKeyEvent 判定（Qt5 里单修饰键属于 Qt::Modifier）
    unsigned nativeMod;       // Win32 MOD_*
    const char* display;      // 界面显示
};

// 顺序即规范化串的书写顺序，保证同一个组合永远得到同一个字符串
constexpr ModifierInfo kModifiers[] = {
    { "CTRL",  Qt::CTRL,  0x0002, "Ctrl"  },
    { "ALT",   Qt::ALT,   0x0001, "Alt"   },
    { "SHIFT", Qt::SHIFT, 0x0004, "Shift" },
    { "WIN",   Qt::META,  0x0008, "Win"   },
};

// 主键名 → VK 虚拟键码，0 表示不支持
uint vkFromName(const QString& name)
{
    if (name.size() == 1) {
        const QChar ch = name.at(0).toUpper();
        if ((ch >= QLatin1Char('A') && ch <= QLatin1Char('Z'))
            || (ch >= QLatin1Char('0') && ch <= QLatin1Char('9')))
            return ch.unicode(); // 字母/数字的 VK 就是其 ASCII 大写值
    }
    if (name.size() >= 2 && (name.at(0) == QLatin1Char('F') || name.at(0) == QLatin1Char('f'))) {
        bool ok = false;
        const int n = name.mid(1).toInt(&ok);
        if (ok && n >= 1 && n <= 24)
            return VK_F1 + static_cast<uint>(n - 1);
    }
    if (name.compare(QLatin1String("SPACE"), Qt::CaseInsensitive) == 0) return VK_SPACE;
    if (name.compare(QLatin1String("ENTER"), Qt::CaseInsensitive) == 0) return VK_RETURN;
    if (name.compare(QLatin1String("TAB"), Qt::CaseInsensitive) == 0)   return VK_TAB;
    if (name.compare(QLatin1String("ESC"), Qt::CaseInsensitive) == 0)   return VK_ESCAPE;
    return 0;
}

// QKeyEvent 的 key → 主键名，空串表示不支持
QString nameFromQtKey(int qtKey)
{
    // Qt 的字母/数字键值与 ASCII 同值
    if ((qtKey >= Qt::Key_A && qtKey <= Qt::Key_Z)
        || (qtKey >= Qt::Key_0 && qtKey <= Qt::Key_9))
        return QString(QChar(static_cast<uint>(qtKey))).toUpper();
    if (qtKey >= Qt::Key_F1 && qtKey <= Qt::Key_F24)
        return QStringLiteral("F%1").arg(qtKey - Qt::Key_F1 + 1);
    switch (qtKey) {
    case Qt::Key_Space:  return QStringLiteral("SPACE");
    case Qt::Key_Return:
    case Qt::Key_Enter:  return QStringLiteral("ENTER");
    case Qt::Key_Tab:    return QStringLiteral("TAB");
    case Qt::Key_Escape: return QStringLiteral("ESC");
    default:             return QString();
    }
}

// 只按了修饰键本身：此时组合还没完成，不该报错也不该记账
bool isModifierOnlyKey(int qtKey)
{
    switch (qtKey) {
    case Qt::Key_Control:
    case Qt::Key_Shift:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_Meta:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
    case Qt::Key_Menu:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_ScrollLock:
        return true;
    default:
        return false;
    }
}

HWND msgHwnd(const QWindow& win)
{
    return reinterpret_cast<HWND>(static_cast<uintptr_t>(win.winId()));
}

} // namespace

HotkeyManager& HotkeyManager::instance()
{
    static HotkeyManager s_instance;
    return s_instance;
}

HotkeyManager::HotkeyManager(QObject* parent)
    : QObject(parent)
{
    // 这个窗口只是 WM_HOTKEY 的落脚点，不 setFlags/show，界面上不会出现任何东西
    m_msgWindow.setTitle(QStringLiteral("workBFF_HotkeySink"));
}

HotkeyManager::~HotkeyManager()
{
    unregisterAll();
    if (m_filterInstalled) {
        if (QAbstractEventDispatcher* d = QAbstractEventDispatcher::instance())
            d->removeNativeEventFilter(this);
    }
}

bool HotkeyManager::ensureReady()
{
    if (!m_filterInstalled) {
        QAbstractEventDispatcher* d = QAbstractEventDispatcher::instance();
        if (!d)
            return false;
        d->installNativeEventFilter(this);
        m_filterInstalled = true;
    }
    if (!m_windowCreated) {
        m_msgWindow.create(); // 只建平台窗口，不显示
        m_windowCreated = m_msgWindow.winId() != 0;
    }
    return m_windowCreated;
}

bool HotkeyManager::toNative(const QString& shortcut, unsigned* mods, unsigned* vk)
{
    const QStringList parts = shortcut.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (parts.size() < 2) // 至少要"一个修饰键 + 一个主键"
        return false;

    unsigned m = 0;
    int i = 0;
    for (; i < parts.size(); ++i) {
        const QString& token = parts.at(i);
        unsigned nativeMod = 0;
        for (const auto& mod : kModifiers) {
            if (token.compare(QLatin1String(mod.name), Qt::CaseInsensitive) == 0) {
                nativeMod = mod.nativeMod;
                break;
            }
        }
        if (nativeMod == 0)
            break; // 不是修饰键，就是主键
        m |= nativeMod;
    }
    // 主键必须是最后一个 token（前面全是修饰键），且不能重复
    if (i != parts.size() - 1)
        return false;

    const uint code = vkFromName(parts.at(i));
    if (code == 0)
        return false;

    if (mods)
        *mods = m | MOD_NOREPEAT;
    if (vk)
        *vk = code;
    return true;
}

QString HotkeyManager::fromKeyEvent(const QKeyEvent* ev, QString* errMsg)
{
    if (errMsg)
        errMsg->clear();
    if (!ev)
        return QString();

    const int key = ev->key();
    if (isModifierOnlyKey(key))
        return QString(); // 还没按完，忽略

    const Qt::KeyboardModifiers pressed =
        ev->modifiers() & (Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::META);
    if (pressed == Qt::NoModifier) {
        if (errMsg)
            *errMsg = QStringLiteral("快捷键要搭配 Ctrl / Alt / Shift / Win 中的至少一个");
        return QString();
    }

    const QString keyName = nameFromQtKey(key);
    if (keyName.isEmpty()) {
        if (errMsg)
            *errMsg = QStringLiteral("不支持的按键，请用 字母 / 数字 / F1-F24 / 空格 / 回车 / Tab / Esc");
        return QString();
    }

    QStringList parts;
    for (const auto& mod : kModifiers) {
        if (pressed & mod.qtModifier)
            parts << QLatin1String(mod.name);
    }
    parts << keyName;
    return parts.join(QLatin1Char('+'));
}

QString HotkeyManager::toDisplayText(const QString& shortcut)
{
    const QStringList parts = shortcut.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    QStringList out;
    out.reserve(parts.size());
    for (const QString& token : parts) {
        bool isModifier = false;
        for (const auto& mod : kModifiers) {
            if (token.compare(QLatin1String(mod.name), Qt::CaseInsensitive) == 0) {
                out << QLatin1String(mod.display);
                isModifier = true;
                break;
            }
        }
        if (isModifier || token.isEmpty())
            continue;
        if (token.size() == 1) {
            out << token.toUpper(); // W / 1
            continue;
        }
        out << token.left(1).toUpper() + token.mid(1).toLower(); // F5→F5 SPACE→Space ESC→Esc
    }
    return out.join(QLatin1Char('+'));
}

bool HotkeyManager::registerHotkey(qint64 itemId, const QString& shortcut, QString* errMsg)
{
    if (errMsg)
        errMsg->clear();

    unsigned mods = 0;
    unsigned vk = 0;
    if (!toNative(shortcut, &mods, &vk)) {
        if (errMsg)
            *errMsg = QStringLiteral("快捷键格式不合法: %1").arg(shortcut);
        return false;
    }

    // 同一改绑：先释放旧注册，避免热键 id 泄漏
    unregisterHotkey(itemId);

    if (m_itemToId.size() >= kMaxHotkeys) {
        if (errMsg)
            *errMsg = QStringLiteral("最多只能设置 %1 个快捷键，请先在「快捷键管理」里删除一个")
                          .arg(kMaxHotkeys);
        return false;
    }

    if (!ensureReady()) {
        if (errMsg)
            *errMsg = QStringLiteral("快捷键模块初始化失败");
        return false;
    }

    // 取一个没在用的热键 id（RegisterHotKey 要求 0x0000~0xBFFF）
    int id = 0;
    for (int cand = 1; cand <= 0xBFFF; ++cand) {
        if (!m_idToItem.contains(cand)) {
            id = cand;
            break;
        }
    }
    if (id == 0) {
        if (errMsg)
            *errMsg = QStringLiteral("热键 id 已用尽，重启程序可恢复");
        return false;
    }

    if (!::RegisterHotKey(msgHwnd(m_msgWindow), id, mods, vk)) {
        const DWORD code = ::GetLastError();
        if (errMsg) {
            *errMsg = (code == ERROR_HOTKEY_ALREADY_REGISTERED)
                ? QStringLiteral("%1 已被其他程序占用，换一个").arg(toDisplayText(shortcut))
                : QStringLiteral("注册 %1 失败（Windows 错误码 %2）").arg(toDisplayText(shortcut)).arg(code);
        }
        return false;
    }

    m_itemToId.insert(itemId, id);
    m_idToItem.insert(id, itemId);
    qInfo() << "[hotkey] registered" << shortcut << "itemId=" << itemId << "id=" << id;
    return true;
}

void HotkeyManager::unregisterHotkey(qint64 itemId)
{
    const int id = m_itemToId.value(itemId, 0);
    if (id == 0) {
        qInfo() << "[hotkey] unregister itemId=" << itemId << "本来就没注册（可能是启动时就没注册上）";
        return;
    }
    m_itemToId.remove(itemId);
    m_idToItem.remove(id);
    if (m_windowCreated)
        ::UnregisterHotKey(msgHwnd(m_msgWindow), id);
    qInfo() << "[hotkey] unregistered itemId=" << itemId << "id=" << id;
}

void HotkeyManager::unregisterAll()
{
    if (m_windowCreated && !m_idToItem.isEmpty()) {
        HWND hwnd = msgHwnd(m_msgWindow);
        for (int id : m_idToItem.keys())
            ::UnregisterHotKey(hwnd, id);
    }
    qInfo() << "[hotkey] unregisterAll, 释放了" << m_idToItem.size() << "个";
    m_idToItem.clear();
    m_itemToId.clear();
}

bool HotkeyManager::nativeEventFilter(const QByteArray& eventType, void* message, long* result)
{
    Q_UNUSED(result);
    if (eventType != "windows_generic_MSG")
        return false;

    auto* msg = static_cast<MSG*>(message);
    if (msg->message != WM_HOTKEY)
        return false;

    const auto it = m_idToItem.constFind(static_cast<int>(msg->wParam));
    if (it == m_idToItem.constEnd())
        return false;

    qInfo() << "[hotkey] hit, itemId=" << it.value();
    emit triggered(it.value());
    return true; // 已消费，不再交给 Qt 窗口过程
}
