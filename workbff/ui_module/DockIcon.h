#pragma once

#include <QIcon>
#include <QPoint>
#include <QWidget>

class QTimer;

// 32×32 圆形 Dock 图标：
// - 无边框、始终置顶、不进任务栏（Qt::Tool）
// - hover 时彩色跑马灯动画
// - 可拖拽移动；未拖拽的单击发出 clicked()
class DockIcon : public QWidget
{
    Q_OBJECT
public:
    explicit DockIcon(QWidget* parent = nullptr);

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void enterEvent(QEvent* e) override;
    void leaveEvent(QEvent* e) override;

private:
    QTimer* m_animTimer = nullptr;
    QIcon m_icon;
    int m_phase = 0;        // 跑马灯相位（随时间递增）
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_dragging = false;
    QPoint m_pressPos;
    QPoint m_dragOffset;
};
