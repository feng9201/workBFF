#include "ui_module/TitleBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>

TitleBar::TitleBar(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(32);
    setObjectName(QStringLiteral("titleBar"));
    // 方形背景由主窗口圆角蒙版裁剪，无需自绘
    setStyleSheet(QStringLiteral(
        "#titleBar { background: rgba(255,255,255,0.07);"
        " border-bottom: 1px solid rgba(255,255,255,0.07); }"
        "QLabel { color: #c8c8d4; background: transparent; }"));

    m_appLabel = new QLabel(QStringLiteral("⚡ WorkBFF"), this);
    m_menuBtn = new QLabel(QStringLiteral("☰"), this);
    m_menuBtn->setCursor(Qt::PointingHandCursor);
    m_menuBtn->setStyleSheet(QStringLiteral("padding:0 8px; font-size:15px;"));
    m_closeBtn = new QLabel(QStringLiteral("✕"), this);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setStyleSheet(QStringLiteral("padding:0 10px; font-size:13px;"));

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(12, 0, 8, 0);
    lay->setSpacing(4);
    lay->addWidget(m_appLabel);
    lay->addWidget(m_menuBtn);
    lay->addStretch();
    lay->addWidget(m_closeBtn);
}

void TitleBar::setAppName(const QString& name)
{
    m_appLabel->setText(name);
}

void TitleBar::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_pressed = true;
    m_dragging = false;
    m_pressPos = e->globalPos();
    m_dragOffset = m_pressPos - window()->frameGeometry().topLeft();
}

void TitleBar::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_pressed)
        return;
    if (!m_dragging && (e->globalPos() - m_pressPos).manhattanLength() > 3)
        m_dragging = true;
    if (m_dragging)
        window()->move(e->globalPos() - m_dragOffset);
}

void TitleBar::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !m_pressed)
        return;
    if (!m_dragging) {
        const QPoint local = e->pos();
        if (m_menuBtn->geometry().contains(local))
            emit menuRequested();
        else if (m_closeBtn->geometry().contains(local))
            emit closeRequested();
    }
    m_pressed = false;
    m_dragging = false;
}
