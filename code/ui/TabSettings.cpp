#include "TabSettings.h"
#include "../downloader/DownloadEngine.h"

#include <api/Logger.h>
#include <api/Config.h>
#include <api/Ffmpeg.h>
#include <api/HttpClient.h>
#include <api/Crypto.h>

#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QMessageBox>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>

namespace bili::ui {

using bili::api::Config;
using bili::api::Logger;
namespace model = bili::model;

TabSettings::TabSettings(QWidget *parent) : QWidget(parent) {
    setupUi();
    loadSettings();
}

void TabSettings::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // ---- 下载设置 ----
    QGroupBox *dlBox = new QGroupBox("下载设置", this);
    QFormLayout *dlForm = new QFormLayout(dlBox);
    QHBoxLayout *pathLayout = new QHBoxLayout();
    m_savePathEdit = new QLineEdit(this);
    QPushButton *browseBtn = new QPushButton("浏览...", this);
    pathLayout->addWidget(m_savePathEdit);
    pathLayout->addWidget(browseBtn);
    dlForm->addRow("下载目录:", pathLayout);
    connect(browseBtn, &QPushButton::clicked, this, &TabSettings::onBrowseSavePath);

    m_formatCombo = new QComboBox(this);
    m_formatCombo->addItem("MP4 (DASH 音视频分流)", model::Format_MP4_DASH);
    m_formatCombo->addItem("FLV", model::Format_FLV);
    m_formatCombo->addItem("仅音频", model::Format_AudioOnly);
    dlForm->addRow("默认下载格式:", m_formatCombo);

    m_qualityCombo = new QComboBox(this);
    for (int qn : QList<int>{127, 120, 116, 112, 80, 74, 64, 32, 16})
        m_qualityCombo->addItem(model::videoQualityText(qn), qn);
    dlForm->addRow("默认清晰度:", m_qualityCombo);

    m_concurrentSpin = new QSpinBox(this);
    m_concurrentSpin->setRange(1, 8);
    m_concurrentSpin->setValue(2);
    dlForm->addRow("同时下载任务数:", m_concurrentSpin);

    m_threadsSpin = new QSpinBox(this);
    m_threadsSpin->setRange(1, 16);
    m_threadsSpin->setValue(4);
    m_threadsSpin->setToolTip("预留: 单任务内多线程分片下载线程数");
    dlForm->addRow("单任务下载线程数:", m_threadsSpin);

    m_checkDashUrlCheck = new QCheckBox("下载前检测 DASH 链接连通性 (自动切换备用链接)", this);
    dlForm->addRow(m_checkDashUrlCheck);
    mainLayout->addWidget(dlBox);

    // ---- 转码设置 ----
    QGroupBox *ffBox = new QGroupBox("合并/转码 (ffmpeg)", this);
    QFormLayout *ffForm = new QFormLayout(ffBox);
    QHBoxLayout *ffLayout = new QHBoxLayout();
    m_ffmpegEdit = new QLineEdit(this);
    m_ffmpegEdit->setPlaceholderText("ffmpeg (留空则自动探测: 程序目录/bin/ffmpeg.exe)");
    QPushButton *ffBrowseBtn = new QPushButton("浏览...", this);
    ffLayout->addWidget(m_ffmpegEdit);
    ffLayout->addWidget(ffBrowseBtn);
    ffForm->addRow("ffmpeg 路径:", ffLayout);
    connect(ffBrowseBtn, &QPushButton::clicked, this, &TabSettings::onBrowseFfmpeg);

    m_ffmpegStatus = new QLabel(this);
    ffForm->addRow("状态:", m_ffmpegStatus);
    m_testFfmpegBtn = new QPushButton("测试 ffmpeg", this);
    ffForm->addRow(m_testFfmpegBtn);
    connect(m_testFfmpegBtn, &QPushButton::clicked, this, &TabSettings::onTestFfmpeg);
    mainLayout->addWidget(ffBox);

    // ---- 账户 ----
    QGroupBox *accBox = new QGroupBox("账户 (Cookie)", this);
    QVBoxLayout *accLayout = new QVBoxLayout(accBox);
    accLayout->addWidget(new QLabel("登录后凭据 (SESSDATA 等), 也可通过 账户->扫码登录 自动获取:", this));
    m_cookieEdit = new QPlainTextEdit(this);
    m_cookieEdit->setMaximumHeight(80);
    m_cookieEdit->setPlaceholderText("SESSDATA=xxx; bili_jct=xxx; DedeUserID=xxx; ...");
    accLayout->addWidget(m_cookieEdit);
    m_cookieStatus = new QLabel(this);
    accLayout->addWidget(m_cookieStatus);
    connect(m_cookieEdit, &QPlainTextEdit::textChanged, this, &TabSettings::onCookieChanged);
    mainLayout->addWidget(accBox);

    // ---- 界面与通知 (任务栏托盘) ----
    QGroupBox *uiBox = new QGroupBox("界面与通知 (任务栏托盘)", this);
    QVBoxLayout *uiLayout = new QVBoxLayout(uiBox);
    m_minTrayCheck = new QCheckBox("关闭窗口时最小化到任务栏托盘 (下载不中断, 双击托盘图标恢复)", this);
    uiLayout->addWidget(m_minTrayCheck);
    m_notifyCheck = new QCheckBox("下载任务完成/失败时弹出气泡通知", this);
    uiLayout->addWidget(m_notifyCheck);
    mainLayout->addWidget(uiBox);

