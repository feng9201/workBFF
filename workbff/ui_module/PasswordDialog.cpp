#include "ui_module/PasswordDialog.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

PasswordDialog::PasswordDialog(Mode mode, QWidget* parent)
    : QDialog(parent)
    , m_mode(mode)
{
    setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(340, mode == Setup ? 290 : 240);
    setModal(true);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 12, 12, 12);

    auto* panel = new QWidget(this);
    panel->setObjectName(QStringLiteral("pwdPanel"));
    panel->setStyleSheet(QStringLiteral(
        "#pwdPanel { background: rgba(30,30,60,0.97);"
        " border: 1px solid rgba(255,255,255,0.08); border-radius: 14px; }"
        "QLabel { background: transparent; }"
        "QLineEdit { background: rgba(255,255,255,0.06);"
        " border: 1px solid rgba(255,255,255,0.1); border-radius: 8px;"
        " padding: 8px 12px; color: #e8e8f0; }"
        "QLineEdit:focus { border-color: #6c5ce7; }"
        "QPushButton { border: none; border-radius: 8px; padding: 9px; font-weight: 500; }"
        "QPushButton#okBtn { background: #6c5ce7; color: white; }"
        "QPushButton#okBtn:hover { background: #7d6ff0; }"
        "QPushButton#cancelBtn { background: rgba(255,255,255,0.06); color: #b0b0c8; }"));

    auto* lay = new QVBoxLayout(panel);
    lay->setContentsMargins(28, 26, 28, 22);
    lay->setSpacing(12);

    auto* title = new QLabel(mode == Setup ? QStringLiteral("🔐 设置密码")
                                           : QStringLiteral("🔐 登录 WorkBFF"), panel);
    title->setStyleSheet(QStringLiteral("font-size: 15px; color: #e0e0f0;"));
    lay->addWidget(title);

    m_status = new QLabel(mode == Setup ? QStringLiteral("首次使用，请设置访问密码")
                                        : QStringLiteral("输入密码查看加密内容"), panel);
    m_status->setStyleSheet(QStringLiteral("font-size: 12px; color: #6a6a82;"));
    lay->addWidget(m_status);

    m_pwd = new QLineEdit(panel);
    m_pwd->setEchoMode(QLineEdit::Password);
    m_pwd->setPlaceholderText(mode == Setup ? QStringLiteral("设置密码（至少 4 位）")
                                            : QStringLiteral("请输入密码"));
    lay->addWidget(m_pwd);

    if (mode == Setup) {
        m_pwd2 = new QLineEdit(panel);
        m_pwd2->setEchoMode(QLineEdit::Password);
        m_pwd2->setPlaceholderText(QStringLiteral("再次输入确认"));
        lay->addWidget(m_pwd2);
    }

    auto* btns = new QHBoxLayout;
    btns->setSpacing(10);
    m_okBtn = new QPushButton(mode == Setup ? QStringLiteral("确定")
                                            : QStringLiteral("登录"), panel);
    m_okBtn->setObjectName(QStringLiteral("okBtn"));
    auto* cancelBtn = new QPushButton(QStringLiteral("取消"), panel);
    cancelBtn->setObjectName(QStringLiteral("cancelBtn"));
    btns->addWidget(m_okBtn);
    btns->addWidget(cancelBtn);
    lay->addLayout(btns);

    outer->addWidget(panel);

    connect(m_okBtn, &QPushButton::clicked, this, &PasswordDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
}

QString PasswordDialog::password() const
{
    return m_pwd->text();
}

void PasswordDialog::setStatus(const QString& text, bool error)
{
    m_status->setText(text);
    m_status->setStyleSheet(error ? QStringLiteral("font-size: 12px; color: #ff6b6b;")
                                  : QStringLiteral("font-size: 12px; color: #6a6a82;"));
}

void PasswordDialog::accept()
{
    const QString pwd = m_pwd->text();
    if (m_mode == Setup) {
        if (pwd.size() < 4) {
            setStatus(QStringLiteral("密码至少 4 位"), true);
            return;
        }
        if (pwd != m_pwd2->text()) {
            setStatus(QStringLiteral("两次输入的密码不一致"), true);
            return;
        }
    } else {
        if (pwd.isEmpty()) {
            setStatus(QStringLiteral("请输入密码"), true);
            return;
        }
    }
    QDialog::accept();
}
