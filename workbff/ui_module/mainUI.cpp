#include "ui_module/mainUI.h"

#include "Version.h"
#include "base/hotkeyManager.h"
#include "crypto/AesCipher.h"
#include "db/DatabaseManager.h"
#include "ui_module/AddPanel.h"
#include "ui_module/ListItemWidget.h"
#include "ui_module/PasswordDialog.h"
#include "ui_module/TitleBar.h"

#include <QApplication>
#include <QAction>
#include <QClipboard>
#include <QCursor>
#include <QDebug>
#include <QDesktopServices>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// 手绘“✕”图标：暗色主题下系统样式图标常常缺失或灰到看不清，这里直接画一个
QIcon makeCloseIcon()
{
    constexpr int kSize = 16;
    QPixmap pm(kSize, kSize);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(0xb0, 0xb0, 0xc8), 1.6, Qt::SolidLine, Qt::RoundCap));
    const double m = 4.5;
    p.drawLine(QPointF(m, m), QPointF(kSize - m, kSize - m));
    p.drawLine(QPointF(kSize - m, m), QPointF(m, kSize - m));
    p.end();

    QIcon icon;
    icon.addPixmap(pm);
    return icon;
}

} // namespace

MainUI::MainUI(QWidget* parent)
    : QWidget(parent)
{
    // 无边框 Tool 窗口（不进任务栏）；点击外部自动隐藏由 changeEvent + 定时器实现
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(480, 480);
    setObjectName(QStringLiteral("mainUI"));
    // 背景由 paintEvent 绘制（纯色，避免透明），此处只样式化子控件
    setStyleSheet(QStringLiteral(
        "QLineEdit { background: rgba(255,255,255,0.06);"
        " border: 1px solid rgba(255,255,255,0.1); border-radius: 8px;"
        " padding: 0 12px; color: #e8e8f0; }"
        "QLineEdit:focus { border-color: #6c5ce7; }"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    buildTitleBar();
    root->addWidget(m_titleBar);

    m_stack = new QStackedWidget(this);
    root->addWidget(m_stack, 1);

    buildSearchPage();
    buildAddPage();
    buildHotkeyPage();
    buildMenu();

    // 输入即筛选
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainUI::refreshList);

    // 全局快捷键：启动时按库里的绑定关系注册，命中后直接执行条目动作
    connect(&HotkeyManager::instance(), &HotkeyManager::triggered,
            this, &MainUI::onHotkeyTriggered);
    reloadHotkeys();

    // 轻提示（复制/登录状态等）
    m_toast = new QLabel(this);
    m_toast->setStyleSheet(QStringLiteral(
        "background: rgba(40,40,70,0.95); color: #e8e8f0;"
        " border: 1px solid rgba(255,255,255,0.1); border-radius: 12px;"
        " padding: 4px 12px; font-size: 12px;"));
    m_toast->hide();
    m_toastTimer = new QTimer(this);
    m_toastTimer->setSingleShot(true);
    connect(m_toastTimer, &QTimer::timeout, m_toast, &QWidget::hide);

    // 点击窗口外部自动隐藏（延迟执行，避免与 dock/托盘点击互斥）
    m_hideTimer = new QTimer(this);
    m_hideTimer->setSingleShot(true);
    m_hideTimer->setInterval(150);
    connect(m_hideTimer, &QTimer::timeout, this, &MainUI::hide);
}

MainUI::~MainUI() = default;

void MainUI::paintEvent(QPaintEvent*)
{
    // 纯色圆角背景（无描边笔），四角由 WA_TranslucentBackground 自然透明
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x1a, 0x1a, 0x2e));
    p.drawRoundedRect(r, 16, 16);
}

void MainUI::changeEvent(QEvent* e)
{
    if (e->type() == QEvent::WindowDeactivate) {
        // 失活可能来自点击窗口外部，也可能是点击自家 Dock 图标。
        // 若鼠标位于本应用其它可见顶层窗口（如 Dock）上，视为内部点击，不自动隐藏。
        const QPoint gp = QCursor::pos();
        bool overOwnTopLevel = false;
        for (QWidget* w : QApplication::topLevelWidgets()) {
            if (w != this && w->isVisible() && w->frameGeometry().contains(gp)) {
                overOwnTopLevel = true;
                break;
            }
        }
        // 有模态弹窗或菜单打开时不处理；刚显示 500ms 内不自动隐藏（防止弹出后
        // 焦点不稳定被立即隐藏）；其余情况延迟隐藏，给 dock/托盘点击留出取消时间
        const bool graceOk = !m_visibleSince.isValid()
                             || m_visibleSince.elapsed() > 500;
        if (m_autoHideEnabled && graceOk && !overOwnTopLevel
            && !QApplication::activeModalWidget()
            && isVisible() && !m_hideTimer->isActive()) {
            m_hideTimer->start();
        }
    } else if (e->type() == QEvent::WindowActivate) {
        // 重新激活（点回窗口）→ 取消待隐藏
        m_hideTimer->stop();
    }
    QWidget::changeEvent(e);
}

