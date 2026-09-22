#include "ui_module/AddPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QVBoxLayout>

namespace {
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

    m_encrypted = new QCheckBox(QStringLiteral("加密（登录后才能看到）"), this);

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
    m_encrypted->setChecked(false);
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
    item.priority = m_levelPriority->currentIndex() + 1;
    item.displayPriority = m_displayPriority->currentIndex() + 1;

    emit itemCreated(item);
}
