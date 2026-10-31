#ifndef BILI_CODE_UI_LOGIN_DIALOG_H
#define BILI_CODE_UI_LOGIN_DIALOG_H

#include <QDialog>
#include <QTimer>
#include <QLabel>

class QPushButton;

namespace bili::ui {

/**
 * @brief 扫码登录对话框
 *   显示二维码 -> 轮询扫码状态 -> 成功后返回 cookie
 */
class LoginDialog : public QDialog {
    Q_OBJECT
public:
    explicit LoginDialog(QWidget *parent = nullptr);
    ~LoginDialog() override;

    QString cookie() const { return m_cookie; }
    QString userName() const { return m_userName; }

private slots:
    void onRefreshClicked();
    void onPollTimeout();
    void onFetchUserInfo();

private:
    void startLogin();
    void stopPolling();

    QLabel *m_qrLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_refreshBtn = nullptr;
    QPushButton *m_cancelBtn = nullptr;

    QTimer *m_pollTimer = nullptr;
    QString m_qrcodeKey;
    QString m_cookie;
    QString m_userName;
    int m_pollCount = 0;
};

} // namespace bili::ui

#endif // BILI_CODE_UI_LOGIN_DIALOG_H
