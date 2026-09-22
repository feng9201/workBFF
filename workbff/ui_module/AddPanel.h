#pragma once

#include <QWidget>

#include "model/Item.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

// 添加界面：最多3个关键词 + 内容(多行) + 类型 + 是否加密
class AddPanel : public QWidget
{
    Q_OBJECT
public:
    explicit AddPanel(QWidget* parent = nullptr);

    void reset();

signals:
    void backRequested();
    void itemCreated(const wb::Item& item);
    // 表单校验提示（由主界面用 toast 展示，避免模态框关闭 popup）
    void hintRequested(const QString& msg);

private slots:
    void onBrowse();
    void onSave();
    void onTypeChanged(int index);

private:
    QLineEdit* m_kw1 = nullptr;
    QLineEdit* m_kw2 = nullptr;
    QLineEdit* m_kw3 = nullptr;
    QPlainTextEdit* m_content = nullptr;
    QComboBox* m_type = nullptr;
    QComboBox* m_levelPriority = nullptr;   // 级 1/2/3
    QComboBox* m_displayPriority = nullptr; // 显示优先级 1~5
    QCheckBox* m_encrypted = nullptr;
    QPushButton* m_browseBtn = nullptr;
};
