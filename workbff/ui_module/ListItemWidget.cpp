#include "ui_module/ListItemWidget.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace {

QString typeBadgeText(wb::ItemType t)
{
    switch (t) {
    case wb::ItemType::Folder: return QStringLiteral("📁 文件夹");
    case wb::ItemType::Exe:    return QStringLiteral("⚙️ 可执行");
    case wb::ItemType::Http:   return QStringLiteral("🌐 链接");
    case wb::ItemType::Scheme: return QStringLiteral("🔗 协议");
    case wb::ItemType::Text:
    default:                   return QStringLiteral("📝 文本");
    }
}

QString typeBadgeColor(wb::ItemType t)
{
    switch (t) {
    case wb::ItemType::Folder: return QStringLiteral("#4fc3f7");
    case wb::ItemType::Exe:    return QStringLiteral("#ffb74d");
    case wb::ItemType::Http:   return QStringLiteral("#81c784");
    case wb::ItemType::Scheme: return QStringLiteral("#ff9ff3");
    case wb::ItemType::Text:
    default:                   return QStringLiteral("#a29bfe");
    }
}

} // namespace

ListItemWidget::ListItemWidget(const wb::Item& item, QWidget* parent)
    : QWidget(parent)
    , m_item(item)
{
    setObjectName(QStringLiteral("listItem"));
    setStyleSheet(QStringLiteral("#listItem { background: transparent; border-radius: 8px; }"));
    setCursor(Qt::PointingHandCursor);

    m_typeBadge = new QLabel(typeBadgeText(item.type), this);
    m_typeBadge->setStyleSheet(QStringLiteral("background: transparent; font-size: 11px; color: %1;")
                                   .arg(typeBadgeColor(item.type)));

    m_contentLabel = new QLabel(item.content, this);
    m_contentLabel->setWordWrap(true);
    m_contentLabel->setTextFormat(Qt::PlainText); // 保留 \n 换行，命令中的 <>& 等按字面显示
    m_contentLabel->setTextInteractionFlags(Qt::NoTextInteraction);
    m_contentLabel->setStyleSheet(QStringLiteral(
        "background: transparent; color: #e8e8f0; font-size: 13px;"));

    QString extra;
    if (item.encrypted)
        extra += QStringLiteral("🔒 加密");
    if (!item.keywords.trimmed().isEmpty()) {
        if (!extra.isEmpty())
            extra += QStringLiteral("   ");
        extra += item.keywords;
    }
    m_extraLabel = new QLabel(extra, this);
    m_extraLabel->setStyleSheet(QStringLiteral(
        "background: transparent; color: #5a5a7a; font-size: 11px;"));

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 6, 10, 6);
    mainLayout->setSpacing(2);

    auto* topRow = new QHBoxLayout;
    topRow->setSpacing(8);
    topRow->addWidget(m_typeBadge);
    topRow->addStretch();
    topRow->addWidget(m_extraLabel);

    // 删除按钮（右上角，hover 变红）
    m_deleteBtn = new QPushButton(QStringLiteral("✕"), this);
    m_deleteBtn->setCursor(Qt::PointingHandCursor);
    m_deleteBtn->setFixedSize(18, 18);
    m_deleteBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: transparent; color: #5a5a7a; border: none;"
        " border-radius: 9px; font-size: 11px; }"
        "QPushButton:hover { background: rgba(255,107,107,0.25); color: #ff6b6b; }"));
    connect(m_deleteBtn, &QPushButton::clicked, this, [this]() {
        emit deleteRequested(m_item);
    });
    topRow->addWidget(m_deleteBtn);

    mainLayout->addLayout(topRow);
    mainLayout->addWidget(m_contentLabel);

    // 单击复制 / 双击打开的判定
    m_clickTimer = new QTimer(this);
    m_clickTimer->setSingleShot(true);
    m_clickTimer->setInterval(QApplication::doubleClickInterval());
    connect(m_clickTimer, &QTimer::timeout, this, [this]() {
        emit copyRequested(m_item);
    });
}

void ListItemWidget::enterEvent(QEvent* e)
{
    setStyleSheet(QStringLiteral(
        "#listItem { background: rgba(108,92,231,0.18); border-radius: 8px; }"));
    QWidget::enterEvent(e);
}

void ListItemWidget::leaveEvent(QEvent* e)
{
    setStyleSheet(QStringLiteral(
        "#listItem { background: transparent; border-radius: 8px; }"));
    QWidget::leaveEvent(e);
}

void ListItemWidget::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton)
        m_pressed = true;
    QWidget::mousePressEvent(e);
}

void ListItemWidget::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && m_pressed && rect().contains(e->pos()))
        m_clickTimer->start(); // 等 doubleClickInterval，若未双击则触发复制
    m_pressed = false;
    QWidget::mouseReleaseEvent(e);
}

void ListItemWidget::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        m_clickTimer->stop(); // 取消复制，改为打开
        emit openRequested(m_item);
    }
    QWidget::mouseDoubleClickEvent(e);
}
