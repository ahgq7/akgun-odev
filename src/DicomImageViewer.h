#pragma once

#include <QWidget>
#include <QEvent>
#include <QString>
#include <vector>

#include <vtkSmartPointer.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkImageViewer2.h>
#include <vtkInteractorStyleImage.h>
#include <vtkTextActor.h>

class QVTKOpenGLNativeWidget;
class QSlider;
class QLabel;
class QComboBox;
class QPushButton;
class MeasurementWidget;
class DicomInteractorStyle;
class ThumbnailStrip;

class DicomImageViewer : public QWidget
{
    Q_OBJECT

public:
    enum SortOrder { SORT_IPP = 0, SORT_INSTANCE_NUMBER, SORT_FILENAME };

    struct OverlayInfo {
        QString patientName, patientID, patientBirthDate, patientSex;
        QString institutionName, studyDate, studyTime, modality, seriesDescription;
        QString sliceThickness;
        
        std::vector<double> slicePositionsZ;
        
        double singleSliceZ    = 0.0;
        bool   hasSingleSliceZ = false;
        
        QString orientRight, orientLeft, orientTop, orientBottom;
    };

    explicit DicomImageViewer(QWidget *parent = nullptr);
    ~DicomImageViewer() override = default;

    bool loadFile(const QString &filePath);
    bool loadDirectory(const QString &dirPath);

    MeasurementWidget *measurementWidget() const { return m_measureWidget; }

    int     currentSlice() const;
    int     minSlice()     const;
    int     maxSlice()     const;
    QString modality()     const { return m_overlay.modality; }

public slots:
    void setSlice(int slice);
    void resetCamera();
    void setMode(int mode);                        // 0=pan, 1=W/L, 2=ölçüm
    void setSortOrder(SortOrder order);
    void setWindowLevel(double window, double level); // window=0,level=0 → auto
    void setOverlayVisible(bool visible);

signals:
    void imageLoaded(int minSlice, int maxSlice);
    void sliceChanged(int slice);
    void sortOrderChanged(SortOrder order);
    void windowLevelChanged(double window, double level);
    void pixelInfoChanged(double worldX, double worldY, double value);
    void modeChangeRequested(int modeId); // klavye kısayolundan mod değişimi

private:
    void setupVTK();
    void setupLayout();
    bool applyImage();
    void updateMeasureSlice(int slice);
    void updateOverlay();
    void updatePresetHighlight(double w, double l);
    bool eventFilter(QObject *obj, QEvent *ev) override;

    QVTKOpenGLNativeWidget *m_vtkWidget          = nullptr;
    QSlider                *m_sliceSlider   = nullptr;
    QComboBox              *m_sortCombo     = nullptr;
    QLabel                 *m_infoLabel     = nullptr;
    MeasurementWidget      *m_measureWidget = nullptr;
    ThumbnailStrip         *m_thumbStrip    = nullptr;
    QList<QPushButton*>     m_bottomPresetBtns;

    QString   m_lastDirPath;
    SortOrder m_sortOrder = SORT_IPP;

    vtkSmartPointer<vtkGenericOpenGLRenderWindow> m_renderWindow;
    vtkSmartPointer<vtkImageViewer2>              m_imageViewer;
    vtkSmartPointer<vtkImageData>                 m_imageData;
    vtkSmartPointer<vtkImageData>                 m_displayImageData;
    vtkSmartPointer<DicomInteractorStyle>         m_style;

    // Overlay text actors (4 köşe)
    vtkSmartPointer<vtkTextActor> m_overlayTL; // Sol Üst
    vtkSmartPointer<vtkTextActor> m_overlayTR; // Sağ Üst
    vtkSmartPointer<vtkTextActor> m_overlayBL; // Sol Alt
    vtkSmartPointer<vtkTextActor> m_overlayBR; // Sağ Alt

    // Yönelim işaretleri (kenar ortaları)
    vtkSmartPointer<vtkTextActor> m_orientTop;
    vtkSmartPointer<vtkTextActor> m_orientBottom;
    vtkSmartPointer<vtkTextActor> m_orientLeft;
    vtkSmartPointer<vtkTextActor> m_orientRight;

    OverlayInfo m_overlay;
    bool        m_overlayVisible = true;

    double m_spacingX         = 1.0;
    double m_spacingY         = 1.0;
    double m_rescaleSlope     = 1.0;
    double m_rescaleIntercept = 0.0;

    bool m_imageLoaded = false;
};