void MainUI::cancelPendingHide()
{
    m_hideTimer->stop();
}

bool MainUI::hasPendingHide() const
{
    return m_hideTimer && m_hideTimer->isActive();
}

void MainUI::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragOffset = e->globalPos() - frameGeometry().topLeft();
    }
    QWidget::mousePressEvent(e);
}

void MainUI::mouseMoveEvent(QMouseEvent* e)
{
    if (m_dragging)
        move(e->globalPos() - m_dragOffset);
    QWidget::mouseMoveEvent(e);
}

void MainUI::mouseReleaseEvent(QMouseEvent* e)
{
    m_dragging = false;
    QWidget::mouseReleaseEvent(e);
}

void MainUI::buildTitleBar()
{
    m_titleBar = new TitleBar(this);
    connect(m_titleBar, &TitleBar::menuRequested, this, [this]() {
        m_menu->popup(m_titleBar->mapToGlobal(QPoint(12, 32)));
    });
    connect(m_titleBar, &TitleBar::closeRequested, this, &MainUI::hideWindow);
}

void MainUI::buildMenu()
{
    m_menu = new QMenu(this);
    m_menu->setStyleSheet(QStringLiteral(
        "QMenu { background: rgba(30,30,60,0.97); color: #d0d0e0;"
        " border: 1px solid rgba(255,255,255,0.08); border-radius: 10px; padding: 6px 0; }"
        "QMenu::item { padding: 8px 20px; }"
        "QMenu::item:selected { background: rgba(108,92,231,0.3); color: white; }"));

    auto* loginAct = m_menu->addAction(QStringLiteral("🔐 登录"));
    auto* addAct = m_menu->addAction(QStringLiteral("➕ 添加"));
    auto* hotkeyAct = m_menu->addAction(QStringLiteral("⌨ 快捷键管理"));
    m_menu->addSeparator();
    auto* helpAct = m_menu->addAction(QStringLiteral("❓ 帮助"));
    auto* versionAct = m_menu->addAction(
        QStringLiteral("📌 版本 %1").arg(QStringLiteral(WORKBFF_VERSION)));

    connect(loginAct, &QAction::triggered, this, &MainUI::onMenuLogin);
    connect(addAct, &QAction::triggered, this, &MainUI::onMenuAdd);
    connect(hotkeyAct, &QAction::triggered, this, &MainUI::onMenuHotkeys);
    connect(helpAct, &QAction::triggered, this, &MainUI::onMenuHelp);
    connect(versionAct, &QAction::triggered, this, &MainUI::onMenuVersion);

    // 菜单打开期间挂起自动隐藏
    connect(m_menu, &QMenu::aboutToShow, this, [this]() { m_autoHideEnabled = false; });
    connect(m_menu, &QMenu::aboutToHide, this, [this]() { m_autoHideEnabled = true; });
}

