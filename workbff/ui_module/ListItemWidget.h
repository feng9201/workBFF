#pragma once

#include <QWidget>

#include "model/Item.h"

class QLabel;
class QPushButton;
class QTimer;

// 列表中的一行：
// - 内容 label 自动换行，行高随内容自适应
// - hover 高亮
// - 单击 = 复制内容，双击 = 打开（通过定时器区分单击/双击）
// - 右上角删除按钮：点击删除该项
class ListItemWidget : public QWidget
{
    Q_OBJECT
public:
    explicit ListItemWidget(const wb::Item& item, QWidget* parent = nullptr);

    const wb::Item& item() const { return m_item; }

signals:
    void copyRequested(const wb::Item& item);
    void openRequested(const wb::Item& item);
    void deleteRequested(const wb::Item& item);

protected:
    void enterEvent(QEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    wb::Item m_item;
    QLabel* m_typeBadge = nullptr;
    QLabel* m_contentLabel = nullptr;
    QLabel* m_extraLabel = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    QTimer* m_clickTimer = nullptr;
    bool m_pressed = false;
};
