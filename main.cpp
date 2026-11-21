// BilibiliDown - C++23 / Qt6 重写版
// 入口: 初始化配置/日志 -> 启动主窗口
#include <QApplication>
#include <QDir>
#include <QStyleFactory>

#include <api/Logger.h>
#include <api/Config.h>
#include <api/HttpClient.h>
#include <code/downloader/DownloadEngine.h>
#include <code/ui/MainWindow.h>
#include <code/ui/ModernStyle.h>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("BilibiliDown");
    QApplication::setApplicationVersion("1.0.0");
    QApplication::setOrganizationName("NiceLee");

#ifdef Q_OS_WIN
    QApplication::setStyle("Fusion");
#endif
    // 现代化外观 (圆角/配色/交互反馈)
    bili::ui::ModernStyle::applyGlobalStyle();

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

    // 退出完全由应用自身控制: 关闭窗口可最小化到托盘, 只有托盘"退出"才真正结束进程
    // (避免 hide() 后 Qt 因 quitOnLastWindowClosed 自动退出, 或托盘退出后进程残留)
    QApplication::setQuitOnLastWindowClosed(false);

    bili::ui::MainWindow w;
    w.show();

    // 恢复上次未完成的下载任务 (暂停状态, 等待用户在下载列表确认继续)
    bili::downloader::DownloadEngine::instance().loadTasksFromDisk();

    int code = app.exec();

    // 正常退出: 保存未完成任务 + 等待下载线程收尾, 避免进程残留
    bili::downloader::DownloadEngine::instance().saveTasksToDisk();
    bili::downloader::DownloadEngine::instance().stopAll();
    return code;
}
