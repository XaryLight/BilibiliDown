#include "TabParse.h"
#include "TabDownload.h"
#include "LoginDialog.h"
#include "../downloader/DownloadEngine.h"

#include <api/Logger.h>
#include <api/Config.h>
#include <parser/InputParser.h>
#include <parser/AbstractBaseParser.h>

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QMessageBox>
#include <QFuture>
#include <QtConcurrentRun>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QPixmap>
#include <QDesktopServices>
#include <QUrl>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QTableWidget>
#include <QLabel>
#include <QCheckBox>
#include <QSpinBox>
#include <QTabWidget>
#include <QMainWindow>
#include <QSet>
#include <algorithm>

namespace bili::ui {

using bili::api::Logger;
using bili::api::Config;
namespace model = bili::model;
namespace downloader = bili::downloader;

TabParse::TabParse(QWidget *parent) : QWidget(parent) {
    setupUi();

    // 异步解析
    m_watcher = new QFutureWatcher<model::VideoInfo>(this);
    connect(m_watcher, &QFutureWatcher<model::VideoInfo>::finished,
            this, &TabParse::onParseFinished);
}

TabParse::~TabParse() {
    if (m_watcher && m_watcher->isRunning())
        m_watcher->waitForFinished();
}

void TabParse::setupUi() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // ---- 输入区 ----
    QGroupBox *inputBox = new QGroupBox("视频链接", this);
    QHBoxLayout *inputLayout = new QHBoxLayout(inputBox);
    m_inputEdit = new QLineEdit(this);
    m_inputEdit->setPlaceholderText(
        "支持: BV号/av号/b23.tv短链/完整视频URL/番剧ss|ep|md/收藏夹ml/UP主空间链接/音频au号  例如: BV1xx411c7mD");
    m_parseBtn = new QPushButton("解析", this);
    m_parseBtn->setDefault(true);
    m_parseStatus = new QLabel(this);
    m_parseStatus->setMinimumWidth(220);
    inputLayout->addWidget(m_inputEdit);
    inputLayout->addWidget(m_parseBtn);
    inputLayout->addWidget(m_parseStatus);
    mainLayout->addWidget(inputBox);

    connect(m_inputEdit, &QLineEdit::returnPressed, this, &TabParse::onParseClicked);
    connect(m_inputEdit, &QLineEdit::textChanged, this, &TabParse::onInputChanged);
    connect(m_parseBtn, &QPushButton::clicked, this, &TabParse::onParseClicked);

    // ---- 视频信息 + 分P 列表 ----
    QGroupBox *infoBox = new QGroupBox("解析结果", this);
    QVBoxLayout *infoLayout = new QVBoxLayout(infoBox);
    QHBoxLayout *titleLayout = new QHBoxLayout();
    m_videoTitle = new QLabel("---", this);
    QFont titleFont = m_videoTitle->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 2);
    m_videoTitle->setFont(titleFont);
    m_videoTitle->setWordWrap(true);
    m_videoAuthor = new QLabel(this);
    m_videoAuthor->setStyleSheet("color:#FB7299;");
    titleLayout->addWidget(m_videoTitle, 1);
    titleLayout->addWidget(m_videoAuthor);
    infoLayout->addLayout(titleLayout);
    m_videoBrief = new QLabel(this);
    m_videoBrief->setWordWrap(true);
    m_videoBrief->setStyleSheet("color:gray;");
    infoLayout->addWidget(m_videoBrief);

    m_clipTable = new QTableWidget(this);
    m_clipTable->setColumnCount(5);
    m_clipTable->setHorizontalHeaderLabels({"序号", "分P标题", "AV号", "UP主", "集合"});
    m_clipTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_clipTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_clipTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_clipTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_clipTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_clipTable->setAlternatingRowColors(true);
    infoLayout->addWidget(m_clipTable, 1);
    mainLayout->addWidget(infoBox, 1);

    // ---- 下载选项 ----
    QGroupBox *dlBox = new QGroupBox("下载选项", this);
    QHBoxLayout *dlLayout = new QHBoxLayout(dlBox);
    dlLayout->addWidget(new QLabel("清晰度:", this));
    m_qualityCombo = new QComboBox(this);
    dlLayout->addWidget(m_qualityCombo);
    dlLayout->addWidget(new QLabel("格式:", this));
    m_formatCombo = new QComboBox(this);
    m_formatCombo->addItem("MP4 (DASH 音视频分流)", model::Format_MP4_DASH);
    m_formatCombo->addItem("FLV", model::Format_FLV);
    m_formatCombo->addItem("仅音频", model::Format_AudioOnly);
    dlLayout->addWidget(m_formatCombo);
    dlLayout->addStretch(1);
    m_mergeCheck = new QCheckBox("合集合并为单个MP4", this);
    m_mergeCheck->setToolTip("多分P/合集/番剧: 下载全部后使用 ffmpeg 合并为一个 MP4 文件");
    m_mergeCheck->setChecked(Config::instance().mergeCollection());
    m_deleteTempCheck = new QCheckBox("合并后删除中间文件", this);
    m_deleteTempCheck->setChecked(Config::instance().deleteTempAfterMerge());
    m_deleteTempCheck->setEnabled(m_mergeCheck->isChecked());
    dlLayout->addWidget(m_mergeCheck);
    dlLayout->addWidget(m_deleteTempCheck);
    m_downloadBtn = new QPushButton("加入下载列表", this);
    m_downloadBtn->setEnabled(false);
    m_downloadBtn->setMinimumWidth(120);
    dlLayout->addWidget(m_downloadBtn);
    mainLayout->addWidget(dlBox);

    connect(m_downloadBtn, &QPushButton::clicked, this, &TabParse::onDownloadClicked);
    connect(m_mergeCheck, &QCheckBox::toggled, this, [this](bool on) {
        m_deleteTempCheck->setEnabled(on);
        Config::instance().setMergeCollection(on);
    });
    connect(m_deleteTempCheck, &QCheckBox::toggled, this, [this](bool on) {
        Config::instance().setDeleteTempAfterMerge(on);
    });

    updateDownloadButton();
}

