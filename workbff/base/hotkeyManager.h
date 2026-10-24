#pragma once

#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QObject>
#include <QWindow>

class QKeyEvent;

// 全局快捷键：Win32 RegisterHotKey 注册 + WM_HOTKEY 监听，命中后发 triggered(itemId)。
// 注册与收消息都在主线程：用一个从不显示的 QWindow 取其 HWND 作为消息目标，
// 这样主界面隐藏（本程序常驻 Dock）时快捷键照样生效。
// 快捷键串统一为规范化大写格式："CTRL+ALT+W"（修饰键按 CTRL/ALT/SHIFT/WIN 固定顺序），
// 界面显示时用 toDisplayText() 转成 "Ctrl+Alt+W"。
class HotkeyManager : public QObject, public QAbstractNativeEventFilter
{
    Q_OBJECT
public:
    // 产品上限：整个程序同时最多注册 5 个热键
    static constexpr int kMaxHotkeys = 5;

    static HotkeyManager& instance();

    // 注册（同一 itemId 重复注册会先解绑再注册）。失败时 errMsg 给出可直接展示的中文原因
    bool registerHotkey(qint64 itemId, const QString& shortcut, QString* errMsg = nullptr);
    void unregisterHotkey(qint64 itemId);
    void unregisterAll();
    int count() const { return m_itemToId.size(); }

    // 录制用：由按键事件生成规范化串。
    // 返回空且 errMsg 为空 → 只按下了修饰键，还没按完，调用方直接忽略；
    // 返回空且 errMsg 非空 → 组合不合法（缺修饰键 / 主键不支持）。
    static QString fromKeyEvent(const QKeyEvent* ev, QString* errMsg = nullptr);
    // "CTRL+SHIFT+W" → "Ctrl+Shift+W"
    static QString toDisplayText(const QString& shortcut);

signals:
    void triggered(qint64 itemId);

protected:
    // Qt5 的原生事件过滤签名用的就是 long*（Qt6 才改成 qintptr*）
    bool nativeEventFilter(const QByteArray& eventType, void* message, long* result) override;

private:
    explicit HotkeyManager(QObject* parent = nullptr);
    ~HotkeyManager() override;
    Q_DISABLE_COPY(HotkeyManager)

    // 建消息窗口 + 装原生事件过滤器（幂等，首次注册时调用）
    bool ensureReady();
    // 规范化串 → Win32 MOD_* 标志与 VK 虚拟键码
    static bool toNative(const QString& shortcut, unsigned* mods, unsigned* vk);

    QWindow m_msgWindow;              // 只为拿 HWND，永远不 show
    QHash<qint64, int> m_itemToId;    // 条目 id → 热键 id（1~0xBFFF）
    QHash<int, qint64> m_idToItem;    // 热键 id → 条目 id
    bool m_windowCreated = false;     // Qt5 的 QWindow 没有 isCreated()，自己记
    bool m_filterInstalled = false;
};
