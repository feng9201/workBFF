#include "ui_module/AddPanel.h"

#include "base/hotkeyManager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace {

// 暗色主题下原生勾选框是灰白小方块，很突兀：统一改成圆角方块，选中填主题紫
// 必须写在控件级样式里 —— 面板级的 QCheckBox{} 规则对 indicator 常常不生效
QString checkStyle()
{
    return QStringLiteral(
        "QCheckBox { color: #8888aa; background: transparent; spacing: 8px; }"
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 5px;"
        " border: 1px solid rgba(255,255,255,0.18); background: rgba(255,255,255,0.06); }"
        "QCheckBox::indicator:hover { border-color: #6c5ce7; background: rgba(108,92,231,0.15); }"
        "QCheckBox::indicator:checked { background: #6c5ce7; border-color: #6c5ce7; }"
        "QCheckBox::indicator:disabled { background: rgba(255,255,255,0.03);"
        " border-color: rgba(255,255,255,0.08); }");
}

QWidget* makeField(QVBoxLayout* lay, const QString& labelText, QWidget* field)
{
    auto* w = new QWidget;
    auto* l = new QVBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(4);
    auto* label = new QLabel(labelText);
    label->setStyleSheet(QStringLiteral("background: transparent; color: #8888aa; font-size: 12px;"));
    l->addWidget(label);
    l->addWidget(field);
    lay->addWidget(w);
    return w;
}
} // namespace

