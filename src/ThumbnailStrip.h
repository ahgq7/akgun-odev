#pragma once

#include <QScrollArea>
#include <QVector>

class QWidget;
class QVBoxLayout;
class QLabel;
class QTimer;
class QEvent;
class vtkImageData;

class ThumbnailStrip : public QScrollArea
{
    Q_OBJECT
public:
    explicit ThumbnailStrip(QWidget *parent = nullptr);

    // data: HU-applied + Y-flipped display image from DicomImageViewer
    void setImageData(vtkImageData *data, double ww, double wl);
    void setActiveSlice(int slice);
    void clear();

signals:
    void sliceClicked(int slice);

protected:
    bool eventFilter(QObject *obj, QEvent *ev) override;

private slots:
    void loadNextBatch();

private:
    QWidget     *m_container   = nullptr;
    QVBoxLayout *m_layout      = nullptr;
    QTimer      *m_loadTimer   = nullptr;

    vtkImageData *m_imageData   = nullptr;
    double        m_ww          = 400.0;
    double        m_wl          = 40.0;
    int           m_stride      = 1;
    int           m_loadedIdx   = 0;   // next thumb index to render
    int           m_activeThumb = -1;  // currently highlighted thumb index

    QVector<QLabel *> m_labels;
    QVector<int>      m_sliceIndices;  // actual slice z-index for each thumb

    QPixmap buildThumb(int sliceZ) const;
    void    applyBorder(int idx, bool active);
};
