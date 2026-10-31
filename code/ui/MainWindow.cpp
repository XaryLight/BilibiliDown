#include "MainWindow.h"
#include "TabParse.h"
#include "TabDownload.h"
#include "TabSettings.h"
#include "LoginDialog.h"

#include <api/Logger.h>
#include <api/Config.h>

#include <QVBoxLayout>
#include <QMenuBar>
#include <QMenu>
#include <QStatusBar>
#include <QDockWidget>
#include <QPlainTextEdit>
#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QTextCursor>
#include <QDir>
#include <QTabWidget>
#include <QLabel>
#include <QSystemTrayIcon>
#include <QApplication>

namespace bili::ui {

using bili::api::Logger;
using bili::api::Config;

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("BilibiliDown - B站视频下载器 (C++/Qt 重写版)");
    resize(1100, 720);
    setWindowIcon(QIcon(":/icon.png"));

    // ---- 标签页 ----
    m_tabs = new QTabWidget(this);
    m_tabParse = new TabParse(this);
    m_tabDownload = new TabDownload(this);
    m_tabSettings = new TabSettings(this);
    m_tabs->addTab(m_tabParse, "解析下载");
    m_tabs->addTab(m_tabDownload, "下载列表");
    m_tabs->addTab(m_tabSettings, "设置");
    setCentralWidget(m_tabs);

    setupMenu();
    setupStatusBar();
    setupLogDock();
    setupTray();

    // ---- 日志信号 ----
    connect(&Logger::instance(), &bili::api::Logger::logAppended,
            this, [this](const QString &line, int level) {
                if (!m_logView) return;
                // 过滤 DEBUG 输出, 避免刷屏
                if (level == 0) return;
                m_logView->appendPlainText(line);
                // 限制行数
                if (m_logView->blockCount() > 2000) {
                    QTextCursor cursor = m_logView->textCursor();
                    cursor.movePosition(QTextCursor::Start);
                    cursor.select(QTextCursor::LineUnderCursor);
                    cursor.removeSelectedText();
                    cursor.deleteChar();
                }
            });

    // ---- 状态定时刷新 ----
    m_statusTimer = new QTimer(this);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::updateStatusInfo);
    m_statusTimer->start(1000);

    // ---- 登录状态 ----
    connect(m_tabParse, &TabParse::loginStateChanged,
            this, &MainWindow::onLoginStateChanged);

    QString cookie = Config::instance().cookies();
    m_statusCookie->setText(cookie.isEmpty() ? "未登录" : "Cookie 已加载");
}

MainWindow::~MainWindow() = default;

void MainWindow::setupMenu() {
    // 文件菜单
    QMenu *fileMenu = menuBar()->addMenu("文件");
    QAction *actOpenFolder = fileMenu->addAction("打开下载目录");
    connect(actOpenFolder, &QAction::triggered, this, &MainWindow::openSaveFolder);
    QAction *actHideTray = fileMenu->addAction("最小化到任务栏托盘");
    connect(actHideTray, &QAction::triggered, this, &MainWindow::hideToTray);
    fileMenu->addSeparator();
    QAction *actQuit = fileMenu->addAction("退出");
    connect(actQuit, &QAction::triggered, this, &QWidget::close);

    // ---- 账户菜单 ----
    QMenu *accountMenu = menuBar()->addMenu("账户");
    QAction *actLogin = accountMenu->addAction("扫码登录");
    connect(actLogin, &QAction::triggered, this, &MainWindow::showLoginDialog);
    QAction *actLogout = accountMenu->addAction("退出登录");
    connect(actLogout, &QAction::triggered, this, [this]() {
        Config::instance().setCookies(QString());
        bili::api::HttpClient::setGlobalCookie(QString());
        m_statusCookie->setText("未登录");
        m_statusLogin->setText("未登录");
        m_userName.clear();
        QMessageBox::information(this, "提示", "已退出登录");
    });

    // 帮助菜单
    QMenu *helpMenu = menuBar()->addMenu("帮助");
    QAction *actAbout = helpMenu->addAction("关于");
    connect(actAbout, &QAction::triggered, this, &MainWindow::showAbout);
}