AddPanel::AddPanel(QWidget* parent)
    : QWidget(parent)
{
    // 外层滚动区域：内容变多时防止超出窗口
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
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

    auto* inner = new QWidget;
    auto* root = new QVBoxLayout(inner);
    root->setContentsMargins(24, 16, 24, 16);
    root->setSpacing(10);

    auto* backBtn = new QPushButton(QStringLiteral("← 返回主界面"), this);
    backBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; color: #6c5ce7; border: none;"
        " font-size: 13px; text-align: left; }"
        "QPushButton:hover { color: #a29bfe; }"));
    connect(backBtn, &QPushButton::clicked, this, &AddPanel::backRequested);
    root->addWidget(backBtn);

    m_kw1 = new QLineEdit(this);
    m_kw2 = new QLineEdit(this);
    m_kw3 = new QLineEdit(this);
    makeField(root, QStringLiteral("搜索关键词 1（必填）"), m_kw1);
    makeField(root, QStringLiteral("搜索关键词 2（可选）"), m_kw2);
    makeField(root, QStringLiteral("搜索关键词 3（可选）"), m_kw3);

    m_content = new QPlainTextEdit(this);
    m_content->setFixedHeight(80);
    m_content->setPlaceholderText(QStringLiteral(
        "文本 / 文件夹路径 / exe 路径 / http 地址 / scheme 协议\n支持多行输入"));
    m_browseBtn = new QPushButton(QStringLiteral("浏览..."), this);
    m_browseBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: rgba(255,255,255,0.06); color: #b0b0c8;"
        " border: 1px solid rgba(255,255,255,0.1); border-radius: 6px; padding: 6px 12px; }"
        "QPushButton:hover { background: rgba(255,255,255,0.12); }"));

    auto* contentWrap = new QWidget;
    auto* contentWrapLay = new QVBoxLayout(contentWrap);
    contentWrapLay->setContentsMargins(0, 0, 0, 0);
    contentWrapLay->setSpacing(4);
    auto* contentLabel = new QLabel(QStringLiteral("内容"));
    contentLabel->setStyleSheet(QStringLiteral("background: transparent; color: #8888aa; font-size: 12px;"));
    contentWrapLay->addWidget(contentLabel);
    auto* contentRow = new QHBoxLayout;
    contentRow->setSpacing(8);
    contentRow->addWidget(m_content, 1);
    contentRow->addWidget(m_browseBtn);
    contentWrapLay->addLayout(contentRow);
    root->addWidget(contentWrap);

    m_type = new QComboBox(this);
    m_type->addItem(QStringLiteral("📝 文本"));
    m_type->addItem(QStringLiteral("📁 文件夹"));
    m_type->addItem(QStringLiteral("⚙️ 可执行文件"));
    m_type->addItem(QStringLiteral("🌐 HTTP 地址"));
    m_type->addItem(QStringLiteral("🔗 Scheme 协议"));

    m_levelPriority = new QComboBox(this);
    m_levelPriority->addItem(QStringLiteral("1（最高）"));
    m_levelPriority->addItem(QStringLiteral("2"));
    m_levelPriority->addItem(QStringLiteral("3"));
    m_levelPriority->setCurrentIndex(0);

    m_displayPriority = new QComboBox(this);
    for (int i = 1; i <= 5; ++i)
        m_displayPriority->addItem(QString::number(i));
    m_displayPriority->setCurrentIndex(0);

    m_encrypted = new QCheckBox(QStringLiteral("🔒 加密（登录后才能看到）"), this);
    m_encrypted->setStyleSheet(checkStyle());
    m_encrypted->setCursor(Qt::PointingHandCursor);

    auto* typeRow = new QHBoxLayout;
    typeRow->setSpacing(8);
    typeRow->addWidget(m_type, 1);
    auto* priLabel = new QLabel(QStringLiteral("优先级"), this);
    priLabel->setStyleSheet(QStringLiteral(
        "background: transparent; color: #8888aa; font-size: 12px;"));
    typeRow->addWidget(priLabel);
    typeRow->addWidget(m_levelPriority);
    auto* dispLabel = new QLabel(QStringLiteral("显示优先级"), this);
    dispLabel->setStyleSheet(QStringLiteral(
        "background: transparent; color: #8888aa; font-size: 12px;"));
    typeRow->addWidget(dispLabel);
    typeRow->addWidget(m_displayPriority);
    root->addLayout(typeRow);
    // 加密单独一行，避免拥挤
    root->addWidget(m_encrypted);

    // 快捷键：勾选后才出现录制卡片，与加密互斥
    m_hotkeyEnabled = new QCheckBox(QStringLiteral("⌨ 支持快捷键（全局生效，最多 5 个）"), this);
    m_hotkeyEnabled->setStyleSheet(checkStyle());
    m_hotkeyEnabled->setCursor(Qt::PointingHandCursor);
    root->addWidget(m_hotkeyEnabled);

    // 录制卡片：紫色调底 + 虚线框输入位，和上面的复选框形成视觉归属
    m_hotkeyWrap = new QWidget;
    m_hotkeyWrap->setObjectName(QStringLiteral("hotkeyCard"));
    m_hotkeyWrap->setAttribute(Qt::WA_StyledBackground, true);
    auto* hkLay = new QHBoxLayout(m_hotkeyWrap);
    hkLay->setContentsMargins(12, 9, 12, 9);
    hkLay->setSpacing(8);
    auto* hkLabel = new QLabel(QStringLiteral("设置快捷键"), m_hotkeyWrap);
    hkLabel->setStyleSheet(QStringLiteral(
        "background: transparent; color: #a29bfe; font-size: 12px; font-weight: 600;"));
    m_hotkeyEdit = new QLineEdit(m_hotkeyWrap);
    m_hotkeyEdit->setReadOnly(true); // 内容只能由录制得来，不接受直接输入
    m_hotkeyEdit->setFixedHeight(30);
    m_hotkeyEdit->setAlignment(Qt::AlignCenter);
    m_hotkeyEdit->setCursor(Qt::PointingHandCursor); // 只读，作用是点它开始录制
    m_hotkeyEdit->setPlaceholderText(QStringLiteral("按下组合键，如 Ctrl+W"));
    m_hotkeyEdit->setToolTip(QStringLiteral("点击此处后按组合键录制，Backspace 或“清除”可重置"));
    m_hotkeyEdit->setStyleSheet(QStringLiteral(
        "QLineEdit { background: rgba(108,92,231,0.12);"
        " border: 1px dashed rgba(108,92,231,0.45); border-radius: 8px;"
        " padding: 0 10px; color: #d8d4ff; font-size: 13px; font-weight: 600; }"
        "QLineEdit:focus { border-style: solid; border-color: #6c5ce7;"
        " background: rgba(108,92,231,0.22); }"));
    QPalette hkPal = m_hotkeyEdit->palette();
    hkPal.setColor(QPalette::PlaceholderText, QColor(0x7a, 0x7a, 0x9e));
    m_hotkeyEdit->setPalette(hkPal);

    m_clearHotkeyBtn = new QPushButton(QStringLiteral("清除"), m_hotkeyWrap);
    m_clearHotkeyBtn->setObjectName(QStringLiteral("clearHkBtn"));
    m_clearHotkeyBtn->setEnabled(false); // 录制到按键后才可点
    m_clearHotkeyBtn->setCursor(Qt::PointingHandCursor);
    hkLay->addWidget(hkLabel);
    hkLay->addWidget(m_hotkeyEdit, 1);
    hkLay->addWidget(m_clearHotkeyBtn);
    m_hotkeyWrap->hide();
    root->addWidget(m_hotkeyWrap);
    m_hotkeyEdit->installEventFilter(this);

    auto* actions = new QHBoxLayout;
    actions->setSpacing(10);
    auto* saveBtn = new QPushButton(QStringLiteral("保存"), this);
    saveBtn->setObjectName(QStringLiteral("saveBtn"));
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"), this);
    cancelBtn->setObjectName(QStringLiteral("cancelBtn"));
    actions->addWidget(saveBtn);
    actions->addWidget(cancelBtn);
    root->addLayout(actions);

    root->addStretch();

    scroll->setWidget(inner);
    outer->addWidget(scroll);

    // 表单整体样式
    setStyleSheet(QStringLiteral(
        "QLineEdit, QPlainTextEdit, QComboBox { background: rgba(255,255,255,0.06);"
        " border: 1px solid rgba(255,255,255,0.1); border-radius: 8px;"
        " padding: 7px 10px; color: #e8e8f0; }"
        "QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus { border-color: #6c5ce7; }"
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QCheckBox { color: #b0b0c8; background: transparent; }"
        "QWidget#hotkeyCard { background: rgba(108,92,231,0.07);"
        " border: 1px solid rgba(108,92,231,0.22); border-radius: 10px; }"
        "QPushButton#clearHkBtn { background: rgba(255,255,255,0.06); color: #b0b0c8;"
        " border: 1px solid rgba(255,255,255,0.1); border-radius: 8px; padding: 5px 12px; }"
        "QPushButton#clearHkBtn:hover { background: rgba(255,255,255,0.12); color: white; }"
        "QPushButton#clearHkBtn:disabled { color: #4a4a6a; background: rgba(255,255,255,0.03);"
        " border-color: rgba(255,255,255,0.05); }"
        "QPushButton#saveBtn { background: #6c5ce7; color: white;"
        " border: none; border-radius: 8px; padding: 9px; font-weight: 500; }"
        "QPushButton#saveBtn:hover { background: #7d6ff0; }"
        "QPushButton#cancelBtn { background: rgba(255,255,255,0.06); color: #b0b0c8;"
        " border: none; border-radius: 8px; padding: 9px; }"));

    connect(saveBtn, &QPushButton::clicked, this, &AddPanel::onSave);
    connect(cancelBtn, &QPushButton::clicked, this, &AddPanel::backRequested);
    connect(m_browseBtn, &QPushButton::clicked, this, &AddPanel::onBrowse);
    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AddPanel::onTypeChanged);

    // 勾选快捷键 → 显示录制卡片，同时锁住加密（两者互斥）
    connect(m_hotkeyEnabled, &QCheckBox::toggled, this, [this](bool on) {
        m_hotkeyWrap->setVisible(on);
        m_encrypted->setEnabled(!on);
        if (!on) {
            m_hotkeyValue.clear();
            m_hotkeyEdit->clear();
            m_clearHotkeyBtn->setEnabled(false);
        } else {
            m_hotkeyEdit->setFocus(); // 展开即聚焦，直接按组合键就能录
        }
    });
    connect(m_clearHotkeyBtn, &QPushButton::clicked, this, [this]() {
        m_hotkeyValue.clear();
        m_hotkeyEdit->clear();
        m_clearHotkeyBtn->setEnabled(false);
        m_hotkeyEdit->setFocus();
    });
    connect(m_encrypted, &QCheckBox::toggled, this, [this](bool on) {
        m_hotkeyEnabled->setEnabled(!on);
        if (on)
            m_hotkeyEnabled->setChecked(false); // 由上面的 toggled 负责隐录制行并清空已录值
        m_hotkeyEnabled->setToolTip(on
            ? QStringLiteral("加密条目不能绑快捷键：未登录时解不出明文")
            : QString());
    });
}