QString TabParse::defaultFormat() const {
    return m_formatCombo->currentData().toInt() == model::Format_FLV ? "FLV"
           : m_formatCombo->currentData().toInt() == model::Format_AudioOnly ? "音频" : "MP4";
}

int TabParse::selectedQn() const {
    return m_qualityCombo->currentData().toInt();
}

void TabParse::onInputChanged() {
    if (m_parsed) {
        // 输入变化后重置解析结果
        m_parsed = false;
        m_videoInfo = model::VideoInfo{};
        m_clipTable->setRowCount(0);
        m_videoTitle->setText("---");
        m_videoAuthor->clear();
        m_videoBrief->clear();
        m_qualityCombo->clear();
        updateDownloadButton();
    }
}

void TabParse::onParseClicked() {
    if (m_parsing)
        return;
    QString input = m_inputEdit->text().trimmed();
    if (input.isEmpty()) {
        QMessageBox::warning(this, "提示", "请输入视频链接或BV号");
        return;
    }

    m_parsing = true;
    m_parseBtn->setEnabled(false);
    m_parseStatus->setText("解析中...");
    m_downloadBtn->setEnabled(false);

    int videoFormat = m_formatCombo->currentData().toInt();

    // 后台线程解析 (避免阻塞 UI)
    auto future = QtConcurrent::run([input, videoFormat]() -> model::VideoInfo {
        try {
            bili::parser::InputParser parser;
            return parser.parse(input, videoFormat, /*getVideoLink=*/false);
        } catch (const std::exception &e) {
            Logger::error(QString("解析失败: %1").arg(e.what()));
            model::VideoInfo empty;
            empty.videoLink = "__ERROR__:" + QString(e.what());
            return empty;
        }
    });
    m_watcher->setFuture(future);
}

