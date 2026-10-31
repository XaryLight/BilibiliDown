#ifndef BILI_CODE_UI_MAIN_WINDOW_H
#define BILI_CODE_UI_MAIN_WINDOW_H

#include "model/Types.h"
#include "downloader/DownloadEngine.h"

#include <QMainWindow>
#include <QTabWidget>
#include <QLabel>
#include <QTimer>
#include <QPlainTextEdit>
#include <QSystemTrayIcon>

namespace bili::ui {

class TabParse;
class TabDownload;
class TabSettings;
class LoginDialog;

/**
 * @brief 主窗口: 菜单栏 + 标签页 (解析/下载/设置) + 日志停靠栏 + 状态栏
 *        另含任务栏托盘图标 (最小化到托盘 / 气泡通知)
 */
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void showLoginDialog();
    void showAbout();
    void openSaveFolder();
    void updateStatusInfo();
    void onLoginStateChanged(bool logged, const QString &userName);

    // ---- 托盘 ----
    void onTrayActivated(QSystemTrayIcon::ActivationReason reason);
    void onTrayTaskFinished(int taskId, bool success, const QString &error);
    void hideToTray();
    void toggleMainWindow();
    void quitApp();

private:
    void setupMenu();
    void setupStatusBar();
    void setupLogDock();
    void setupTray();
    void updateTrayStatus(double totalSpeed, int activeTasks);

    QTabWidget *m_tabs = nullptr;
    TabParse *m_tabParse = nullptr;
    TabDownload *m_tabDownload = nullptr;
    TabSettings *m_tabSettings = nullptr;

    QLabel *m_statusLogin = nullptr;
    QLabel *m_statusCookie = nullptr;
    QLabel *m_statusSpeed = nullptr;
    QPlainTextEdit *m_logView = nullptr;
    QTimer *m_statusTimer = nullptr;

    // 任务栏托盘
    QSystemTrayIcon *m_tray = nullptr;
    QMenu *m_trayMenu = nullptr;
    bool m_forceQuit = false; // 托盘菜单"退出"时绕过最小化到托盘

    QString m_userName;
};

} // namespace bili::ui

#endif // BILI_CODE_UI_MAIN_WINDOW_H
