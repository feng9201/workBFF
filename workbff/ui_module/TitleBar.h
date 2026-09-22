#pragma once

#include <QPoint>
#include <QWidget>

class QLabel;

// 32px 高无边框标题栏：
// - 左侧应用名 + 菜单按钮（☰），右侧关闭按钮（✕）
// - 空白区域按下可拖拽整个窗口
class TitleBar : public QWidget
{
    Q_OBJECT
public:
    explicit TitleBar(QWidget* parent = nullptr);
    void setAppName(const QString& name);

signals:
    void menuRequested();
    void closeRequested();

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    QLabel* m_appLabel = nullptr;
    QLabel* m_menuBtn = nullptr;
    QLabel* m_closeBtn = nullptr;
    bool m_pressed = false;
    bool m_dragging = false;
    QPoint m_pressPos;
    QPoint m_dragOffset;
};