void AddPanel::reset()
{
    m_kw1->clear();
    m_kw2->clear();
    m_kw3->clear();
    m_content->clear();
    m_type->setCurrentIndex(0);
    m_levelPriority->setCurrentIndex(0);
    m_displayPriority->setCurrentIndex(0);
    m_hotkeyEnabled->setChecked(false); // 触发 toggled：隐录制卡片 + 清空 m_hotkeyValue
    m_hotkeyEdit->clear();
    m_clearHotkeyBtn->setEnabled(false);
    m_encrypted->setChecked(false);
    // 本来就是未勾选时 setChecked 不会发信号，互斥带来的禁用状态需要显式恢复
    m_hotkeyEnabled->setEnabled(true);
    m_encrypted->setEnabled(true);
}

bool AddPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_hotkeyEdit && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Backspace || keyEvent->key() == Qt::Key_Delete) {
            m_hotkeyValue.clear();
            m_hotkeyEdit->clear();
            m_clearHotkeyBtn->setEnabled(false);
            return true;
        }
        QString err;
        const QString shortcut = HotkeyManager::fromKeyEvent(keyEvent, &err);
        if (shortcut.isEmpty()) {
            if (!err.isEmpty())
                emit hintRequested(QStringLiteral("⌨ ") + err);
            return true; // 录制框不吃任何按键，避免只读框自己响应编辑操作
        }
        m_hotkeyValue = shortcut;
        m_hotkeyEdit->setText(HotkeyManager::toDisplayText(shortcut));
        m_clearHotkeyBtn->setEnabled(true); // 录制成功后才允许清除
        return true;
    }
    return QWidget::eventFilter(watched, event);
}