void MainUI::buildSearchPage()
{
    auto* page = new QWidget(m_stack);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(8);

    // 搜索行
    auto* searchRow = new QHBoxLayout;
    searchRow->setSpacing(8);
    m_searchEdit = new QLineEdit(page);
    m_searchEdit->setFixedHeight(32);
    m_searchEdit->setPlaceholderText(QStringLiteral("搜索关键词或路径..."));
    auto* searchBtn = new QPushButton(QStringLiteral("搜索"), page);
    searchBtn->setFixedHeight(32);
    searchBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: #6c5ce7; color: white; border: none;"
        " border-radius: 8px; padding: 0 16px; font-weight: 500; }"
        "QPushButton:hover { background: #7d6ff0; }"));
    connect(searchBtn, &QPushButton::clicked, this, &MainUI::refreshList);

    // 尾部清空按钮：输入内容后显示，点击清空（清空会触发 textChanged，列表自动重新筛选）
    m_clearSearchAct = m_searchEdit->addAction(makeCloseIcon(), QLineEdit::TrailingPosition);
    m_clearSearchAct->setToolTip(QStringLiteral("清空搜索"));
    m_clearSearchAct->setVisible(false);
    connect(m_clearSearchAct, &QAction::triggered, this, [this]() {
        m_searchEdit->clear();
        m_searchEdit->setFocus(); // 清空后继续接着输入
    });
    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        m_clearSearchAct->setVisible(!text.isEmpty());
    });

    searchRow->addWidget(m_searchEdit, 1);
    searchRow->addWidget(searchBtn);
    lay->addLayout(searchRow);

    // 列表：滚动 + 自动换行，行高随内容自适应
    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->setAutoFillBackground(false); // 防止默认白色背景
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }"));
    // 直接在滚动条控件上设置样式，避免级联不生效
    scroll->verticalScrollBar()->setStyleSheet(QStringLiteral(
        "QScrollBar:vertical { background: transparent; width: 6px;"
        " margin: 2px 0; border: none; }"
        "QScrollBar::handle:vertical { background: #4a4a6a; border: none;"
        " border-radius: 3px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: #6a6a8a; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; width: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"));
    m_listContainer = new QWidget;
    m_listLayout = new QVBoxLayout(m_listContainer);
    m_listLayout->setContentsMargins(0, 0, 4, 0);
    m_listLayout->setSpacing(2);
    m_listLayout->addStretch();
    scroll->setWidget(m_listContainer);
    lay->addWidget(scroll, 1);

    // 空态提示（覆盖在列表区域）
    m_emptyLabel = new QLabel(QStringLiteral("🔍 没有匹配的条目\n试试其他关键词"), page);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral(
        "background: transparent; color: #4a4a6a; font-size: 14px;"));
    m_emptyLabel->hide();

    m_stack->addWidget(page);
}

void MainUI::buildAddPage()
{
    m_addPanel = new AddPanel(m_stack);
    connect(m_addPanel, &AddPanel::backRequested, this, [this]() {
        m_stack->setCurrentIndex(0);
        refreshList();
    });
    // 添加页校验提示改用 toast，避免模态框把 popup 连带关闭
    connect(m_addPanel, &AddPanel::hintRequested, this, &MainUI::showToast);
    connect(m_addPanel, &AddPanel::itemCreated, this, [this](const wb::Item& item) {
        auto& db = DatabaseManager::instance();
        if (item.encrypted && m_aesKey.isEmpty()) {
            showToast(QStringLiteral("🔒 加密内容需要先设置/登录密码"));
            return;
        }
        // 快捷键：数量与重复在入库前拦，注册失败则回滚绑定
        if (!item.shortcut.isEmpty()) {
            if (db.queryHotkeyItems().size() >= HotkeyManager::kMaxHotkeys) {
                showToast(QStringLiteral("⌨ 最多只能设置 %1 个快捷键，请先在「快捷键管理」里删除一个")
                              .arg(HotkeyManager::kMaxHotkeys));
                return;
            }
            if (db.shortcutExists(item.shortcut)) {
                showToast(QStringLiteral("⌨ %1 已被其他条目使用")
                              .arg(HotkeyManager::toDisplayText(item.shortcut)));
                return;
            }
        }

        qint64 newId = -1;
        if (!db.addItem(item, m_aesKey, &newId)) {
            showToast(QStringLiteral("❌ 保存失败"));
            return;
        }
        if (item.shortcut.isEmpty()) {
            m_addPanel->reset();
            showToast(QStringLiteral("✅ 添加成功"));
            return;
        }
        QString err;
        if (HotkeyManager::instance().registerHotkey(newId, item.shortcut, &err)) {
            m_addPanel->reset();
            showToast(QStringLiteral("✅ 已添加，快捷键 %1 生效")
                          .arg(HotkeyManager::toDisplayText(item.shortcut)));
        } else {
            // 被别的程序占用：注册不上就把绑定从库里清掉，避免留下用不了的记录
            db.setItemShortcut(newId, QString());
            showToast(QStringLiteral("⌨ %1").arg(err));
        }
    });
    m_stack->addWidget(m_addPanel);
}

