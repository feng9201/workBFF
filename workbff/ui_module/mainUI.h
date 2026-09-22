#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QPoint>
#include <QWidget>

#include "model/Item.h"

class AddPanel;
class QLabel;
class QLineEdit;
class QMenu;
class QMouseEvent;
class QPaintEvent;
class QStackedWidget;
class QTimer;
class QVBoxLayout;
class TitleBar;

// 480×480 无边框主界面（Qt::Tool，不进任务栏）
// 两个页面：搜索页（标题栏+搜索框+可变行高列表）/ 添加页
class MainUI : public QWidget
{
    Q_OBJECT

public:
    explicit MainUI(QWidget* parent = nullptr);
    ~MainUI() override;

    void showCentered();
    // 取消待执行的“点击外部自动隐藏”（dock/托盘点击时调用）
    void cancelPendingHide();
    // 是否存在待执行的自动隐藏（刚点击过外部、窗口尚未隐藏）
    bool hasPendingHide() const;

protected:
    // 绘制纯色圆角背景（保证不透明）
    void paintEvent(QPaintEvent* e) override;
    // 点击窗口外部时自动隐藏
    void changeEvent(QEvent* e) override;
    // 窗口任意空白处拖拽移动（交互控件会自行消费事件，不受影响）
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private slots:
    void refreshList();
    void onCopyItem(const wb::Item& item);
    void onOpenItem(const wb::Item& item);
    void onDeleteItem(const wb::Item& item);
    void onMenuLogin();
    void onMenuAdd();
    void onMenuHelp();
    void onMenuVersion();
    void showToast(const QString& msg);

private:
    void buildTitleBar();
    void buildMenu();
    void buildSearchPage();
    void buildAddPage();
    void hideWindow();

    TitleBar* m_titleBar = nullptr;
    QMenu* m_menu = nullptr;
    QLineEdit* m_searchEdit = nullptr;
    QStackedWidget* m_stack = nullptr;
    QWidget* m_listContainer = nullptr;
    QVBoxLayout* m_listLayout = nullptr;
    QLabel* m_emptyLabel = nullptr;
    AddPanel* m_addPanel = nullptr;
    QLabel* m_toast = nullptr;
    QTimer* m_toastTimer = nullptr;

    bool m_dragging = false;
    QPoint m_dragOffset;

    QTimer* m_hideTimer = nullptr;   // 点击外部后的延迟隐藏
    QElapsedTimer m_visibleSince;    // 本次显示时刻（显示后宽限期内不自动隐藏）
    bool m_autoHideEnabled = true;   // 菜单/弹窗打开时挂起自动隐藏

    bool m_loggedIn = false;
    QByteArray m_aesKey;
};


