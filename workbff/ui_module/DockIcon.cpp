#include "ui_module/DockIcon.h"

#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>

DockIcon::DockIcon(QWidget* parent)
    : QWidget(parent)
    , m_icon(QStringLiteral(":/app.ico"))
{
    setFixedSize(32, 32);
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setToolTip(QStringLiteral("WorkBFF"));

    m_animTimer = new QTimer(this);
    m_animTimer->setInterval(40);
    connect(m_animTimer, &QTimer::timeout, this, [this]() {
        ++m_phase;
        update();
    });
}

void DockIcon::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);

    // 圆形渐变背景
    QLinearGradient grad(r.topLeft(), r.bottomRight());
    grad.setColorAt(0.0, QColor(0x8e, 0x7b, 0xff));
    grad.setColorAt(1.0, QColor(0x5b, 0x4b, 0xd6));
    p.setPen(Qt::NoPen);
    p.setBrush(grad);
    p.drawEllipse(r);

    // 应用图标（裁剪在圆内）
    if (!m_icon.isNull()) {
        QPainterPath clip;
        clip.addEllipse(r);
        p.save();
        p.setClipPath(clip);
        m_icon.paint(&p, QRect(6, 6, 20, 20));
        p.restore();
    }

    // hover 彩色跑马灯
    if (m_hovered) {
        const int nSeg = 12;
        const QRectF ring = r.adjusted(-0.5, -0.5, 0.5, 0.5);
        QPen pen;
        pen.setWidthF(2.0);
        pen.setCapStyle(Qt::RoundCap);
        const int segSpan = static_cast<int>((360.0 / nSeg - 10.0) * 16.0);
        for (int i = 0; i < nSeg; ++i) {
            const int hue = (m_phase * 8 + i * (360 / nSeg)) % 360;
            pen.setColor(QColor::fromHsv(hue, 220, 255));
            p.setPen(pen);
            const int start = static_cast<int>((i * (360.0 / nSeg) + m_phase * 4) * 16.0);
            p.drawArc(ring, start, segSpan);
        }
    }
    p.end();
}

void DockIcon::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_pressed = true;
    m_dragging = false;
    m_pressPos = e->globalPos();
    m_dragOffset = m_pressPos - frameGeometry().topLeft();
}

void DockIcon::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_pressed)
        return;
    if (!m_dragging && (e->globalPos() - m_pressPos).manhattanLength() > 3)
        m_dragging = true;
    if (m_dragging)
        move(e->globalPos() - m_dragOffset);
}

void DockIcon::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton && m_pressed) {
        if (!m_dragging)
            emit clicked();
        m_pressed = false;
        m_dragging = false;
    }
}

void DockIcon::enterEvent(QEvent*)
{
    m_hovered = true;
    m_animTimer->start();
    update();
}

void DockIcon::leaveEvent(QEvent*)
{
    m_hovered = false;
    m_animTimer->stop();
    update();
}