void MainUI::refreshList()
{
    // 清空列表（保留末尾 stretch）
    while (m_listLayout->count() > 1) {
        QLayoutItem* item = m_listLayout->takeAt(0);
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }

    const QVector<wb::Item> items =
        DatabaseManager::instance().queryItems(m_searchEdit->text(), m_aesKey);
    for (const wb::Item& it : items) {
        auto* row = new ListItemWidget(it, m_listContainer);
        connect(row, &ListItemWidget::copyRequested, this, &MainUI::onCopyItem);
        connect(row, &ListItemWidget::openRequested, this, &MainUI::onOpenItem);
        connect(row, &ListItemWidget::deleteRequested, this, &MainUI::onDeleteItem);
        m_listLayout->insertWidget(m_listLayout->count() - 1, row);

        // 条目下方分割线
        auto* line = new QWidget(m_listContainer);
        line->setFixedHeight(1);
        line->setStyleSheet(QStringLiteral("background: rgba(255,255,255,0.08); border: none;"));
        m_listLayout->insertWidget(m_listLayout->count() - 1, line);
    }

    m_emptyLabel->setVisible(items.isEmpty());
    if (items.isEmpty())
        m_emptyLabel->setGeometry(0, 80, width(), height() - 120);
}

void MainUI::onCopyItem(const wb::Item& item)
{
    QApplication::clipboard()->setText(item.content);
    showToast(QStringLiteral("已复制: %1").arg(item.content));
}

void MainUI::onOpenItem(const wb::Item& item)
{
    switch (item.type) {
    case wb::ItemType::Folder:
        QDesktopServices::openUrl(QUrl::fromLocalFile(item.content));
        break;
    case wb::ItemType::Exe:
        if (!QProcess::startDetached(item.content))
            showToast(QStringLiteral("启动失败: %1").arg(item.content));
        break;
    case wb::ItemType::Http:
    case wb::ItemType::Scheme: {
        // 内容已带 scheme（http/https 或自定义协议如 omnistation://）时，
        // 直接交给系统 Shell 处理：自定义协议会唤起对应应用（与网页点击一致）。
        // 仅当完全没有 scheme 时才补 http:// 交给浏览器。
        QUrl url(item.content);
        if (url.scheme().isEmpty())
            url = QUrl(QStringLiteral("http://") + item.content);
        QDesktopServices::openUrl(url);
        break;
    }
    case wb::ItemType::Text:
    default:
        break; // 文本：单击已复制到剪贴板
    }
}

void MainUI::onDeleteItem(const wb::Item& item)
{
    if (DatabaseManager::instance().removeItem(item.id)) {
        HotkeyManager::instance().unregisterHotkey(item.id); // 条目没了，热键跟着释放
        showToast(QStringLiteral("已删除: %1").arg(item.content));
        refreshList();
    }
}