void TabParse::onParseFinished() {
    m_parsing = false;
    m_parseBtn->setEnabled(true);

    model::VideoInfo vi = m_watcher->result();
    if (vi.videoLink.startsWith("__ERROR__:")) {
        QString err = vi.videoLink.mid(9);
        m_parseStatus->setText("解析失败");
        QMessageBox::critical(this, "解析失败", err);
        return;
    }

    m_videoInfo = vi;
    m_parsed = true;
    m_parseStatus->setText(QString("解析成功, 共 %1 个分P").arg(vi.clipCount()));

    m_videoTitle->setText(vi.videoName.isEmpty() ? vi.videoId : vi.videoName);
    m_videoAuthor->setText(QString("UP主: %1").arg(vi.author));
    m_videoBrief->setText(vi.brief.left(200));

    // ---- 清晰度下拉 ----
    m_qualityCombo->clear();
    // 从所有 clip 的可用清晰度中取并集 (按清晰度降序)
    QSet<int> qnSet;
    for (const model::ClipInfo &clip : vi.clips)
        for (int qn : clip.links.keys())
            qnSet.insert(qn);
    QList<int> qns = qnSet.values();
    std::sort(qns.begin(), qns.end(), std::greater<int>());
    if (qns.isEmpty())
        qns = QList<int>{127, 120, 116, 112, 80, 74, 64, 32, 16};
    int preferred = Config::instance().defaultQuality();
    int selectedIdx = 0;
    for (int qn : qns) {
        m_qualityCombo->addItem(model::videoQualityText(qn), qn);
        if (qn == preferred)
            selectedIdx = m_qualityCombo->count() - 1;
    }
    // 默认偏好清晰度 (若不在列表中则选第一个 = 最高)
    if (qns.contains(preferred))
        m_qualityCombo->setCurrentIndex(selectedIdx);

    updateClipTable();
    updateDownloadButton();

    Logger::info(QString("解析完成: %1 (%2 个分P)").arg(vi.videoName).arg(vi.clipCount()));
}

void TabParse::updateClipTable() {
    m_clipTable->setRowCount(0);
    if (!m_parsed)
        return;
    m_clipTable->setRowCount(m_videoInfo.clipCount());
    for (int i = 0; i < m_videoInfo.clipCount(); i++) {
        const model::ClipInfo &clip = m_videoInfo.clips.at(i);
        auto *item0 = new QTableWidgetItem(QString::number(clip.displayOrder()));
        item0->setTextAlignment(Qt::AlignCenter);
        m_clipTable->setItem(i, 0, item0);
        m_clipTable->setItem(i, 1, new QTableWidgetItem(clip.title));
        m_clipTable->setItem(i, 2, new QTableWidgetItem(clip.avId));
        m_clipTable->setItem(i, 3, new QTableWidgetItem(clip.upName));
        m_clipTable->setItem(i, 4, new QTableWidgetItem(clip.listName));
    }
}

void TabParse::updateDownloadButton() {
    bool canDownload = m_parsed && !m_videoInfo.clips.isEmpty();
    // 合集合并仅在多分P时有意义
    if (m_mergeCheck->isChecked() && m_videoInfo.clipCount() <= 1)
        canDownload = canDownload; // 单P也允许加入, 合并自动退化为直接转码
    m_downloadBtn->setEnabled(canDownload);
}

void TabParse::onDownloadClicked() {
    if (!m_parsed || m_videoInfo.clips.isEmpty()) {
        QMessageBox::warning(this, "提示", "请先解析视频");
        return;
    }

    model::DownloadTask task;
    task.inputUrl = m_inputEdit->text().trimmed();
    task.videoInfo = m_videoInfo;
    task.qn = selectedQn();
    task.format = m_formatCombo->currentData().toInt();
    task.mergeCollection = m_mergeCheck->isChecked() && m_videoInfo.clipCount() > 1;
    task.deleteTempAfterMerge = m_deleteTempCheck->isChecked();
    task.status = model::TaskStatus::Queued;
    task.statusText = "队列中";

    int taskId = downloader::DownloadEngine::instance().addTask(task);

    // 跳到下载列表标签页
    if (auto *tabDownload = findChild<TabDownload *>()) {
        // 由 MainWindow 处理; 这里通过父窗口查找
    }
    QWidget *w = window();
    if (auto *mw = qobject_cast<QMainWindow *>(w)) {
        if (auto *tabs = mw->findChild<QTabWidget *>()) {
            for (int i = 0; i < tabs->count(); i++) {
                if (tabs->tabText(i) == "下载列表") {
                    tabs->setCurrentIndex(i);
                    break;
                }
            }
        }
    }

    downloader::DownloadEngine::instance().startTask(taskId);
    Logger::info(QString("已加入下载队列: #%1 %2 (qn=%3, 格式=%4%5)")
                     .arg(taskId)
                     .arg(m_videoInfo.videoName)
                     .arg(task.qn)
                     .arg(defaultFormat())
                     .arg(task.mergeCollection ? ", 合集合并" : ""));
}

void TabParse::onQualityChanged(int index) {
    Q_UNUSED(index)
    updateDownloadButton();
}

} // namespace bili::ui