    // ---- 保存按钮 ----
    QHBoxLayout *btnLayout = new QHBoxLayout();
    btnLayout->addStretch(1);
    QPushButton *saveBtn = new QPushButton("保存设置", this);
    saveBtn->setMinimumWidth(120);
    btnLayout->addWidget(saveBtn);
    connect(saveBtn, &QPushButton::clicked, this, &TabSettings::onSaveClicked);
    mainLayout->addLayout(btnLayout);

    mainLayout->addStretch(1);
}

void TabSettings::loadSettings() {
    Config &cfg = Config::instance();
    m_savePathEdit->setText(cfg.savePath());
    int fmt = cfg.downloadFormat();
    m_formatCombo->setCurrentIndex(m_formatCombo->findData(fmt));
    int qn = cfg.defaultQuality();
    m_qualityCombo->setCurrentIndex(m_qualityCombo->findData(qn));
    m_threadsSpin->setValue(cfg.maxDownloadThreads());
    m_checkDashUrlCheck->setChecked(cfg.checkDashUrl());
    m_ffmpegEdit->setText(cfg.ffmpegPath() == "ffmpeg" ? QString() : cfg.ffmpegPath());
    m_cookieEdit->setPlainText(cfg.cookies());
    m_concurrentSpin->setValue(bili::downloader::DownloadEngine::instance().maxConcurrent());
    m_minTrayCheck->setChecked(cfg.minimizeToTray());
    m_notifyCheck->setChecked(cfg.notifyOnFinish());
    onCookieChanged();
}

void TabSettings::saveSettings() {
    Config &cfg = Config::instance();
    cfg.setSavePath(m_savePathEdit->text().trimmed());
    cfg.setDownloadFormat(m_formatCombo->currentData().toInt());
    cfg.setDefaultQuality(m_qualityCombo->currentData().toInt());
    cfg.setMaxDownloadThreads(m_threadsSpin->value());
    cfg.setCheckDashUrl(m_checkDashUrlCheck->isChecked());
    QString ff = m_ffmpegEdit->text().trimmed();
    cfg.setFfmpegPath(ff.isEmpty() ? "ffmpeg" : ff);
    QString cookie = m_cookieEdit->toPlainText().trimmed();
    cfg.setCookies(cookie);
    bili::api::HttpClient::setGlobalCookie(cookie);
    bili::downloader::DownloadEngine::instance().setMaxConcurrent(m_concurrentSpin->value());
    cfg.setMinimizeToTray(m_minTrayCheck->isChecked());
    cfg.setNotifyOnFinish(m_notifyCheck->isChecked());
    cfg.save();
    Logger::info("设置已保存");
}

void TabSettings::onSaveClicked() {
    saveSettings();
    QMessageBox::information(this, "提示", "设置已保存");
}

void TabSettings::onBrowseSavePath() {
    QString dir = QFileDialog::getExistingDirectory(this, "选择下载目录",
                                                    m_savePathEdit->text());
    if (!dir.isEmpty())
        m_savePathEdit->setText(dir);
}

void TabSettings::onBrowseFfmpeg() {
    QString file = QFileDialog::getOpenFileName(this, "选择 ffmpeg 可执行文件",
                                                QString(), "ffmpeg (ffmpeg.exe);;所有文件 (*)");
    if (!file.isEmpty())
        m_ffmpegEdit->setText(file);
}

void TabSettings::onTestFfmpeg() {
    bili::api::Ffmpeg &ff = bili::api::Ffmpeg::instance();
    QString custom = m_ffmpegEdit->text().trimmed();
    if (!custom.isEmpty())
        ff.setExecutable(custom);
    else
        ff.autoDetect();

    QString version;
    bool ok = ff.available(&version);
    if (ok) {
        m_ffmpegStatus->setText("可用: " + version.left(60));
        m_ffmpegStatus->setStyleSheet("color:green;");
        Logger::info("ffmpeg 测试通过: " + version);
    } else {
        m_ffmpegStatus->setText("不可用: " + version);
        m_ffmpegStatus->setStyleSheet("color:red;");
        QMessageBox::warning(this, "提示",
                             "ffmpeg 不可用。\n请将 ffmpeg.exe 放入程序目录 bin/ 下, 或在设置中指定路径。\n\n"
                             "下载功能可以正常使用, 但音视频合并/合集合并将无法完成。");
    }
}

void TabSettings::onCookieChanged() {
    QString cookie = m_cookieEdit->toPlainText().trimmed();
    if (cookie.isEmpty()) {
        m_cookieStatus->setText("未登录: 部分高清视频需要登录后才能下载");
        m_cookieStatus->setStyleSheet("color:gray;");
        return;
    }
    bool hasSessdata = cookie.contains("SESSDATA");
    m_cookieStatus->setText(hasSessdata ? "Cookie 已配置 (含 SESSDATA)" : "Cookie 已配置 (不含 SESSDATA, 可能未登录)");
    m_cookieStatus->setStyleSheet(hasSessdata ? "color:green;" : "color:orange;");
}

} // namespace bili::ui