void MainUI::onMenuLogin()
{
    auto& db = DatabaseManager::instance();
    const bool setup = !db.hasPassword();
    PasswordDialog dlg(setup ? PasswordDialog::Setup : PasswordDialog::Login, this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    if (setup) {
        if (!db.setPassword(dlg.password())) {
            showToast(QStringLiteral("❌ 保存密码失败"));
        } else {
            m_loggedIn = true;
            m_aesKey = AesCipher::deriveKey(dlg.password());
            showToast(QStringLiteral("✅ 密码已设置并登录"));
        }
    } else {
        if (!db.verifyPassword(dlg.password())) {
            showToast(QStringLiteral("❌ 密码错误"));
        } else {
            m_loggedIn = true;
            m_aesKey = AesCipher::deriveKey(dlg.password());
            showToast(QStringLiteral("✅ 登录成功"));
        }
    }
    refreshList();
}

void MainUI::onMenuAdd()
{
    m_addPanel->reset();
    m_stack->setCurrentWidget(m_addPanel);
}

void MainUI::onMenuHotkeys()
{
    refreshHotkeyList();
    m_stack->setCurrentIndex(2);
}

void MainUI::onMenuHelp()
{
    QMessageBox::about(this, QStringLiteral("帮助"),
                       QStringLiteral("WorkBFF 使用说明\n\n"
                                      "- 单击条目：复制内容到剪贴板\n"
                                      "- 双击条目：文件夹→打开资源管理器，"
                                      "exe→启动程序，http→浏览器打开\n"
                                      "- 加密条目需登录后才能看到\n"
                                      "- 添加条目时勾选“支持快捷键”即可绑定全局热键（最多 5 个）：\n"
                                      "  文本类按下即复制，其他类型按下即打开，窗口隐藏时也能用\n"
                                      "- 解绑请到菜单“⌨ 快捷键管理”，只删绑定不删条目"));
}

void MainUI::onMenuVersion()
{
    QMessageBox::about(this, QStringLiteral("版本"),
                       QStringLiteral("WorkBFF v%1").arg(QStringLiteral(WORKBFF_VERSION)));
}

void MainUI::buildHotkeyPage()
{
    auto* page = new QWidget(m_stack);
    auto* lay = new QVBoxLayout(page);
    lay->setContentsMargins(12, 10, 12, 10);
    lay->setSpacing(8);

    auto* backBtn = new QPushButton(QStringLiteral("← 返回主界面"), page);
    backBtn->setFixedHeight(34); // 加高一点，点击区域更舒服
    backBtn->setCursor(Qt::PointingHandCursor);
    backBtn->setFocusPolicy(Qt::NoFocus); // 不给焦点：消除进页时默认聚焦的虚线框
    backBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; border: none; outline: none;"
        " color: #6c5ce7; font-size: 13px; text-align: left; padding: 0; }"
        "QPushButton:hover { color: #a29bfe; border: none; }"
        "QPushButton:pressed { color: #8577e0; border: none; }"
        "QPushButton:focus, QPushButton:default { border: none; outline: none; }"));
    connect(backBtn, &QPushButton::clicked, this, [this]() {
        m_stack->setCurrentIndex(0);
        refreshList();
    });
    lay->addWidget(backBtn);

    auto* tip = new QLabel(page);
    tip->setWordWrap(true);
    tip->setStyleSheet(QStringLiteral(
        "background: rgba(108,92,231,0.07); color: #a29bfe; font-size: 12px;"
        " border: 1px solid rgba(108,92,231,0.22); border-radius: 10px; padding: 8px 10px;"));
    m_hotkeyTip = tip;
    lay->addWidget(tip);

    auto* scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->viewport()->setAutoFillBackground(false); // 防止默认白色背景
    scroll->setStyleSheet(QStringLiteral(
        "QScrollArea { background: transparent; border: none; }"
        "QScrollArea > QWidget > QWidget { background: transparent; }"));
    scroll->verticalScrollBar()->setStyleSheet(QStringLiteral(
        "QScrollBar:vertical { background: transparent; width: 6px;"
        " margin: 2px 0; border: none; }"
        "QScrollBar::handle:vertical { background: #4a4a6a; border: none;"
        " border-radius: 3px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: #6a6a8a; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; width: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }"));
    m_hotkeyContainer = new QWidget;
    m_hotkeyLayout = new QVBoxLayout(m_hotkeyContainer);
    m_hotkeyLayout->setContentsMargins(0, 0, 4, 0);
    m_hotkeyLayout->setSpacing(6);
    m_hotkeyLayout->addStretch();
    scroll->setWidget(m_hotkeyContainer);
    lay->addWidget(scroll, 1);

    m_stack->addWidget(page);
}

