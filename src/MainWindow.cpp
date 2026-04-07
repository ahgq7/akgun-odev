#include "MainWindow.h"
#include "DicomImageViewer.h"
#include "MeasurementWidget.h"
#include "TciaBrowser.h"

#include <QMenuBar>
#include <QMenu>
#include <QAction>
#include <QStatusBar>
#include <QDockWidget>
#include <QFileDialog>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QButtonGroup>
#include <QGroupBox>
#include <QRadioButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QScrollArea>
#include <QCheckBox>
#include <QFrame>
#include <cmath>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    createActions();
    createMenuBar();
    createCentralWidget();
    createControlPanel();
    createStatusBar();

    setMinimumSize(900, 600);
    setStyleSheet(R"(
        QMainWindow  { background: #2b2b2b; }
        QDockWidget  { background: #3c3c3c; color: #ddd; }
        QGroupBox    { color: #ddd; border: 1px solid #555; margin-top: 6px;
                       border-radius: 4px; padding: 6px; }
        QGroupBox::title { subcontrol-origin: margin; left: 8px; }
        QPushButton  { background: #4a4a4a; color: #ddd; border: 1px solid #666;
                       padding: 6px 10px; border-radius: 3px; }
        QPushButton:hover   { background: #5a5a5a; }
        QPushButton:pressed { background: #1a5a8a; border-color: #4fc3f7; }
        QPushButton:checked { background: #1e6e9e; border-color: #4fc3f7; }
        QPushButton:focus   { outline: none; }
        QLabel  { color: #ccc; }
        QSlider::groove:horizontal { background:#555; height:4px; border-radius:2px; }
        QSlider::handle:horizontal { background:#aaa; width:12px; height:12px;
                                      margin:-4px 0; border-radius:6px; }
        QStatusBar   { background:#1e1e1e; color:#aaa; }
        QRadioButton { color:#ddd; }
        QRadioButton::indicator { width:13px; height:13px; }
        QMenuBar     { background:#2b2b2b; color:#ddd; }
        QMenuBar::item:selected { background:#3c3c3c; }
        QMenu        { background:#2b2b2b; color:#ddd; border:1px solid #555; }
        QMenu::item:selected { background:#1e6e9e; }
        QScrollArea#controlScroll                { background: #3c3c3c; border: none; }
        QWidget#controlViewport                  { background: #3c3c3c; }
        QWidget#controlPanel                     { background: #3c3c3c; }
    )");
}

void MainWindow::createActions()
{
    m_actOpenFile = new QAction("Dosya Aç (*.dcm)", this);
    m_actOpenFile->setShortcut(Qt::CTRL | Qt::Key_D);
    connect(m_actOpenFile, &QAction::triggered, this, &MainWindow::openDicomFile);

    m_actOpenDir = new QAction("Klasör Aç", this);
    m_actOpenDir->setShortcut(Qt::CTRL | Qt::Key_A);
    connect(m_actOpenDir, &QAction::triggered, this, &MainWindow::openDicomDirectory);

    m_actTcia = new QAction("TCIA Örnek Veritabanı...", this);
    m_actTcia->setShortcut(Qt::CTRL | Qt::Key_T);
    connect(m_actTcia, &QAction::triggered, this, &MainWindow::openTciaBrowser);

    m_actExit = new QAction("Çıkış", this);
    m_actExit->setShortcut(QKeySequence::Quit);
    connect(m_actExit, &QAction::triggered, this, &QMainWindow::close);
}

void MainWindow::createMenuBar()
{
    auto *file = menuBar()->addMenu("Dosya");
    file->addAction(m_actOpenFile);
    file->addAction(m_actOpenDir);
    file->addSeparator();
    file->addAction(m_actTcia);
    file->addSeparator();
    file->addAction(m_actExit);
}

void MainWindow::createCentralWidget()
{
    m_viewer = new DicomImageViewer(this);
    connect(m_viewer, &DicomImageViewer::imageLoaded,    this, &MainWindow::onImageLoaded);
    connect(m_viewer, &DicomImageViewer::sliceChanged,   this, &MainWindow::onSliceChanged);
    connect(m_viewer, &DicomImageViewer::windowLevelChanged,
            this, &MainWindow::onWindowLevelChanged);
    connect(m_viewer, &DicomImageViewer::pixelInfoChanged,
            this, &MainWindow::onPixelInfoChanged);
    connect(m_viewer, &DicomImageViewer::sortOrderChanged, this, [this](DicomImageViewer::SortOrder o) {
        if (m_sortGroup) {
            m_sortGroup->blockSignals(true);
            auto *btn = m_sortGroup->button(static_cast<int>(o));
            if (btn) btn->setChecked(true);
            m_sortGroup->blockSignals(false);
        }
    });
    connect(m_viewer->measurementWidget(), &MeasurementWidget::measurementDone,
            this, &MainWindow::onMeasurementDone);
    connect(m_viewer, &DicomImageViewer::modeChangeRequested, this, [this](int id) {
        // Buton grubunu güncelle (setMode zaten viewer tarafında çağrıldı)
        m_modeGroup->blockSignals(true);
        if (auto *btn = m_modeGroup->button(id)) btn->setChecked(true);
        m_modeGroup->blockSignals(false);
        // Status bar mesajını güncelle
        static const char *msgs[] = {
            "Kaydır modu  [X]",
            "W/L Ayarı modu  [C]",
            "Mesafe: 1. noktayı tıkla  [ESC: iptal]",
            "Elips: merkezi tıkla, 2. tıkla kenar  [ESC: iptal]",
            "Açı: 1. ucu tıkla (3 tık)  [ESC: iptal]",
            "ROI: sol üst köşeyi tıkla  [ESC: iptal]",
        };
        if (id >= 0 && id <= 5) m_statusMsg->setText(msgs[id]);
    });
    setCentralWidget(m_viewer);
}

void MainWindow::createControlPanel()
{
    auto *dock  = new QDockWidget("Kontroller", this);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);

    auto *panel = new QWidget;
    panel->setObjectName("controlPanel");
    auto *vbox  = new QVBoxLayout(panel);
    vbox->setSpacing(8);
    vbox->setContentsMargins(8, 8, 8, 8);

    // --- Dosya ---
    auto *fileGroup = new QGroupBox("Dosya", panel);
    auto *fL = new QVBoxLayout(fileGroup);
    m_openFileBtn = new QPushButton("Dosya Aç (*.dcm)");
    m_openDirBtn  = new QPushButton("Klasör Aç");
    auto *tciaBtn = new QPushButton("TCIA Örnek Veritabanı...");
    tciaBtn->setStyleSheet("QPushButton { color:#4fc3f7; }");
    connect(m_openFileBtn, &QPushButton::clicked, this, &MainWindow::openDicomFile);
    connect(m_openDirBtn,  &QPushButton::clicked, this, &MainWindow::openDicomDirectory);
    connect(tciaBtn,       &QPushButton::clicked, this, &MainWindow::openTciaBrowser);
    fL->addWidget(m_openFileBtn);
    fL->addWidget(m_openDirBtn);
    fL->addWidget(tciaBtn);
    vbox->addWidget(fileGroup);

    // --- Mod seçimi ---
    auto *modeGroup = new QGroupBox("Fare Modu", panel);
    auto *mL = new QVBoxLayout(modeGroup);

    m_btnPan         = new QPushButton("Kaydır  [X]");
    m_btnWL          = new QPushButton("W/L Ayarı  [C]");
    m_btnMeasDist    = new QPushButton("Ölçüm — Mesafe  [V]");
    m_btnMeasEllipse = new QPushButton("Ölçüm — Elips / Alan  [B]");
    m_btnMeasAngle   = new QPushButton("Ölçüm — Açı  [N]");
    m_btnMeasROI     = new QPushButton("Ölçüm — ROI İstatistiği  [M]");

    for (auto *b : {m_btnPan, m_btnWL,
                    m_btnMeasDist, m_btnMeasEllipse,
                    m_btnMeasAngle, m_btnMeasROI})
        b->setCheckable(true);
    m_btnPan->setChecked(true);

    m_modeGroup = new QButtonGroup(this);
    m_modeGroup->setExclusive(true);
    m_modeGroup->addButton(m_btnPan,         0);
    m_modeGroup->addButton(m_btnWL,          1);
    m_modeGroup->addButton(m_btnMeasDist,    2);
    m_modeGroup->addButton(m_btnMeasEllipse, 3);
    m_modeGroup->addButton(m_btnMeasAngle,   4);
    m_modeGroup->addButton(m_btnMeasROI,     5);

    connect(m_modeGroup, &QButtonGroup::idClicked, this, &MainWindow::onModeChanged);

    // İnce ayırıcı çizgi
    auto *sep = new QFrame; sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet("color:#555;");

    mL->addWidget(m_btnPan);
    mL->addWidget(m_btnWL);
    mL->addWidget(sep);
    mL->addWidget(m_btnMeasDist);
    mL->addWidget(m_btnMeasEllipse);
    mL->addWidget(m_btnMeasAngle);
    mL->addWidget(m_btnMeasROI);
    vbox->addWidget(modeGroup);

    // --- Ölçüm ---
    auto *measGroup = new QGroupBox("Ölçüm", panel);
    auto *msL = new QVBoxLayout(measGroup);
    m_clearBtn = new QPushButton("Tüm Ölçümleri Sil");
    connect(m_clearBtn, &QPushButton::clicked, this, &MainWindow::clearMeasurements);
    m_clearSliceBtn = new QPushButton("Bu Dilimdeki Ölçümleri Sil");
    connect(m_clearSliceBtn, &QPushButton::clicked, this, &MainWindow::clearCurrentSliceMeasurements);
    msL->addWidget(m_clearBtn);
    msL->addWidget(m_clearSliceBtn);
    vbox->addWidget(measGroup);

    // --- Görüntü ---
    auto *imgGroup = new QGroupBox("Görüntü", panel);
    auto *iL = new QVBoxLayout(imgGroup);
    m_resetCamBtn = new QPushButton("Kamerayı Sıfırla");
    connect(m_resetCamBtn, &QPushButton::clicked, this, &MainWindow::resetCamera);

    m_overlayCheck = new QCheckBox("Overlay Göster");
    m_overlayCheck->setChecked(true);
    m_overlayCheck->setStyleSheet("QCheckBox { color:#ddd; }"
                                  "QCheckBox::indicator { width:13px; height:13px; }");
    connect(m_overlayCheck, &QCheckBox::toggled,
            m_viewer, &DicomImageViewer::setOverlayVisible);

    m_sliceLabel  = new QLabel("Dilim: -");
    m_sliceSlider = new QSlider(Qt::Horizontal);
    m_sliceSlider->setEnabled(false);
    connect(m_sliceSlider, &QSlider::valueChanged, m_viewer, &DicomImageViewer::setSlice);
    iL->addWidget(m_resetCamBtn);
    iL->addWidget(m_overlayCheck);
    iL->addWidget(m_sliceLabel);
    iL->addWidget(m_sliceSlider);

    // Dilim sıralama
    auto *sortLabel = new QLabel("Dilim Sıralaması:");
    sortLabel->setStyleSheet("color:#aaa; margin-top:4px;");
    iL->addWidget(sortLabel);
    m_sortIppBtn  = new QRadioButton("IPP (3D konum)");
    m_sortInstBtn = new QRadioButton("Instance Number");
    m_sortFileBtn = new QRadioButton("Dosya adı");
    m_sortIppBtn->setChecked(true);
    m_sortGroup = new QButtonGroup(this);
    m_sortGroup->addButton(m_sortIppBtn,  DicomImageViewer::SORT_IPP);
    m_sortGroup->addButton(m_sortInstBtn, DicomImageViewer::SORT_INSTANCE_NUMBER);
    m_sortGroup->addButton(m_sortFileBtn, DicomImageViewer::SORT_FILENAME);
    connect(m_sortGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_viewer->setSortOrder(static_cast<DicomImageViewer::SortOrder>(id));
    });
    iL->addWidget(m_sortIppBtn);
    iL->addWidget(m_sortInstBtn);
    iL->addWidget(m_sortFileBtn);
    vbox->addWidget(imgGroup);

    // --- W/L Presetleri ---
    auto *wlGroup = new QGroupBox("W/L Presetleri", panel);
    auto *wlGrid  = new QGridLayout(wlGroup);
    wlGrid->setSpacing(4);

    struct Preset { const char *label; double ww; double wc; };
    static const Preset presets[] = {
        { "Kemik",        1500,  300  },
        { "Akciğer",      1500, -600  },
        { "Yumuşak Doku",  400,   40  },
        { "Beyin",          80,   40  },
        { "Karaciğer",     150,   60  },
        { "Anjiyo",        600,  300  },
        { "Varsayılan",      0,    0  },
    };

    int col = 0, row = 0;
    for (const auto &p : presets) {
        auto *btn = new QPushButton(p.label);
        btn->setEnabled(false);
        btn->setCheckable(true);
        const double ww = p.ww, wc = p.wc;
        connect(btn, &QPushButton::clicked, this, [this, ww, wc]() {
            m_viewer->setWindowLevel(ww, wc);
        });
        wlGrid->addWidget(btn, row, col);
        m_wlPresetBtns.append(btn);
        col++;
        if (col == 2) { col = 0; row++; }
    }
    // Varsayılan butonu tek kaldıysa span 2
    if (col == 1) {
        wlGrid->removeWidget(m_wlPresetBtns.last());
        wlGrid->addWidget(m_wlPresetBtns.last(), row, 0, 1, 2);
    }
    vbox->addWidget(wlGroup);

    vbox->addStretch();

    auto *scroll = new QScrollArea(dock);
    scroll->setObjectName("controlScroll");
    scroll->setWidget(panel);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setObjectName("controlViewport");
    scroll->setStyleSheet("QScrollBar:vertical { background:#3c3c3c; width:8px; border-radius:4px; }"
                          "QScrollBar::handle:vertical { background:#666; border-radius:4px; min-height:20px; }"
                          "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }");
    dock->setWidget(scroll);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

void MainWindow::createStatusBar()
{
    m_statusMsg  = new QLabel("Hazır");
    m_distLabel  = new QLabel("");
    m_distLabel->setStyleSheet("color:#4fc3f7; font-weight:bold;");
    m_pixelLabel = new QLabel("");
    m_pixelLabel->setStyleSheet("color:#90ee90; font-family:monospace; margin-right:8px;");
    m_wlLabel    = new QLabel("");
    m_wlLabel->setStyleSheet("color:#aaa; margin-right:8px;");
    statusBar()->addWidget(m_statusMsg, 1);
    statusBar()->addPermanentWidget(m_distLabel);
    statusBar()->addPermanentWidget(m_pixelLabel);
    statusBar()->addPermanentWidget(m_wlLabel);
}

// ---- Slotlar ----

void MainWindow::openDicomFile()
{
    QFileDialog dlg(this, "DICOM Dosyası Seç");
    dlg.setFileMode(QFileDialog::ExistingFile);
    dlg.setNameFilter("DICOM (*.dcm *.DCM *.dicom);;Tüm Dosyalar (*)");
    dlg.setOption(QFileDialog::DontUseNativeDialog, true);
    if (dlg.exec() != QDialog::Accepted) return;
    m_viewer->loadFile(dlg.selectedFiles().first());
}

void MainWindow::openTciaBrowser()
{
    auto *dlg = new TciaBrowser(this);
    connect(dlg, &TciaBrowser::directoryReady,
            this, [this](const QString &path) {
        m_viewer->loadDirectory(path);
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->exec();
}

void MainWindow::openDicomDirectory()
{
    QFileDialog dlg(this, "DICOM Klasörü Seç");
    dlg.setFileMode(QFileDialog::Directory);
    dlg.setOption(QFileDialog::ShowDirsOnly,        true);
    dlg.setOption(QFileDialog::DontUseNativeDialog, true);
    if (dlg.exec() != QDialog::Accepted) return;
    m_viewer->loadDirectory(dlg.selectedFiles().first());
}

void MainWindow::onModeChanged(int id)
{
    static const struct { int vtkMode; MeasurementWidget::MeasurementType measType; const char *msg; }
    table[] = {
        {0, MeasurementWidget::MEAS_DISTANCE, "Kaydır modu"},
        {1, MeasurementWidget::MEAS_DISTANCE, "Parlaklık/Kontrast modu"},
        {2, MeasurementWidget::MEAS_DISTANCE, "Mesafe: 1. noktayı tıkla"},
        {2, MeasurementWidget::MEAS_ELLIPSE,  "Elips: merkezi tıkla, 2. tıkla kenar"},
        {2, MeasurementWidget::MEAS_ANGLE,    "Açı: 1. ucu tıkla (3 tık gerekli)"},
        {2, MeasurementWidget::MEAS_ROI,      "ROI: sol üst köşeyi tıkla"},
    };
    if (id < 0 || id > 5) return;
    m_viewer->setMode(table[id].vtkMode);
    if (id >= 2)
        m_viewer->measurementWidget()->setMeasurementType(table[id].measType);
    m_statusMsg->setText(table[id].msg);
}

void MainWindow::clearMeasurements()
{
    m_viewer->measurementWidget()->clear();
    m_distLabel->setText("");
    m_statusMsg->setText("Tüm ölçümler silindi");
}

void MainWindow::clearCurrentSliceMeasurements()
{
    m_viewer->measurementWidget()->clearCurrentSlice();
    m_distLabel->setText("");
    m_statusMsg->setText(QString("Dilim %1 ölçümleri silindi").arg(m_viewer->currentSlice()));
}

void MainWindow::onMeasurementDone(double val)
{
    QString txt;
    const QString unit = (m_viewer->modality() == "CT") ? " HU" : "";
    switch (m_viewer->measurementWidget()->currentType()) {
    case MeasurementWidget::MEAS_DISTANCE:
        txt = QString("Mesafe: %1 mm").arg(val, 0, 'f', 2);
        m_statusMsg->setText("Mesafe ölçüldü. Yeni ölçüm için tıkla.");
        break;
    case MeasurementWidget::MEAS_ELLIPSE:
        txt = QString("Elips: %1 cm²").arg(val / 100.0, 0, 'f', 2);
        m_statusMsg->setText("Elips ölçüldü. Yeni ölçüm için tıkla.");
        break;
    case MeasurementWidget::MEAS_ANGLE:
        txt = QString("Açı: %1°").arg(val, 0, 'f', 1);
        m_statusMsg->setText("Açı ölçüldü. Yeni ölçüm için tıkla.");
        break;
    case MeasurementWidget::MEAS_ROI:
        txt = QString("ROI Ort: %1%2").arg(static_cast<int>(std::round(val))).arg(unit);
        m_statusMsg->setText("ROI hesaplandı. Yeni ölçüm için tıkla.");
        break;
    }
    m_distLabel->setText(txt);
}

void MainWindow::onImageLoaded(int minSlice, int maxSlice)
{
    m_sliceSlider->setRange(minSlice, maxSlice);
    m_sliceSlider->setValue(minSlice);
    m_sliceSlider->setEnabled(maxSlice > minSlice);
    m_sliceLabel->setText(QString("Dilim: %1 / %2").arg(minSlice).arg(maxSlice));
    m_statusMsg->setText("Görüntü yüklendi");
    for (auto *btn : m_wlPresetBtns)
        btn->setEnabled(true);
}

void MainWindow::onWindowLevelChanged(double window, double level)
{
    m_wlLabel->setText(QString("W: %1  L: %2")
                       .arg(static_cast<int>(window))
                       .arg(static_cast<int>(level)));

    // Sağ panel preset butonlarını güncelle
    struct Preset { double ww; double wc; };
    static const Preset kPresets[] = {
        { 1500,  300 }, // Kemik
        { 1500, -600 }, // Akciğer
        {  400,   40 }, // Yumuşak Doku
        {   80,   40 }, // Beyin
        {  150,   60 }, // Karaciğer
        {  600,  300 }, // Anjiyo
        {    0,    0 }, // Varsayılan (sentinel)
    };
    const int n = static_cast<int>(m_wlPresetBtns.size());
    for (int i = 0; i < n; ++i) {
        const bool match = (std::abs(window - kPresets[i].ww) < 0.5 &&
                            std::abs(level  - kPresets[i].wc) < 0.5);
        m_wlPresetBtns[i]->setChecked(match);
    }
}

void MainWindow::onPixelInfoChanged(double /*x*/, double /*y*/, double value)
{
    if (std::isnan(value)) {
        m_pixelLabel->setText("");
        return;
    }
    const QString unit = (m_viewer->modality() == "CT") ? "HU" : "Val";
    m_pixelLabel->setText(QString("%1: %2").arg(unit).arg(static_cast<int>(value)));
}

void MainWindow::onSliceChanged(int slice)
{
    m_sliceSlider->blockSignals(true);
    m_sliceSlider->setValue(slice);
    m_sliceSlider->blockSignals(false);
    m_sliceLabel->setText(QString("Dilim: %1 / %2").arg(slice).arg(m_viewer->maxSlice()));
}

void MainWindow::resetCamera()
{
    m_viewer->resetCamera();
}
