#pragma once

#include <QMainWindow>
#include <QList>
#include "DicomImageViewer.h"
#include "MeasurementWidget.h"

class QLabel;
class QSlider;
class QPushButton;
class QButtonGroup;
class QGroupBox;
class QAction;
class QRadioButton;
class QCheckBox;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

private slots:
    void openDicomFile();
    void openDicomDirectory();
    void openTciaBrowser();
    void onModeChanged(int mode);
    void clearMeasurements();
    void clearCurrentSliceMeasurements();
    void onMeasurementDone(double mm);
    void onImageLoaded(int minSlice, int maxSlice);
    void onSliceChanged(int slice);
    void onWindowLevelChanged(double window, double level);
    void onPixelInfoChanged(double x, double y, double value);
    void resetCamera();

private:
    void createActions();
    void createMenuBar();
    void createCentralWidget();
    void createControlPanel();
    void createStatusBar();

    DicomImageViewer *m_viewer = nullptr;

    QPushButton  *m_openFileBtn      = nullptr;
    QPushButton  *m_openDirBtn      = nullptr;
    QPushButton  *m_btnPan          = nullptr;  // mod: kaydır
    QPushButton  *m_btnWL           = nullptr;  // mod: parlaklık/kontrast
    QPushButton  *m_btnMeasDist     = nullptr;  // mod: mesafe
    QPushButton  *m_btnMeasEllipse  = nullptr;  // mod: elips
    QPushButton  *m_btnMeasAngle    = nullptr;  // mod: açı
    QPushButton  *m_btnMeasROI      = nullptr;  // mod: ROI
    QPushButton  *m_clearBtn          = nullptr;
    QPushButton  *m_clearSliceBtn     = nullptr;
    QPushButton  *m_resetCamBtn      = nullptr;
    QButtonGroup  *m_modeGroup    = nullptr;
    QSlider       *m_sliceSlider  = nullptr;
    QLabel        *m_sliceLabel   = nullptr;
    QRadioButton  *m_sortIppBtn   = nullptr;
    QRadioButton  *m_sortInstBtn  = nullptr;
    QRadioButton  *m_sortFileBtn  = nullptr;
    QButtonGroup  *m_sortGroup    = nullptr;

    QLabel    *m_statusMsg   = nullptr;
    QLabel    *m_distLabel   = nullptr;
    QLabel    *m_wlLabel     = nullptr;
    QLabel    *m_pixelLabel  = nullptr;
    QCheckBox *m_overlayCheck = nullptr;

    QList<QPushButton*> m_wlPresetBtns;

    QAction *m_actOpenFile  = nullptr;
    QAction *m_actOpenDir   = nullptr;
    QAction *m_actTcia      = nullptr;
    QAction *m_actExit      = nullptr;
};
