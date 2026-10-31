// BilibiliDown - C++23 / Qt6 重写版
// 入口: 初始化配置/日志 -> 启动主窗口
#include <QApplication>
#include <QDir>
#include <QStyleFactory>

#include <api/Logger.h>
#include <api/Config.h>
#include <api/HttpClient.h>
#include <code/ui/MainWindow.h>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("BilibiliDown");
    QApplication::setApplicationVersion("1.0.0");
    QApplication::setOrganizationName("NiceLee");

#ifdef Q_OS_WIN
    QApplication::setStyle("Fusion");
#endif

    // 配置与日志 (api 层)
    bili::api::Config::instance().load();
    bili::api::Logger::instance().init();

    // 恢复登录 Cookie
    QString cookie = bili::api::Config::instance().cookies();
    if (!cookie.isEmpty())
        bili::api::HttpClient::setGlobalCookie(cookie);

    bili::api::Logger::info("==========================================");
    bili::api::Logger::info(QString("BilibiliDown v%1 启动 (C++23 / Qt %2)")
                     .arg(QApplication::applicationVersion(), qVersion()));
    bili::api::Logger::info("==========================================");

    bili::ui::MainWindow w;
    w.show();
    return app.exec();
}
