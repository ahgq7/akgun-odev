#include "TciaBrowser.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QComboBox>
#include <QTableWidget>
#include <QListWidget>
#include <QTabWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QProcess>
#include <QMessageBox>
#include <QUrl>
#include <QUrlQuery>
#include <QDirIterator>
#include <QSslConfiguration>
#include <QPainter>
#include <QTimer>
#include <QResizeEvent>
#include <QDateTime>

class LoadingOverlay : public QWidget
{
public:
    explicit LoadingOverlay(QWidget *parent = nullptr) : QWidget(parent)
    {
        setAttribute(Qt::WA_TranslucentBackground);
        setAutoFillBackground(false);

        m_box = new QLabel(this);
        m_box->setAlignment(Qt::AlignCenter);
        m_box->setStyleSheet(
            "background:#2a2a2a; color:#fff; border-radius:10px;"
            "padding:24px 48px; font-size:15px; border:1px solid #666;");

        m_timer = new QTimer(this);
        connect(m_timer, &QTimer::timeout, this, [this]() {
            m_dots = (m_dots + 1) % 4;
            m_box->setText(m_text + QString(".").repeated(m_dots));
            reposition();
        });
        hide();
    }

    void showWith(const QString &text)
    {
        m_text = text; m_dots = 0;
        m_box->setText(text);
        m_box->adjustSize();
        reposition();
        show(); raise();
        m_timer->start(350);
    }

    void hideOverlay() { m_timer->stop(); hide(); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0, 0, 0, 150));
    }
    void resizeEvent(QResizeEvent *e) override { QWidget::resizeEvent(e); reposition(); }

private:
    void reposition()
    {
        if (parentWidget()) resize(parentWidget()->size());
        m_box->adjustSize();
        m_box->move((width()  - m_box->width())  / 2,
                    (height() - m_box->height()) / 2);
    }
    QLabel  *m_box;
    QTimer  *m_timer;
    int      m_dots = 0;
    QString  m_text;
};

enum Col {
    COL_COLLECTION = 0, COL_PATIENT, COL_MODALITY,
    COL_BODYPART, COL_DESCRIPTION, COL_IMAGES, COL_UID, COL_COUNT
};

static constexpr int PAGE_SIZE = 50;

