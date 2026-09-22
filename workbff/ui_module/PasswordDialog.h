#pragma once

#include <QDialog>

class QLabel;
class QLineEdit;
class QPushButton;

// 密码对话框：登录模式 / 首次设置模式（无边框、深色样式）
class PasswordDialog : public QDialog
{
    Q_OBJECT
public:
    enum Mode { Login, Setup };

    explicit PasswordDialog(Mode mode, QWidget* parent = nullptr);

    QString password() const;
    void setStatus(const QString& text, bool error = false);

protected:
    void accept() override;

private:
    Mode m_mode;
    QLineEdit* m_pwd = nullptr;
    QLineEdit* m_pwd2 = nullptr; // Setup 模式的确认密码
    QLabel* m_status = nullptr;
    QPushButton* m_okBtn = nullptr;
};
