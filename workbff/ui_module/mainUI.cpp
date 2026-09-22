#include "ui_module/mainUI.h"

#include "Version.h"
#include "crypto/AesCipher.h"
#include "db/DatabaseManager.h"
#include "ui_module/AddPanel.h"
#include "ui_module/ListItemWidget.h"
#include "ui_module/PasswordDialog.h"
#include "ui_module/TitleBar.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDesktopServices>
#include <QFrame>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPen>
#include <QProcess>
#include <QPushButton>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

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
    buildMenu();

    // 输入即筛选
    connect(m_searchEdit, &QLineEdit::textChanged, this, &MainUI::refreshList);

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
    m_menu->addSeparator();
    auto* helpAct = m_menu->addAction(QStringLiteral("❓ 帮助"));
    auto* versionAct = m_menu->addAction(
        QStringLiteral("📌 版本 %1").arg(QStringLiteral(WORKBFF_VERSION)));

    connect(loginAct, &QAction::triggered, this, &MainUI::onMenuLogin);
    connect(addAct, &QAction::triggered, this, &MainUI::onMenuAdd);
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
        if (db.addItem(item, m_aesKey)) {
            // 保存成功后停留在添加页，仅提示，可继续添加
            m_addPanel->reset();
            showToast(QStringLiteral("✅ 添加成功"));
        } else {
            showToast(QStringLiteral("❌ 保存失败"));
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

void MainUI::onMenuHelp()
{
    QMessageBox::about(this, QStringLiteral("帮助"),
                       QStringLiteral("WorkBFF 使用说明\n\n"
                                      "- 单击条目：复制内容到剪贴板\n"
                                      "- 双击条目：文件夹→打开资源管理器，"
                                      "exe→启动程序，http→浏览器打开\n"
                                      "- 加密条目需登录后才能看到"));
}

void MainUI::onMenuVersion()
{
    QMessageBox::about(this, QStringLiteral("版本"),
                       QStringLiteral("WorkBFF v%1").arg(QStringLiteral(WORKBFF_VERSION)));
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