void MainUI::refreshHotkeyList()
{
    // 重建行（保留末尾 stretch）
    while (m_hotkeyLayout->count() > 1) {
        QLayoutItem* item = m_hotkeyLayout->takeAt(0);
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }

    const QVector<wb::Item> items = DatabaseManager::instance().queryHotkeyItems();
    m_hotkeyTip->setText(QStringLiteral(
                           "⌨ 已绑定 %1 / %2 个快捷键，按下即执行条目操作：文本→复制，其他→打开（"
                           "窗口隐藏时也生效）。去添加页勾选“支持快捷键”可新增。")
                       .arg(items.size())
                       .arg(HotkeyManager::kMaxHotkeys));
    if (items.isEmpty()) {
        auto* empty = new QLabel(QStringLiteral("⌨ 还没有快捷键\n\n在添加条目时勾选“支持快捷键”即可绑定"),
                                m_hotkeyContainer);
        empty->setAlignment(Qt::AlignCenter);
        empty->setStyleSheet(QStringLiteral(
            "background: rgba(255,255,255,0.04); border: 1px dashed rgba(255,255,255,0.12);"
            " border-radius: 12px; color: #6a6a8a; font-size: 13px; padding: 24px;"));
        m_hotkeyLayout->insertWidget(0, empty);
        return;
    }

    for (const wb::Item& it : items) {
        auto* row = new QWidget(m_hotkeyContainer);
        row->setObjectName(QStringLiteral("hkRow")); // 用名字限定规则，不级联到子控件
        row->setStyleSheet(QStringLiteral(
            "QWidget#hkRow { background: rgba(255,255,255,0.05);"
            " border: 1px solid rgba(255,255,255,0.07); border-radius: 10px; }"
            "QWidget#hkRow:hover { background: rgba(108,92,231,0.1);"
            " border-color: rgba(108,92,231,0.35); }"));
        auto* rowLay = new QHBoxLayout(row);
        rowLay->setContentsMargins(10, 7, 8, 7);
        rowLay->setSpacing(10);

        // “键帽”样式：让快捷键一眼就能认出来
        auto* keyLabel = new QLabel(HotkeyManager::toDisplayText(it.shortcut), row);
        keyLabel->setStyleSheet(QStringLiteral(
            "background: rgba(108,92,231,0.22); color: #d8d4ff; font-size: 12px;"
            " font-weight: 700; border: 1px solid rgba(108,92,231,0.45);"
            " border-radius: 6px; padding: 4px 10px;"));

        QString summary = it.keywords.isEmpty() ? it.content
                                                : QStringLiteral("%1 · %2").arg(it.keywords, it.content);
        summary = summary.left(36) + (summary.size() > 36 ? QStringLiteral("…") : QString());
        auto* descLabel = new QLabel(summary, row);
        descLabel->setToolTip(it.content); // 悬停看完整内容
        descLabel->setStyleSheet(QStringLiteral(
            "background: transparent; color: #e8e8f0; font-size: 12px;"));

        auto* unbindBtn = new QPushButton(QStringLiteral("解除绑定"), row);
        unbindBtn->setCursor(Qt::PointingHandCursor);
        unbindBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: rgba(255,107,107,0.12); color: #ff8f8f; border: 1px solid"
            " rgba(255,107,107,0.3); border-radius: 8px; padding: 5px 12px; font-size: 12px; }"
            "QPushButton:hover { background: rgba(255,107,107,0.28); color: white;"
            " border-color: rgba(255,107,107,0.5); }"));
        const qint64 itemId = it.id;
        connect(unbindBtn, &QPushButton::clicked, this, [this, itemId]() {
            if (!DatabaseManager::instance().setItemShortcut(itemId, QString())) {
                showToast(QStringLiteral("❌ 解绑失败"));
                return;
            }
            HotkeyManager::instance().unregisterHotkey(itemId);
            showToast(QStringLiteral("已解除快捷键绑定（条目保留）"));
            refreshHotkeyList();
        });

        rowLay->addWidget(keyLabel);
        rowLay->addWidget(descLabel, 1);
        rowLay->addWidget(unbindBtn);
        m_hotkeyLayout->insertWidget(m_hotkeyLayout->count() - 1, row);
    }
}

void MainUI::reloadHotkeys()
{
    auto& hk = HotkeyManager::instance();
    hk.unregisterAll();
    const QVector<wb::Item> bound = DatabaseManager::instance().queryHotkeyItems();
    // 启动阶段注册失败（被其他程序占用等）只记日志：条目在管理页里可见，用户可解绑重绑
    for (const wb::Item& it : bound) {
        QString err;
        if (!hk.registerHotkey(it.id, it.shortcut, &err)) {
            qWarning() << "[hotkey] register" << it.shortcut << "failed:" << err;
        }
    }
}

void MainUI::onHotkeyTriggered(qint64 itemId)
{
    wb::Item item;
    if (!DatabaseManager::instance().itemById(itemId, &item)) {
        // 条目已删除或变成了加密项：释放热键，别让它白白占位
        HotkeyManager::instance().unregisterHotkey(itemId);
        return;
    }
    // 与列表里的点击行为一致：文本复制，其他类型打开
    if (item.type == wb::ItemType::Text)
        onCopyItem(item);
    else
        onOpenItem(item);
}

void MainUI::showCentered()
{
    m_hideTimer->stop(); // 打开时取消待隐藏
    m_visibleSince.start(); // 记录显示时刻（宽限期）
    refreshList(); // 每次打开都重新加载（搜索框为空 → 只显示第 1 优先级）
    const QRect sg = QGuiApplication::primaryScreen()->availableGeometry();
    move(sg.center() - QPoint(width() / 2, height() / 2));
    show();
    raise();
    activateWindow(); // 确保显示后拿到焦点，避免随即又被判定为失活而隐藏
}

void MainUI::hideWindow()
{
    m_hideTimer->stop();
    hide();
}

void MainUI::showToast(const QString& msg)
{
    m_toast->setText(msg);
    m_toast->adjustSize();
    const int w = m_toast->width() + 24;
    m_toast->setGeometry((width() - w) / 2, height() - 60, w, 32);
    m_toast->show();
    m_toast->raise();
    m_toastTimer->start(1500);
}