TciaBrowser::TciaBrowser(QWidget *parent)
    : QDialog(parent)
    , m_nam(new QNetworkAccessManager(this))
{
    setWindowTitle("TCIA — Örnek DICOM Veritabanı");
    setMinimumSize(980, 660);
    setStyleSheet(R"(
        QDialog      { background:#2b2b2b; color:#ddd; }
        QTabWidget::pane { border:1px solid #555; background:#2b2b2b; }
        QTabBar::tab { background:#3c3c3c; color:#ddd; padding:6px 16px;
                       border:1px solid #555; border-bottom:none; margin-right:2px; }
        QTabBar::tab:selected { background:#2b2b2b; color:#4fc3f7; }
        QGroupBox    { color:#ddd; border:1px solid #555; margin-top:6px;
                       border-radius:4px; padding:6px; }
        QGroupBox::title { subcontrol-origin:margin; left:8px; }
        QComboBox    { background:#3c3c3c; color:#ddd; border:1px solid #555;
                       padding:3px 6px; border-radius:3px; }
        QComboBox QAbstractItemView { background:#3c3c3c; color:#ddd; }
        QPushButton  { background:#4a4a4a; color:#ddd; border:1px solid #666;
                       padding:5px 12px; border-radius:3px; }
        QPushButton:hover    { background:#5a5a5a; }
        QPushButton:disabled { background:#333; color:#555; }
        QTableWidget { background:#1e1e1e; color:#ddd; gridline-color:#444;
                       border:1px solid #555; alternate-background-color:#242424; }
        QHeaderView::section { background:#3c3c3c; color:#ddd; padding:4px;
                               border:1px solid #555; }
        QTableWidget::item:selected { background:#1e6e9e; }
        QListWidget  { background:#1e1e1e; color:#ddd; border:1px solid #555;
                       alternate-background-color:#242424; }
        QListWidget::item:selected { background:#1e6e9e; }
        QLabel       { color:#ccc; }
        QProgressBar { background:#333; border:1px solid #555; border-radius:3px;
                       text-align:center; height:14px; }
        QProgressBar::chunk { background:#1e6e9e; border-radius:3px; }
    )");

    m_tabs = new QTabWidget(this);

    auto *searchWidget = new QWidget;
    auto *searchVBox   = new QVBoxLayout(searchWidget);
    searchVBox->setSpacing(6);
    searchVBox->setContentsMargins(6, 6, 6, 6);

    auto *searchRow = new QHBoxLayout;
    searchRow->setSpacing(8);
    searchRow->addWidget(new QLabel("Koleksiyon:"));
    m_collectionCombo = new QComboBox;
    m_collectionCombo->addItem("Yükleniyor...");
    m_collectionCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    searchRow->addWidget(m_collectionCombo, 1);
    m_searchBtn = new QPushButton("Ara");
    m_searchBtn->setEnabled(false);
    m_searchBtn->setMinimumWidth(70);
    searchRow->addWidget(m_searchBtn);
    searchVBox->addLayout(searchRow);

    auto *filterBar = new QFrame;
    filterBar->setFrameShape(QFrame::StyledPanel);
    filterBar->setStyleSheet(
        "QFrame { background:#232323; border:1px solid #444; border-radius:4px; padding:2px; }"
        "QLabel#filterHint { color:#888; font-style:italic; font-size:11px; }");
    auto *filterLayout = new QHBoxLayout(filterBar);
    filterLayout->setSpacing(8);
    filterLayout->setContentsMargins(8, 4, 8, 4);
    auto *filterIcon = new QLabel("▼  Sonuçları filtrele:");
    filterIcon->setStyleSheet("color:#aaa; font-size:11px;");
    filterLayout->addWidget(filterIcon);
    filterLayout->addWidget(new QLabel("Modalite:"));
    m_modalityCombo = new QComboBox;
    m_modalityCombo->addItem("Yükleniyor...");
    m_modalityCombo->setMinimumWidth(130);
    filterLayout->addWidget(m_modalityCombo);
    auto *filterHint = new QLabel("(arama yapıldıktan sonra aktif)");
    filterHint->setObjectName("filterHint");
    filterLayout->addWidget(filterHint);
    filterLayout->addStretch();
    searchVBox->addWidget(filterBar);

    // Tablo
    m_table = new QTableWidget(0, COL_COUNT);
    m_table->setHorizontalHeaderLabels({
        "Koleksiyon", "Hasta ID", "Modalite",
        "Vücut Bölgesi", "Seri Açıklaması", "Görüntü", "UID"
    });
    m_table->setColumnHidden(COL_UID, true);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(COL_DESCRIPTION, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setAlternatingRowColors(true);
    searchVBox->addWidget(m_table, 1);

    // Sayfalama satırı
    m_prevBtn   = new QPushButton("< Önceki");
    m_nextBtn   = new QPushButton("Sonraki >");
    m_pageLabel = new QLabel("—");
    m_pageLabel->setAlignment(Qt::AlignCenter);
    m_prevBtn->setEnabled(false);
    m_nextBtn->setEnabled(false);
    auto *pageRow = new QHBoxLayout;
    pageRow->addStretch();
    pageRow->addWidget(m_prevBtn);
    pageRow->addWidget(m_pageLabel);
    pageRow->addWidget(m_nextBtn);
    pageRow->addStretch();
    searchVBox->addLayout(pageRow);

    // Alt bar
    m_statusLabel = new QLabel("Modaliteler yükleniyor...");
    m_progress    = new QProgressBar;
    m_progress->setRange(0, 0);
    m_progress->setFixedHeight(14);
    m_progress->setVisible(false);
    m_downloadBtn = new QPushButton("İndir ve Aç");
    m_downloadBtn->setEnabled(false);
    auto *retryBtn = new QPushButton("Yeniden Dene");
    auto *bottomRow = new QHBoxLayout;
    bottomRow->addWidget(m_statusLabel, 1);
    bottomRow->addWidget(m_progress);
    bottomRow->addWidget(retryBtn);
    bottomRow->addWidget(m_downloadBtn);
    searchVBox->addLayout(bottomRow);

    m_tabs->addTab(searchWidget, "Ara");

    // Tab 1: İndirilenler
    auto *dlWidget = new QWidget;
    auto *dlVBox   = new QVBoxLayout(dlWidget);
    dlVBox->setSpacing(6);
    dlVBox->setContentsMargins(6, 6, 6, 6);

    auto *dlInfoLabel = new QLabel(
        "Daha önce indirilen seriler — çift tıkla veya 'Aç' butonuna bas.");
    dlInfoLabel->setStyleSheet("color:#aaa; padding:4px;");
    dlVBox->addWidget(dlInfoLabel);

    m_downloadsList = new QListWidget;
    m_downloadsList->setAlternatingRowColors(true);
    dlVBox->addWidget(m_downloadsList, 1);

    auto *dlBtnRow = new QHBoxLayout;
    m_openDownloadBtn   = new QPushButton("Aç");
    m_removeDownloadBtn = new QPushButton("Listeden Kaldır");
    m_openDownloadBtn->setEnabled(false);
    m_removeDownloadBtn->setEnabled(false);
    dlBtnRow->addStretch();
    dlBtnRow->addWidget(m_openDownloadBtn);
    dlBtnRow->addWidget(m_removeDownloadBtn);
    dlVBox->addLayout(dlBtnRow);

    m_tabs->addTab(dlWidget, "İndirilenler (0)");

    auto *mainVBox = new QVBoxLayout(this);
    mainVBox->addWidget(m_tabs);

    m_overlay = new LoadingOverlay(this);

    connect(m_modalityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TciaBrowser::onModalityChanged);
    connect(m_collectionCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { if (!m_busy) setBusy(false); });
    connect(m_searchBtn,   &QPushButton::clicked,  this, &TciaBrowser::onSearch);
    connect(m_downloadBtn, &QPushButton::clicked,  this, &TciaBrowser::onDownload);
    connect(m_prevBtn, &QPushButton::clicked, this, [this]() { fetchPage(m_currentPage - 1); });
    connect(m_nextBtn, &QPushButton::clicked, this, [this]() { fetchPage(m_currentPage + 1); });
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        m_downloadBtn->setEnabled(!m_table->selectedItems().isEmpty() && !m_busy);
    });
    connect(retryBtn, &QPushButton::clicked, this, [this]() {
        m_modalityCombo->clear();
        m_modalityCombo->addItem("Yükleniyor...");
        fetchModalities();
    });
    connect(m_downloadsList, &QListWidget::itemSelectionChanged, this, [this]() {
        bool sel = !m_downloadsList->selectedItems().isEmpty();
        m_openDownloadBtn->setEnabled(sel);
        m_removeDownloadBtn->setEnabled(sel);
    });
    connect(m_downloadsList, &QListWidget::itemDoubleClicked,
            this, &TciaBrowser::openDownloaded);
    connect(m_openDownloadBtn,   &QPushButton::clicked, this, &TciaBrowser::openDownloaded);
    connect(m_removeDownloadBtn, &QPushButton::clicked, this, &TciaBrowser::removeDownloaded);

    fetchModalities();
    refreshDownloadsList();
}

void TciaBrowser::resizeEvent(QResizeEvent *e)
{
    QDialog::resizeEvent(e);
    m_overlay->resize(size());
}

QString TciaBrowser::downloadsBaseDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
           + "/DicomViewer/downloads";
}

void TciaBrowser::apiGet(const QString &endpoint,
                         std::function<void(const QJsonDocument &)> callback)
{
    QNetworkRequest req(QUrl(QString("%1%2").arg(BASE_V1, endpoint)));
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(30000);

    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, callback]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setStatus("Hata: " + reply->errorString(), true);
            if (m_modalityCombo->count() == 1 &&
                m_modalityCombo->itemText(0) == "Yükleniyor...") {
                m_modalityCombo->clear();
                m_modalityCombo->addItem("— bağlantı hatası —");
            }
            setBusy(false);
            return;
        }
        QJsonParseError jerr;
        auto doc = QJsonDocument::fromJson(reply->readAll(), &jerr);
        if (jerr.error != QJsonParseError::NoError) {
            setStatus("JSON hatası: " + jerr.errorString(), true);
            setBusy(false);
            return;
        }
        callback(doc);
    });
}

// Modalite sunucuya gönderilmez — client-side filtrelenir
void TciaBrowser::apiSearch(std::function<void(const QJsonArray &)> callback)
{
    QString col = m_collectionCombo->currentText();

    QUrlQuery q;
    if (!col.isEmpty() && col != "Yükleniyor...") q.addQueryItem("Collection", col);

    QUrl url(QString("%1/getSeries").arg(BASE_V1));
    url.setQuery(q);

    QNetworkRequest req(url);
    req.setRawHeader("Accept", "application/json");
    req.setTransferTimeout(300000); // 5 dakika

    setStatus(QString("İstek: ...getSeries?Collection=%1")
                  .arg(col.isEmpty() ? "(tümü)" : col));

    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, callback]() {
        reply->deleteLater();
        int httpStatus = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

        if (reply->error() != QNetworkReply::NoError) {
            setStatus(QString("Hata (HTTP %1): %2").arg(httpStatus).arg(reply->errorString()), true);
            setBusy(false);
            return;
        }
        QByteArray data = reply->readAll();
        if (data.trimmed().isEmpty()) {
            setStatus(QString("Sunucu boş yanıt döndürdü (HTTP %1).\nURL: %2")
                          .arg(httpStatus).arg(url.toString()), true);
            setBusy(false);
            return;
        }
        QJsonParseError jerr;
        auto doc = QJsonDocument::fromJson(data, &jerr);
        if (jerr.error != QJsonParseError::NoError) {
            setStatus(QString("JSON hatası (HTTP %1): %2\nİlk 100 byte: %3")
                          .arg(httpStatus)
                          .arg(jerr.errorString())
                          .arg(QString::fromLatin1(data.left(100))), true);
            setBusy(false);
            return;
        }
        QJsonArray arr;
        if (doc.isArray()) {
            arr = doc.array();
        } else if (doc.isObject()) {
            auto obj = doc.object();
            for (const QString &key : {"data", "resultSet", "series", "Data"}) {
                if (obj.contains(key)) { arr = obj[key].toArray(); break; }
            }
        }
        callback(arr);
    });
}

void TciaBrowser::fetchModalities()
{
    setBusy(true, "Modaliteler yükleniyor");
    apiGet("/getModalityValues", [this](const QJsonDocument &doc) {
        m_modalityCombo->blockSignals(true);
        m_modalityCombo->clear();
        m_modalityCombo->addItem("Tümü", "");
        for (const auto &v : doc.array()) {
            QString mod;
            if (v.isString()) mod = v.toString();
            else if (v.isObject()) {
                auto o = v.toObject();
                mod = o["Modality"].toString();
                if (mod.isEmpty()) mod = o["modality"].toString();
                if (mod.isEmpty() && !o.isEmpty()) mod = o.begin().value().toString();
            }
            if (!mod.isEmpty()) m_modalityCombo->addItem(mod, mod);
        }
        m_modalityCombo->setCurrentIndex(0); // Tümü varsayılan
        m_modalityCombo->blockSignals(false);
        setStatus(QString("%1 modalite yüklendi.").arg(m_modalityCombo->count() - 1));
        fetchCollections(m_modalityCombo->currentData().toString());
    });
}

// getCollectionValues hiçbir parametre kabul etmez — modality filtresi yok
void TciaBrowser::fetchCollections(const QString &)
{
    setBusy(true, "Koleksiyonlar yükleniyor");
    apiGet("/getCollectionValues", [this](const QJsonDocument &doc) {
        m_collectionCombo->blockSignals(true);
        m_collectionCombo->clear();
        for (const auto &v : doc.array()) {
            QString col;
            if (v.isString()) col = v.toString();
            else if (v.isObject()) {
                auto o = v.toObject();
                col = o["Collection"].toString();
                if (col.isEmpty()) col = o["collection"].toString();
                if (col.isEmpty() && !o.isEmpty()) col = o.begin().value().toString();
            }
            if (!col.isEmpty()) m_collectionCombo->addItem(col, col);
        }
        m_collectionCombo->blockSignals(false);
        setStatus(QString("%1 koleksiyon. Bir koleksiyon veya modalite seçin → Ara.")
                      .arg(m_collectionCombo->count() - 1));
        setBusy(false);
    });
}

void TciaBrowser::onModalityChanged(int)
{
    // Modalite client-side filtre — mevcut sonuçlara uygula
    if (!m_allSeries.isEmpty()) {
        applyFilter();
        if (m_filteredSeries.isEmpty()) {
            m_table->setRowCount(0);
            m_pageLabel->setText("—");
            m_prevBtn->setEnabled(false);
            m_nextBtn->setEnabled(false);
            setStatus(QString("%1 seriden hiçbiri seçili modalitede değil.").arg(m_allSeries.size()));
        } else {
            setStatus(QString("%1 seri (toplam %2, filtreli).")
                          .arg(m_filteredSeries.size()).arg(m_allSeries.size()));
            fetchPage(0);
        }
    }
    if (!m_busy) setBusy(false);
}

void TciaBrowser::onSearch()
{
    QString col = m_collectionCombo->currentText();
    if (col.isEmpty() || col == "Yükleniyor...") {
        setStatus("Lütfen bir koleksiyon seçin.", true);
        return;
    }
    m_allSeries = QJsonArray();
    m_table->setRowCount(0);
    m_downloadBtn->setEnabled(false);
    m_prevBtn->setEnabled(false);
    m_nextBtn->setEnabled(false);
    m_pageLabel->setText("—");

    setBusy(true, "Seriler getiriliyor");
    apiSearch([this](const QJsonArray &arr) {
        m_allSeries = arr;
        applyFilter();
        if (m_filteredSeries.isEmpty()) {
            setStatus(arr.isEmpty() ? "Sonuç bulunamadı."
                                    : QString("%1 seri bulundu, seçili modalitede sonuç yok.").arg(arr.size()));
            m_pageLabel->setText("—");
            setBusy(false);
            return;
        }
        setStatus(QString("%1 seri (toplam %2, filtreli).")
                      .arg(m_filteredSeries.size()).arg(arr.size()));
        fetchPage(0);
        setBusy(false);
    });
}

void TciaBrowser::applyFilter()
{
    QString mod = m_modalityCombo->currentData().toString();
    if (mod.isEmpty()) {
        m_filteredSeries = m_allSeries;
        return;
    }
    m_filteredSeries = QJsonArray();
    for (const auto &v : m_allSeries) {
        QJsonObject obj = v.toObject();
        QString m = obj.value("Modality").toString();
        if (m.isEmpty()) m = obj.value("modality").toString();
        if (m.compare(mod, Qt::CaseInsensitive) == 0)
            m_filteredSeries.append(v);
    }
}

void TciaBrowser::fetchPage(int page)
{
    if (m_filteredSeries.isEmpty()) return;
    m_currentPage = page;
    showCurrentPage();
    updatePageButtons();
}

void TciaBrowser::showCurrentPage()
{
    int total = m_filteredSeries.size();
    int from  = m_currentPage * PAGE_SIZE;
    int to    = qMin(from + PAGE_SIZE, total);

    m_table->setRowCount(to - from);

    for (int i = from; i < to; ++i) {
        QJsonObject obj = m_filteredSeries[i].toObject();

        // getSeries yanıtı PascalCase kullanır
        auto val = [&](const QString &camel, const QString &pascal) -> QString {
            auto v = obj.value(camel);
            if (!v.isUndefined() && !v.isNull() && !v.toString().isEmpty())
                return v.toString();
            return obj.value(pascal).toString();
        };

        // Görüntü sayısı: JSON number veya string olabilir
        auto valInt = [&](const QString &camel, const QString &pascal) -> QString {
            for (const QString &key : {camel, pascal}) {
                auto v = obj.value(key);
                if (v.isDouble()) return QString::number(qRound(v.toDouble()));
                if (v.isString() && !v.toString().isEmpty()) return v.toString();
            }
            return "?";
        };

        auto cell = [&](int col, const QString &text) {
            auto *item = new QTableWidgetItem(text);
            item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            m_table->setItem(i - from, col, item);
        };

        cell(COL_COLLECTION,  val("collection",        "Collection"));
        cell(COL_PATIENT,     val("patientId",         "PatientID"));
        cell(COL_MODALITY,    val("modality",          "Modality"));
        cell(COL_BODYPART,    val("bodyPartExamined",  "BodyPartExamined"));
        cell(COL_DESCRIPTION, val("seriesDescription", "SeriesDescription"));
        cell(COL_IMAGES,      valInt("imageCount",     "ImageCount"));
        cell(COL_UID,         val("seriesInstanceUID", "SeriesInstanceUID"));
    }

    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setSectionResizeMode(
        COL_DESCRIPTION, QHeaderView::Stretch);
}

void TciaBrowser::updatePageButtons()
{
    int total    = m_filteredSeries.size();
    int lastPage = (total - 1) / PAGE_SIZE;
    m_prevBtn->setEnabled(!m_busy && m_currentPage > 0);
    m_nextBtn->setEnabled(!m_busy && m_currentPage < lastPage);

    int from = m_currentPage * PAGE_SIZE + 1;
    int to   = qMin((m_currentPage + 1) * PAGE_SIZE, total);
    m_pageLabel->setText(
        QString("Sayfa %1/%2  (%3–%4 / toplam %5)")
            .arg(m_currentPage + 1).arg(lastPage + 1)
            .arg(from).arg(to).arg(total));
}

void TciaBrowser::onDownload()
{
    if (m_table->selectedItems().isEmpty()) return;

    int row = m_table->currentRow();
    QString uid        = m_table->item(row, COL_UID)->text();
    QString collection = m_table->item(row, COL_COLLECTION)->text();
    QString patientId  = m_table->item(row, COL_PATIENT)->text();
    QString desc       = m_table->item(row, COL_DESCRIPTION)->text();
    QString imageCount = m_table->item(row, COL_IMAGES)->text();

    if (imageCount.toInt() > 500) {
        auto r = QMessageBox::question(this, "Büyük Seri",
            QString("Bu seri %1 görüntü içeriyor. İndirmek uzun sürebilir. Devam?")
                .arg(imageCount));
        if (r != QMessageBox::Yes) return;
    }

    setBusy(true, "DICOM dosyaları indiriliyor");
    m_progress->setRange(0, 0);
    m_progress->setVisible(true);

    auto sanitize = [](const QString &s) {
        QString r;
        for (const QChar &c : s)
            r += (c.isLetterOrNumber() || c == '-') ? c : '_';
        return r;
    };
    QString dirName = sanitize(collection).left(30) + "_"
                    + sanitize(patientId).left(20)  + "_"
                    + sanitize(uid).left(40);
    QString saveDir = downloadsBaseDir() + "/" + dirName;
    QString zipPath = saveDir + "/series.zip";
    QString destDir = saveDir + "/dcm";

    // Önceki yarım indirmeyi temizle — dosya karışıklığını önler
    if (QDir(destDir).exists())
        QDir(destDir).removeRecursively();
    QDir().mkpath(destDir);

    QNetworkRequest req(QUrl(QString("%1/getImage?SeriesInstanceUID=%2")
                                 .arg(BASE_V1, uid)));
    req.setSslConfiguration(QSslConfiguration::defaultConfiguration());
    auto *reply = m_nam->get(req);

    connect(reply, &QNetworkReply::downloadProgress, this,
            [this](qint64 recv, qint64 total) {
        if (total > 0) {
            m_progress->setRange(0, 100);
            m_progress->setValue(static_cast<int>(recv * 100 / total));
        }
        setStatus(QString("İndiriliyor: %1 MB%2")
                      .arg(recv / 1048576.0, 0, 'f', 1)
                      .arg(total > 0
                           ? QString(" / %1 MB").arg(total / 1048576.0, 0, 'f', 1)
                           : ""));
    });

    connect(reply, &QNetworkReply::finished, this,
            [this, reply, zipPath, destDir, saveDir,
             uid, collection, patientId, desc, imageCount]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            setStatus("İndirme hatası: " + reply->errorString(), true);
            setBusy(false); m_progress->setVisible(false);
            return;
        }
        QFile f(zipPath);
        if (f.open(QIODevice::WriteOnly)) { f.write(reply->readAll()); f.close(); }

        setBusy(true, "ZIP açılıyor");
        QProcess proc;
        proc.start("powershell", {
            "-ExecutionPolicy", "Bypass", "-Command",
            QString("Expand-Archive -Path '%1' -DestinationPath '%2' -Force")
                .arg(zipPath, destDir)
        });
        proc.waitForFinished(120000);
        QFile::remove(zipPath);

        m_progress->setVisible(false);
        QString dcmDir = findDcmDir(destDir);
        if (dcmDir.isEmpty()) dcmDir = destDir;

        saveDownloadRecord(uid, collection, patientId, desc, imageCount, dcmDir);
        refreshDownloadsList();

        setBusy(false);
        if (dcmDir.isEmpty()) { setStatus("DICOM bulunamadı.", true); return; }
        setStatus("İndirildi: " + dcmDir);
        emit directoryReady(dcmDir);
        accept();
    });
}

void TciaBrowser::saveDownloadRecord(const QString &uid,
                                      const QString &collection,
                                      const QString &patientId,
                                      const QString &desc,
                                      const QString &imageCount,
                                      const QString &dirPath)
{
    QString catalogPath = downloadsBaseDir() + "/downloads.json";
    QFile f(catalogPath);

    QJsonArray catalog;
    if (f.open(QIODevice::ReadOnly)) {
        QJsonParseError err;
        auto doc = QJsonDocument::fromJson(f.readAll(), &err);
        if (err.error == QJsonParseError::NoError && doc.isArray())
            catalog = doc.array();
        f.close();
    }

    // UID varsa güncelle, yoksa ekle
    for (int i = 0; i < catalog.size(); ++i) {
        if (catalog[i].toObject()["uid"].toString() == uid) {
            catalog.removeAt(i);
            break;
        }
    }

    QJsonObject entry;
    entry["uid"]        = uid;
    entry["collection"] = collection;
    entry["patientId"]  = patientId;
    entry["desc"]       = desc;
    entry["imageCount"] = imageCount;
    entry["path"]       = dirPath;
    entry["date"]       = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm");
    catalog.prepend(entry);  // en yeni başta

    QDir().mkpath(downloadsBaseDir());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(catalog).toJson());
        f.close();
    }
}

void TciaBrowser::refreshDownloadsList()
{
    m_downloadsList->clear();
    QString catalogPath = downloadsBaseDir() + "/downloads.json";
    QFile f(catalogPath);
    if (!f.open(QIODevice::ReadOnly)) {
        m_tabs->setTabText(1, "İndirilenler (0)");
        return;
    }
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        m_tabs->setTabText(1, "İndirilenler (0)");
        return;
    }

    QJsonArray catalog = doc.array();
    for (const auto &v : catalog) {
        QJsonObject o = v.toObject();
        QString path = o["path"].toString();
        bool exists  = QDir(path).exists();

        QString label = QString("[%1]  %2  |  %3  |  %4 görüntü  —  %5%6")
            .arg(o["collection"].toString())
            .arg(o["patientId"].toString())
            .arg(o["desc"].toString())
            .arg(o["imageCount"].toString())
            .arg(o["date"].toString())
            .arg(exists ? "" : "  ⚠ klasör yok");

        auto *item = new QListWidgetItem(label, m_downloadsList);
        item->setData(Qt::UserRole, path);
        item->setData(Qt::UserRole + 1, o["uid"].toString());
        if (!exists)
            item->setForeground(QColor("#ff6b6b"));
    }

    m_tabs->setTabText(1, QString("İndirilenler (%1)").arg(catalog.size()));
}

void TciaBrowser::openDownloaded()
{
    auto *item = m_downloadsList->currentItem();
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();
    if (!QDir(path).exists()) {
        QMessageBox::warning(this, "Hata",
            "Klasör bulunamadı:\n" + path +
            "\n\nDosyalar silinmiş olabilir.");
        return;
    }
    emit directoryReady(path);
    accept();
}

void TciaBrowser::removeDownloaded()
{
    auto *item = m_downloadsList->currentItem();
    if (!item) return;
    QString uid = item->data(Qt::UserRole + 1).toString();

    QString catalogPath = downloadsBaseDir() + "/downloads.json";
    QFile f(catalogPath);
    if (!f.open(QIODevice::ReadOnly)) return;
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    f.close();
    if (err.error != QJsonParseError::NoError || !doc.isArray()) return;

    QJsonArray catalog = doc.array();
    for (int i = 0; i < catalog.size(); ++i) {
        if (catalog[i].toObject()["uid"].toString() == uid) {
            catalog.removeAt(i);
            break;
        }
    }
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(catalog).toJson());
        f.close();
    }
    refreshDownloadsList();
}

