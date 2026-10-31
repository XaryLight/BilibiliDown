#ifndef BILI_CODE_UI_TAB_SETTINGS_H
#define BILI_CODE_UI_TAB_SETTINGS_H

#include <QWidget>
#include <QLabel>

class QLineEdit;
class QComboBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QPlainTextEdit;

namespace bili::ui {

/**
 * @brief 设置标签页: 下载路径 / 格式 / 清晰度 / 并发 / ffmpeg / Cookie
 */
class TabSettings : public QWidget {
    Q_OBJECT
public:
    explicit TabSettings(QWidget *parent = nullptr);

private slots:
    void onBrowseSavePath();
    void onBrowseFfmpeg();
    void onSaveClicked();
    void onTestFfmpeg();
    void onCookieChanged();

private:
    void setupUi();
    void loadSettings();
    void saveSettings();

    QLineEdit *m_savePathEdit = nullptr;
    QComboBox *m_formatCombo = nullptr;
    QComboBox *m_qualityCombo = nullptr;
    QSpinBox *m_threadsSpin = nullptr;
    QSpinBox *m_concurrentSpin = nullptr;
    QLineEdit *m_ffmpegEdit = nullptr;
    QPlainTextEdit *m_cookieEdit = nullptr;
    QCheckBox *m_checkDashUrlCheck = nullptr;
    QCheckBox *m_minTrayCheck = nullptr;
    QCheckBox *m_notifyCheck = nullptr;
    QPushButton *m_testFfmpegBtn = nullptr;
    QLabel *m_ffmpegStatus = nullptr;
    QLabel *m_cookieStatus = nullptr;
};

} // namespace bili::ui

#endif // BILI_CODE_UI_TAB_SETTINGS_H
