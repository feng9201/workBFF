#pragma once

#include <QWidget>

#include "model/Item.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

// 添加界面：最多3个关键词 + 内容(多行) + 类型 + 是否加密 + 全局快捷键
// 快捷键与加密互斥（加密条目未登录时解不出明文，绑了也没用）
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

protected:
    // 录制框吃掉按键：在 m_hotkeyEdit 上监听 KeyPress，直接转成 "CTRL+W" 串
    bool eventFilter(QObject* watched, QEvent* event) override;

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

    QCheckBox* m_hotkeyEnabled = nullptr; // 是否支持快捷键
    QWidget* m_hotkeyWrap = nullptr;      // 录制卡片，勾上才显示
    QLineEdit* m_hotkeyEdit = nullptr;    // 只读录制框，显示 Ctrl+W 这种形式
    QPushButton* m_clearHotkeyBtn = nullptr; // 清除已录制的快捷键
    QString m_hotkeyValue;                // 规范化串，如 "CTRL+ALT+W"
};
