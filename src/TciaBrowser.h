#pragma once

#include <QDialog>
#include <QNetworkAccessManager>
#include <QJsonArray>
#include <functional>

class QComboBox;
class QTableWidget;
class QListWidget;
class QPushButton;
class QLabel;
class QProgressBar;
class QJsonDocument;
class QTabWidget;
class LoadingOverlay;

class TciaBrowser : public QDialog
{
    Q_OBJECT

public:
    explicit TciaBrowser(QWidget *parent = nullptr);

signals:
    void directoryReady(const QString &dirPath);

private slots:
    void onModalityChanged(int index);
    void onSearch();
    void onDownload();
    void openDownloaded();
    void removeDownloaded();

protected:
    void resizeEvent(QResizeEvent *e) override;

private:
    void fetchModalities();
    void fetchCollections(const QString &modality = {});
    void apiGet(const QString &endpoint,
                std::function<void(const QJsonDocument &)> callback);
    void apiSearch(std::function<void(const QJsonArray &)> callback);
    void fetchPage(int page);
    void showCurrentPage();
    void updatePageButtons();
    void applyFilter();   // modalite filtresi (client-side)
    void setStatus(const QString &msg, bool error = false);
    void setBusy(bool busy, const QString &overlayText = {});
    QString findDcmDir(const QString &rootDir);
    QString downloadsBaseDir() const;
    void saveDownloadRecord(const QString &uid, const QString &collection,
                            const QString &patientId, const QString &desc,
                            const QString &imageCount, const QString &dirPath);
    void refreshDownloadsList();

    static constexpr const char *BASE_V1 =
        "https://services.cancerimagingarchive.net/nbia-api/services/v1";

    QNetworkAccessManager *m_nam;

    // Ara sekmesi
    QComboBox    *m_modalityCombo   = nullptr;
    QComboBox    *m_collectionCombo = nullptr;
    QTableWidget *m_table           = nullptr;
    QPushButton  *m_searchBtn       = nullptr;
    QPushButton  *m_downloadBtn     = nullptr;
    QPushButton  *m_prevBtn         = nullptr;
    QPushButton  *m_nextBtn         = nullptr;
    QLabel       *m_statusLabel     = nullptr;
    QLabel       *m_pageLabel       = nullptr;
    QProgressBar *m_progress        = nullptr;
    LoadingOverlay *m_overlay       = nullptr;

    // İndirilenler sekmesi
    QTabWidget   *m_tabs            = nullptr;
    QListWidget  *m_downloadsList   = nullptr;
    QPushButton  *m_openDownloadBtn = nullptr;
    QPushButton  *m_removeDownloadBtn = nullptr;

    QJsonArray   m_allSeries;       // sunucudan gelen tüm sonuçlar
    QJsonArray   m_filteredSeries;  // modalite filtresi uygulanmış
    int          m_currentPage = 0;
    bool         m_busy        = false;
};