void MainWindow::setupStatusBar() {
    m_statusLogin = new QLabel("未登录", this);
    m_statusCookie = new QLabel("未登录", this);
    m_statusSpeed = new QLabel("", this);
    statusBar()->addWidget(new QLabel("账户:", this));
    statusBar()->addWidget(m_statusLogin);
    statusBar()->addWidget(new QLabel("  下载速度:", this));
    statusBar()->addWidget(m_statusSpeed);
    statusBar()->addPermanentWidget(m_statusCookie);
}

void MainWindow::setupLogDock() {
    QDockWidget *dock = new QDockWidget("运行日志", this);
    dock->setAllowedAreas(Qt::BottomDockWidgetArea | Qt::RightDockWidgetArea);
    m_logView = new QPlainTextEdit(dock);
    m_logView->setReadOnly(true);
    m_logView->setMaximumBlockCount(2000);
    m_logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont monoFont("Consolas", 9);
    monoFont.setStyleHint(QFont::Monospace);
    m_logView->setFont(monoFont);
    dock->setWidget(m_logView);
    addDockWidget(Qt::BottomDockWidgetArea, dock);
}

// ============================================================
//  任务栏托盘图标
// ============================================================
void MainWindow::setupTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        Logger::warn("当前系统不支持任务栏托盘图标, 托盘功能不可用");
        return;
    }
    m_tray = new QSystemTrayIcon(QIcon(":/icon.ico"), this);
    m_tray->setToolTip("BilibiliDown - B站视频下载器");

    m_trayMenu = new QMenu(this);
    QAction *actShow = m_trayMenu->addAction("显示/隐藏主窗口");
    connect(actShow, &QAction::triggered, this, &MainWindow::toggleMainWindow);
    m_trayMenu->addSeparator();
    QAction *actStartAll = m_trayMenu->addAction("全部开始");
    connect(actStartAll, &QAction::triggered, this, [this]() {
        bili::downloader::DownloadEngine::instance().startAllQueued();
    });
    QAction *actStopAll = m_trayMenu->addAction("全部停止");
    connect(actStopAll, &QAction::triggered, this, [this]() {
        bili::downloader::DownloadEngine::instance().stopAll();
    });
    m_trayMenu->addSeparator();
    QAction *actQuit = m_trayMenu->addAction("退出");
    connect(actQuit, &QAction::triggered, this, &MainWindow::quitApp);
    m_tray->setContextMenu(m_trayMenu);

    connect(m_tray, &QSystemTrayIcon::activated, this, &MainWindow::onTrayActivated);

    // 任务完成/失败 -> 气泡通知 (按设置开关)
    connect(&bili::downloader::DownloadEngine::instance(),
            &bili::downloader::DownloadEngine::taskFinished,
            this, &MainWindow::onTrayTaskFinished);

    m_tray->show();
    Logger::info("任务栏托盘图标已启用");
}

void MainWindow::onTrayActivated(QSystemTrayIcon::ActivationReason reason) {
    if (!m_tray) return;
    // 双击 / 单击: 显示或隐藏主窗口 (Trigger 含左键单击, 部分平台只有 DoubleClick)
    if (reason == QSystemTrayIcon::DoubleClick || reason == QSystemTrayIcon::Trigger)
        toggleMainWindow();
}

void MainWindow::toggleMainWindow() {
    if (isVisible() && !isMinimized()) {
        hideToTray();
    } else {
        showNormal();
        activateWindow();
        raise();
    }
}

void MainWindow::hideToTray() {
    if (!m_tray) {
        showMinimized();
        return;
    }
    hide();
    m_tray->showMessage("BilibiliDown", "程序已最小化到任务栏托盘, 双击图标可恢复主窗口",
                        QSystemTrayIcon::Information, 3000);
}

void MainWindow::quitApp() {
    m_forceQuit = true;
    close();
}

void MainWindow::onTrayTaskFinished(int taskId, bool success, const QString &error) {
    if (!m_tray || !Config::instance().notifyOnFinish())
        return;
    auto t = bili::downloader::DownloadEngine::instance().taskSnapshot(taskId);
    QString name = t.displayName();
    if (success) {
        m_tray->showMessage("下载完成", name, QSystemTrayIcon::Information, 4000);
    } else if (!error.isEmpty()) {
        m_tray->showMessage("下载失败", name + "\n" + error.left(200),
                            QSystemTrayIcon::Warning, 5000);
    }
    // 已停止的任务 (无错误信息) 不打扰
}

