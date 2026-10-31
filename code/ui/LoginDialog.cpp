#include "LoginDialog.h"
#include "../util/LoginClient.h"

#include <api/Logger.h>
#include <api/HttpClient.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>

namespace bili::ui {

using bili::api::Logger;
using bili::api::HttpClient;

LoginDialog::LoginDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle("扫码登录 - 哔哩哔哩");
    setFixedSize(360, 460);
    setWindowIcon(QIcon(":/icon.png"));

    QVBoxLayout *layout = new QVBoxLayout(this);

    m_qrLabel = new QLabel(this);
    m_qrLabel->setFixedSize(240, 240);
    m_qrLabel->setAlignment(Qt::AlignCenter);
    m_qrLabel->setStyleSheet("border: 1px solid #ccc; background: white;");
    m_qrLabel->setText("正在生成二维码...");
    layout->addWidget(m_qrLabel, 0, Qt::AlignCenter);

    m_statusLabel = new QLabel("请使用哔哩哔哩客户端扫码", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_statusLabel);

    QHBoxLayout *btnLayout = new QHBoxLayout();
    m_refreshBtn = new QPushButton("刷新二维码", this);
    m_cancelBtn = new QPushButton("取消", this);
    btnLayout->addWidget(m_refreshBtn);
    btnLayout->addWidget(m_cancelBtn);
    layout->addLayout(btnLayout);

    connect(m_refreshBtn, &QPushButton::clicked, this, &LoginDialog::onRefreshClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &LoginDialog::reject);

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(2000);
    connect(m_pollTimer, &QTimer::timeout, this, &LoginDialog::onPollTimeout);

    startLogin();
}

LoginDialog::~LoginDialog() {
    stopPolling();
}

void LoginDialog::startLogin() {
    stopPolling();
    m_cookie.clear();
    m_userName.clear();
    m_pollCount = 0;
    m_statusLabel->setText("正在生成二维码...");
    m_qrLabel->setText("...");

    bili::util::LoginClient client;
    QString key, err;
    QImage img = client.generateQr(key, err);
    if (img.isNull()) {
        m_statusLabel->setText("二维码生成失败: " + err);
        return;
    }
    m_qrcodeKey = key;
    m_qrLabel->setPixmap(QPixmap::fromImage(img).scaled(
        m_qrLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    m_statusLabel->setText("请使用哔哩哔哩客户端扫码");
    m_pollTimer->start();
    Logger::info("登录对话框: 二维码已显示, 开始轮询");
}

void LoginDialog::stopPolling() {
    if (m_pollTimer)
        m_pollTimer->stop();
}

void LoginDialog::onRefreshClicked() {
    startLogin();
}

void LoginDialog::onPollTimeout() {
    if (m_qrcodeKey.isEmpty())
        return;
    // 二维码有效期约 3 分钟, 轮询超时自动刷新
    if (++m_pollCount > 90) {
        m_statusLabel->setText("二维码已过期, 正在刷新...");
        startLogin();
        return;
    }

    bili::util::LoginClient client;
    QString cookie, message;
    auto status = client.poll(m_qrcodeKey, cookie, message);
    switch (status) {
        case bili::util::LoginClient::PollStatus::WaitScan:
            m_statusLabel->setText("请使用哔哩哔哩客户端扫码");
            break;
        case bili::util::LoginClient::PollStatus::WaitConfirm:
            m_statusLabel->setText("已扫描, 请在手机上确认登录");
            break;
        case bili::util::LoginClient::PollStatus::Expired:
            m_statusLabel->setText("二维码已过期, 正在刷新...");
            startLogin();
            return;
        case bili::util::LoginClient::PollStatus::Success: {
            m_cookie = cookie;
            stopPolling();
            m_statusLabel->setText("登录成功!");
            Logger::info("扫码登录成功");
            // 获取用户名
            onFetchUserInfo();
            QTimer::singleShot(800, this, &QDialog::accept);
            return;
        }
        case bili::util::LoginClient::PollStatus::Failed:
            m_statusLabel->setText("登录失败: " + message);
            stopPolling();
            return;
    }
}

void LoginDialog::onFetchUserInfo() {
    if (m_cookie.isEmpty())
        return;
    HttpClient &http = HttpClient::instance();
    api::Headers h = http.jsonApiHeaders();
    h["Cookie"] = m_cookie;
    auto r = http.getSync("https://api.bilibili.com/x/web-interface/nav", h);
    if (!r.ok)
        return;
    QJsonObject obj = QJsonDocument::fromJson(r.body).object();
    QJsonObject data = obj.value("data").toObject();
    bool isLogin = data.value("isLogin").toBool();
    if (isLogin) {
        m_userName = data.value("uname").toString();
        m_statusLabel->setText("登录成功: " + m_userName);
    }
}

} // namespace bili::ui