QString TciaBrowser::findDcmDir(const QString &rootDir)
{
    // Önce .dcm uzantılı dene
    QDirIterator it(rootDir, {"*.dcm", "*.DCM"}, QDir::Files,
                    QDirIterator::Subdirectories);
    if (it.hasNext()) {
        it.next();
        return QFileInfo(it.filePath()).absolutePath();
    }
    // TCIA dosyaları çoğunlukla uzantısız gelir — herhangi bir dosya yeterli
    QDirIterator it2(rootDir, QDir::Files, QDirIterator::Subdirectories);
    while (it2.hasNext()) {
        it2.next();
        QString fp = it2.filePath();
        // Küçük dosyaları atla (thumbnail vs.)
        if (QFileInfo(fp).size() > 1024)
            return QFileInfo(fp).absolutePath();
    }
    return {};
}

void TciaBrowser::setStatus(const QString &msg, bool error)
{
    m_statusLabel->setText(msg);
    m_statusLabel->setStyleSheet(error ? "color:#ff6b6b;" : "color:#ccc;");
}

void TciaBrowser::setBusy(bool busy, const QString &overlayText)
{
    m_busy = busy;
    if (busy && !overlayText.isEmpty()) m_overlay->showWith(overlayText);
    else                                m_overlay->hideOverlay();
    // Koleksiyon listesi yüklenmiş ve geçerli seçim var
    bool canSearch = !busy
                  && m_modalityCombo->count() > 1
                  && m_collectionCombo->count() > 0
                  && !m_collectionCombo->currentText().isEmpty()
                  && m_collectionCombo->currentText() != "Yükleniyor...";
    m_searchBtn->setEnabled(canSearch);
    m_modalityCombo->setEnabled(!busy);
    m_collectionCombo->setEnabled(!busy);
    if (!busy) updatePageButtons();
}