void MainWindow::updateTrayStatus(double totalSpeed, int activeTasks) {
    if (!m_tray) return;
    QString tip = "BilibiliDown - B站视频下载器";
    if (activeTasks > 0) {
        tip += QString("\n下载中: %1 个任务").arg(activeTasks);
        if (totalSpeed > 0) {
            if (totalSpeed >= 1024 * 1024)
                tip += QString(" · %1 MB/s").arg(totalSpeed / 1024.0 / 1024.0, 0, 'f', 2);
            else
                tip += QString(" · %1 KB/s").arg(totalSpeed / 1024.0, 0, 'f', 1);
        }
    } else {
        tip += "\n暂无进行中的下载";
    }
    m_tray->setToolTip(tip);
}

void MainWindow::updateStatusInfo() {
    // 汇总下载速度
    double totalSpeed = 0;
    int activeTasks = 0;
    for (int id : bili::downloader::DownloadEngine::instance().taskIds()) {
        auto t = bili::downloader::DownloadEngine::instance().taskSnapshot(id);
        if (t.status == model::TaskStatus::Downloading) {
            totalSpeed += t.speedBytesPerSec;
            activeTasks++;
        }
    }
    QString speedText;
    if (totalSpeed >= 1024 * 1024)
        speedText = QString::number(totalSpeed / 1024.0 / 1024.0, 'f', 2) + " MB/s";
    else if (totalSpeed >= 1024)
        speedText = QString::number(totalSpeed / 1024.0, 'f', 1) + " KB/s";
    else
        speedText = QString::number(totalSpeed, 'f', 0) + " B/s";
    m_statusSpeed->setText(speedText);
    updateTrayStatus(totalSpeed, activeTasks);
}

void MainWindow::onLoginStateChanged(bool logged, const QString &userName) {
    m_userName = userName;
    m_statusLogin->setText(logged ? userName : "未登录");
    m_statusCookie->setText(logged ? "已登录" : "未登录");
}

void MainWindow::showLoginDialog() {
    LoginDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted) {
        QString cookie = dlg.cookie();
        Config::instance().setCookies(cookie);
        bili::api::HttpClient::setGlobalCookie(cookie);
        m_statusCookie->setText("已登录");
        m_statusLogin->setText(dlg.userName());
        Logger::info("登录状态已更新: " + dlg.userName());
    }
}

void MainWindow::showAbout() {
    QMessageBox::about(this, "关于 BilibiliDown",
                       "<h3>BilibiliDown (C++23 / Qt6 重写版)</h3>"
                       "<p>基于 <a href='https://gitee.com/NiceLeee/BilibiliDown'>NiceLeee/BilibiliDown</a> "
                       "Java 版重写的 B 站视频下载器。</p>"
                       "<p>特性: 多清晰度选择、DASH 音视频分流下载、断点续传、"
                       "<b>合集合并为单个 MP4</b>、二维码登录。</p>"
                       "<p>技术栈: C++23 / Qt 6 / LLVM clang + lld / CMake (CLion)</p>"
                       "<p>本项目遵守 Apache 2.0 开源协议, 仅供学习交流使用。</p>");
}

void MainWindow::openSaveFolder() {
    QString path = Config::instance().savePath();
    QDir().mkpath(path);
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::closeEvent(QCloseEvent *event) {
    // 最小化到托盘: 关闭窗口仅隐藏 (下载继续), 从托盘菜单"退出"才真正结束
    if (!m_forceQuit && m_tray && Config::instance().minimizeToTray()) {
        hide();
        m_tray->showMessage("BilibiliDown", "程序已最小化到任务栏托盘, 双击图标可恢复主窗口",
                            QSystemTrayIcon::Information, 3000);
        event->ignore();
        return;
    }
    // 停止所有下载
    bili::downloader::DownloadEngine::instance().stopAll();
    Config::instance().save();
    if (m_tray)
        m_tray->hide();
    QMainWindow::closeEvent(event);
}

} // namespace bili::ui
