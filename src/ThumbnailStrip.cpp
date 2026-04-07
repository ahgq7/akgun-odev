#include "ThumbnailStrip.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QImage>
#include <QPixmap>
#include <QEvent>
#include <QMouseEvent>
#include <QScrollBar>

#include <vtkImageData.h>

#include <algorithm>
#include <cmath>

ThumbnailStrip::ThumbnailStrip(QWidget *parent)
    : QScrollArea(parent)
{
    setObjectName("thumbStrip");
    setStyleSheet(
        "QScrollArea#thumbStrip  { background:#1a1a1a; border:none; }"
        "QWidget#thumbContainer  { background:#1a1a1a; }"
        "QScrollBar:vertical     { background:#1a1a1a; width:6px; border-radius:3px; }"
        "QScrollBar::handle:vertical { background:#444; border-radius:3px; min-height:20px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }");

    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setWidgetResizable(true);
    setFixedWidth(96);

    m_container = new QWidget;
    m_container->setObjectName("thumbContainer");
    m_layout = new QVBoxLayout(m_container);
    m_layout->setContentsMargins(4, 4, 4, 4);
    m_layout->setSpacing(4);
    m_layout->addStretch();
    setWidget(m_container);

    m_loadTimer = new QTimer(this);
    m_loadTimer->setInterval(16); // ~60 fps batch loading
    connect(m_loadTimer, &QTimer::timeout, this, &ThumbnailStrip::loadNextBatch);
}

void ThumbnailStrip::clear()
{
    m_loadTimer->stop();
    m_loadedIdx   = 0;
    m_activeThumb = -1;
    m_imageData   = nullptr;

    for (QLabel *lbl : m_labels) {
        m_layout->removeWidget(lbl);
        lbl->deleteLater();
    }
    m_labels.clear();
    m_sliceIndices.clear();
}

void ThumbnailStrip::setImageData(vtkImageData *data, double ww, double wl)
{
    clear();
    if (!data) return;

    int dims[3];
    data->GetDimensions(dims);
    int nz = dims[2];
    if (nz <= 1) return; // single-slice: keep strip hidden (caller hides widget)

    m_imageData = data;
    m_ww = (ww != 0.0) ? ww : 400.0;
    m_wl = wl;

    // 1 thumbnail per slice — scroll area handles the height
    m_stride = 1;
    int numThumbs = nz;

    // Remove the trailing stretch before inserting labels
    QLayoutItem *stretchItem = m_layout->takeAt(m_layout->count() - 1);

    m_labels.reserve(numThumbs);
    m_sliceIndices.reserve(numThumbs);

    for (int i = 0; i < numThumbs; ++i) {
        int sliceZ = std::min(i * m_stride, nz - 1);
        m_sliceIndices.append(sliceZ);

        auto *lbl = new QLabel(m_container);
        lbl->setFixedSize(80, 80);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setCursor(Qt::PointingHandCursor);
        lbl->setStyleSheet("border:1px solid #333; background:#111;");
        lbl->installEventFilter(this);

        m_layout->addWidget(lbl, 0, Qt::AlignHCenter);
        m_labels.append(lbl);
    }

    // Re-add stretch at the bottom
    if (stretchItem)
        delete stretchItem;
    m_layout->addStretch();

    m_loadedIdx = 0;
    m_loadTimer->start();
}

void ThumbnailStrip::setActiveSlice(int slice)
{
    if (m_sliceIndices.isEmpty()) return;

    // Find the thumb whose sliceIndex is closest to requested slice
    int newIdx = slice / m_stride;
    newIdx = std::max(0, std::min(newIdx, (int)m_labels.size() - 1));

    if (newIdx == m_activeThumb) return;

    applyBorder(m_activeThumb, false);
    m_activeThumb = newIdx;
    applyBorder(m_activeThumb, true);

    if (m_activeThumb >= 0 && m_activeThumb < (int)m_labels.size())
        ensureWidgetVisible(m_labels[m_activeThumb]);
}

bool ThumbnailStrip::eventFilter(QObject *obj, QEvent *ev)
{
    if (ev->type() == QEvent::MouseButtonRelease) {
        int idx = m_labels.indexOf(static_cast<QLabel *>(obj));
        if (idx >= 0 && idx < (int)m_sliceIndices.size())
            emit sliceClicked(m_sliceIndices[idx]);
    }
    return QScrollArea::eventFilter(obj, ev);
}

void ThumbnailStrip::loadNextBatch()
{
    if (!m_imageData) { m_loadTimer->stop(); return; }

    const int BATCH = 5;
    int end = std::min(m_loadedIdx + BATCH, (int)m_labels.size());

    for (int i = m_loadedIdx; i < end; ++i)
        m_labels[i]->setPixmap(buildThumb(m_sliceIndices[i]));

    m_loadedIdx = end;
    if (m_loadedIdx >= (int)m_labels.size())
        m_loadTimer->stop();
}

QPixmap ThumbnailStrip::buildThumb(int sliceZ) const
{
    if (!m_imageData) return {};

    int dims[3];
    m_imageData->GetDimensions(dims);
    int w = dims[0], h = dims[1];
    if (w <= 0 || h <= 0) return {};

    const int TW = 80, TH = 80;
    QImage img(TW, TH, QImage::Format_Grayscale8);

    double wLow = m_wl - m_ww / 2.0;

    for (int ty = 0; ty < TH; ++ty) {
        // Map thumb row → source row (nearest-neighbor, clamped)
        int j = (TH > 1) ? ((ty * (h - 1)) / (TH - 1)) : 0;
        j = std::max(0, std::min(j, h - 1));
        uchar *line = img.scanLine(ty);

        for (int tx = 0; tx < TW; ++tx) {
            int i = (TW > 1) ? ((tx * (w - 1)) / (TW - 1)) : 0;
            i = std::max(0, std::min(i, w - 1));

            double val = m_imageData->GetScalarComponentAsDouble(i, j, sliceZ, 0);
            double norm = (val - wLow) / m_ww;
            int c = static_cast<int>(norm * 255.0 + 0.5);
            line[tx] = static_cast<uchar>(std::max(0, std::min(255, c)));
        }
    }

    return QPixmap::fromImage(img);
}

void ThumbnailStrip::applyBorder(int idx, bool active)
{
    if (idx < 0 || idx >= (int)m_labels.size()) return;
    if (active)
        m_labels[idx]->setStyleSheet("border:2px solid #4fc3f7; background:#111;");
    else
        m_labels[idx]->setStyleSheet("border:1px solid #333; background:#111;");
}
