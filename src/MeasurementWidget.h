#pragma once

#include <QObject>
#include <vector>

#include <vtkSmartPointer.h>
#include <vtkRenderer.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkActor.h>
#include <vtkTextActor.h>
#include <vtkCallbackCommand.h>
#include <vtkImageData.h>

class MeasurementWidget : public QObject
{
    Q_OBJECT
public:
    enum MeasurementType { MEAS_DISTANCE=0, MEAS_ELLIPSE, MEAS_ANGLE, MEAS_ROI };

    explicit MeasurementWidget(QObject *parent = nullptr);

    void setRenderer(vtkRenderer *renderer);
    void setPixelSpacing(double sx, double sy);

    void setCurrentSlice(int slice);
    void setCurrentSliceZ(double worldZ);
    void setMeasurementType(MeasurementType t);
    MeasurementType currentType() const { return m_measType; }

    void handleClick(double worldX, double worldY);
    void updatePreview(double worldX, double worldY);

    void clear();
    void clearCurrentSlice();
    void enable(bool on);
    bool isEnabled() const { return m_enabled; }

    void cancelCurrent(); // ESC — devam eden çizimi iptal et
    void undo();          // Ctrl+Z — son ölçümü geri al
    void redo();          // Ctrl+Y — geri alınanı ileri al

    // HU+flip uygulanmış display image — ROI istatistiği için
    vtkImageData *ImageDataRef = nullptr;

signals:
    void measurementDone(double value); // mm / mm² / derece / ortalama

private:
    struct Measurement {
        MeasurementType type;
        std::vector<vtkSmartPointer<vtkActor>> actors;
        vtkSmartPointer<vtkTextActor> labelActor;
        double midWorld[2];
        int    sliceIndex;
    };

    void cancelPending();
    void finalizeDistance(double x1,double y1,double x2,double y2);
    void finalizeEllipse (double cx,double cy,double ex,double ey);
    void finalizeAngle   (double ax,double ay,double vx,double vy,double bx,double by);
    void finalizeROI     (double x1,double y1,double x2,double y2);
    void computeROIStats (double wx1,double wy1,double wx2,double wy2,
                          double &mean,double &stddev,double &minV,double &maxV,int &N);
    void updateLabelPositions();
    double sliceZ() const;

    static vtkSmartPointer<vtkActor> buildActor(vtkPolyData *pd,
                                                 double r,double g,double b,
                                                 float lw = 2.0f);
    static void fillLine   (vtkPolyData *pd,double x1,double y1,double x2,double y2,double z);
    static void fillEllipse(vtkPolyData *pd,double cx,double cy,double a,double b,double z);
    static void fillRect   (vtkPolyData *pd,double x1,double y1,double x2,double y2,double z);
    static void fillArc    (vtkPolyData *pd,double cx,double cy,
                             double th1,double dth,double r,double z);

    vtkSmartPointer<vtkTextActor> makeLabel(double r,double g,double b,int sz=16);

    vtkRenderer     *m_renderer       = nullptr;
    double           m_spacingX       = 1.0;
    double           m_spacingY       = 1.0;
    bool             m_enabled        = false;
    int              m_currentSlice   = 0;
    double           m_currentSliceZ  = 0.0;
    MeasurementType  m_measType       = MEAS_DISTANCE;
    int              m_clickCount     = 0;
    double           m_pts[3][2]      = {};

    // Önizleme
    vtkSmartPointer<vtkPolyData> m_prevPolyData;
    vtkSmartPointer<vtkActor>    m_prevActor;
    // Açı ölçümünde 1. kolu önizleme sırasında kalıcı göster
    vtkSmartPointer<vtkPolyData> m_arm1PolyData;
    vtkSmartPointer<vtkActor>    m_arm1Actor;

    std::vector<Measurement> m_measurements;
    std::vector<Measurement> m_redoStack;
    vtkSmartPointer<vtkCallbackCommand> m_renderCallback;
};
