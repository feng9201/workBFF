#include <QApplication>
#include <QThread>
#include <QDir>
#include <QUrl>
#include <QUrlQuery>
#include <QDebug>
#include <iostream>
#include <Windows.h>

#include "spdlog/spdlog.h"
#include "spdlog/sinks/basic_file_sink.h"
#include "spdlog/sinks/stdout_color_sinks.h"
#include "spdlog/sinks/daily_file_sink.h"
#include "spdlog/sinks/rotating_file_sink.h"

#include "Version.h"
#include "stdafx.h"
#include "mtPool/MtPool.h"
#include "base/deepLink.h"
#include "db/DatabaseManager.h"
#include "ui_module/DockIcon.h"
#include "ui_module/mainUI.h"

#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QScreen>
#include <QSystemTrayIcon>


// workbff://action?start=1 唤起进程
// 处理 scheme URL 注入的参数，例如: workbff://play?ip=172.16.0.1&channel=1
void handleDeepLink(const QString& url)
{
    const QUrl parsed(url);
    const QString action = parsed.host();   // scheme:// 后的第一段作为动作名
    const QUrlQuery query(parsed);

    LOG_INFO("[deeplink] url=%s action=%s", url.toStdString().c_str(), action.toStdString().c_str());
    for (const auto& item : query.queryItems()) {
        LOG_INFO("[deeplink] param: %s=%s",
                 item.first.toStdString().c_str(), item.second.toStdString().c_str());
        if(item.first == "log_level"){
            auto level = item.second.toInt();
            if(level == 1){
                w_log_cpp::setFileLogLevel(w_log_cpp::LOG_LEVEL_E::DEBUG);
            }
            else if(level == 2){
                w_log_cpp::setFileLogLevel(w_log_cpp::LOG_LEVEL_E::TRACE);
            }
            else{
                w_log_cpp::setFileLogLevel(w_log_cpp::LOG_LEVEL_E::INFO);
            }
        }
    }
}

std::vector<std::wstring> getCommandLineArgs()
{
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return {};

    std::vector<std::wstring> args;
    for (int i = 1; i < argc; ++i) {
        args.push_back(argv[i]);
    }
    LocalFree(argv);
    return args;
}

void outputMessage(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    switch (type)
    {
    case QtDebugMsg:
        LOG_DEBUG("%s",msg.toStdString().c_str());
        break;
    case QtWarningMsg:
        LOG_WARN("%s",msg.toStdString().c_str());
        break;
    case QtCriticalMsg:
        LOG_DEBUG("%s",msg.toStdString().c_str());
        break;
    case QtFatalMsg:
        LOG_ERR("%s",msg.toStdString().c_str());
        break;
    case QtInfoMsg:
        LOG_INFO("%s",msg.toStdString().c_str());
        break;
    default:
        break;
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling, true);
    QCoreApplication::setAttribute(Qt::AA_UseHighDpiPixmaps, true);
    QApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc, argv);

    // 注意：scheme 名必须符合 RFC 3986（字母开头，仅含字母/数字/+/-/.），
    // 下划线不合法，Windows 外壳和浏览器会拒绝解析，导致无法唤起进程
    const std::wstring wsScheme = L"workbff";

    // ── scheme 协议：注册 + 单实例参数转发 ──
    deeplink::DeepLink<> deepLink(wsScheme);
    try {
        if (!deepLink.isSchemeRegistered()) {   // 已注册且指向当前 exe 则跳过，防止重复注册
            deepLink.registerScheme();
        }
    }
    catch (const std::exception& e) {
        qWarning() << "[deeplink] register failed:" << e.what();
    }

    deepLink.setOnMessage([](const std::string& url) {
        const QString qurl = QString::fromStdString(url);
        // 回调发生在管道线程（或事件循环启动前），统一排队到主线程处理
        QMetaObject::invokeMethod(qApp, [qurl]() { handleDeepLink(qurl); }, Qt::QueuedConnection);
    });

    // 已有实例运行时，URL 经命名管道转发给旧实例，本进程直接退出
    if (!deepLink.runOrForward(getCommandLineArgs())) {
        return 0;
    }

    // 设置日志
    QString logDir = qApp->applicationDirPath() + "/logs/";
    QDir().mkpath(logDir);
    QString logPath = logDir + "workbff.log";
    auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
        logPath.toLocal8Bit().data(), 1024 * 1024 * 5, 3);
    w_log_cpp::initLog({ file_sink });
    w_log_cpp::setLogLevel(w_log_cpp::LOG_LEVEL_E::TRACE);
    w_log_cpp::setFlushOn(w_log_cpp::LOG_LEVEL_E::TRACE); // 所有级别实时落盘，便于诊断
    w_log_cpp::setFileLogLevel(w_log_cpp::LOG_LEVEL_E::DEBUG);

    LOG_INFO("******************WORKBFF START******************");
    LOG_INFO("WORKBFF_VERSION: %s", WORKBFF_VERSION);
    qInstallMessageHandler(outputMessage);
    // 初始化全局线程池（供网络请求等后台任务使用）
    auto& pool = mtPool::pool();
    LOG_INFO("thread_pool initialized, threads=%d", static_cast<int>(pool.get_thread_count()));

    // 初始化本地数据库
    if (!DatabaseManager::instance().init()) {
        LOG_ERR("database init failed");
        return -2;
    }

    app.setWindowIcon(QIcon(QStringLiteral(":/app.ico")));

    // Dock 图标（圆形跑马灯，可拖拽）+ 主界面（默认隐藏，点 Dock 开关）
    DockIcon dock;
    MainUI mainUI;
    mainUI.hide();

    QObject::connect(&dock, &DockIcon::clicked, [&]() {
        // 先记录状态再取消待隐藏：若刚点击过外部（有待隐藏），点 Dock 应显示而不是再隐藏
        const bool wasVisible = mainUI.isVisible();
        const bool hadPendingHide = mainUI.hasPendingHide();
        mainUI.cancelPendingHide();
        if (wasVisible && !hadPendingHide)
            mainUI.hide();
        else
            mainUI.showCentered();
    });

    // 启动时停靠在屏幕右上角（距顶/右各 80px）
    const QRect sg = QGuiApplication::primaryScreen()->availableGeometry();
    dock.move(sg.right() - dock.width() - 80, sg.top() + 80);
    dock.show();
    LOG_INFO("workBFF UI started");

    // 系统托盘（右下角）：右键菜单含“退出”
    QMenu trayMenu;
    QAction* showAct = trayMenu.addAction(QStringLiteral("打开主界面"));
    trayMenu.addSeparator();
    QAction* quitAct = trayMenu.addAction(QStringLiteral("退出"));
    QObject::connect(showAct, &QAction::triggered, &mainUI, &MainUI::showCentered);
    QObject::connect(quitAct, &QAction::triggered, qApp, &QCoreApplication::quit);

    QSystemTrayIcon tray(QIcon(QStringLiteral(":/app.ico")));
    tray.setToolTip(QStringLiteral("WorkBFF"));
    tray.setContextMenu(&trayMenu);
    QObject::connect(&tray, &QSystemTrayIcon::activated, [&](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) { // 左键单击：开关主界面
            const bool wasVisible = mainUI.isVisible();
            const bool hadPendingHide = mainUI.hasPendingHide();
            mainUI.cancelPendingHide();
            if (wasVisible && !hadPendingHide)
                mainUI.hide();
            else
                mainUI.showCentered();
        }
    });
    tray.show();

    int ret = app.exec();
    QThread::msleep(100); // 等待子进程退出
    return ret;
}