void AddPanel::onTypeChanged(int index)
{
    const bool canBrowse = (index == 1 || index == 2); // 文件夹 / exe
    m_browseBtn->setEnabled(canBrowse);
}

void AddPanel::onBrowse()
{
    QString path;
    if (m_type->currentIndex() == 1) { // 文件夹
        path = QFileDialog::getExistingDirectory(this, QStringLiteral("选择文件夹"),
                                                 m_content->toPlainText());
    } else if (m_type->currentIndex() == 2) { // exe
        path = QFileDialog::getOpenFileName(this, QStringLiteral("选择可执行文件"),
                                            m_content->toPlainText(),
                                            QStringLiteral("程序 (*.exe)"));
    }
    if (!path.isEmpty())
        m_content->setPlainText(path);
}

void AddPanel::onSave()
{
    QStringList keywords;
    for (QLineEdit* edit : { m_kw1, m_kw2, m_kw3 }) {
        const QString kw = edit->text().trimmed();
        if (!kw.isEmpty())
            keywords << kw;
    }
    const QString content = m_content->toPlainText().trimmed();

    if (keywords.isEmpty()) {
        emit hintRequested(QStringLiteral("请至少填写一个搜索关键词"));
        return;
    }
    if (content.isEmpty()) {
        emit hintRequested(QStringLiteral("请填写内容"));
        return;
    }
    if (m_hotkeyEnabled->isChecked() && m_hotkeyValue.isEmpty()) {
        emit hintRequested(QStringLiteral("请先录制快捷键：点击输入框后按下组合键"));
        return;
    }

    wb::Item item;
    item.keywords = keywords.join(QStringLiteral(", "));
    item.content = content;
    switch (m_type->currentIndex()) {
    case 1: item.type = wb::ItemType::Folder; break;
    case 2: item.type = wb::ItemType::Exe;    break;
    case 3: item.type = wb::ItemType::Http;   break;
    case 4: item.type = wb::ItemType::Scheme; break;
    default: item.type = wb::ItemType::Text;  break;
    }
    item.encrypted = m_encrypted->isChecked();
    item.shortcut = m_hotkeyEnabled->isChecked() ? m_hotkeyValue : QString();
    item.priority = m_levelPriority->currentIndex() + 1;
    item.displayPriority = m_displayPriority->currentIndex() + 1;

    emit itemCreated(item);
}
