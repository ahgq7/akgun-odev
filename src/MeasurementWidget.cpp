#include "MeasurementWidget.h"

#include <vtkNew.h>
#include <vtkPoints.h>
#include <vtkCellArray.h>
#include <vtkProperty.h>
#include <vtkTextProperty.h>
#include <vtkRenderWindow.h>

#include <cmath>
#include <cstdio>
#include <algorithm>
#include <vector>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

vtkSmartPointer<vtkActor>
MeasurementWidget::buildActor(vtkPolyData *pd, double r, double g, double b, float lw)
{
    auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
    mapper->SetInputData(pd);
    auto actor = vtkSmartPointer<vtkActor>::New();
    actor->SetMapper(mapper);
    actor->GetProperty()->SetColor(r, g, b);
    actor->GetProperty()->SetLineWidth(lw);
    return actor;
}

void MeasurementWidget::fillLine(vtkPolyData *pd,
                                  double x1,double y1,double x2,double y2,double z)
{
    auto pts   = vtkSmartPointer<vtkPoints>::New();
    auto cells = vtkSmartPointer<vtkCellArray>::New();
    pts->InsertNextPoint(x1, y1, z);
    pts->InsertNextPoint(x2, y2, z);
    vtkIdType ids[2] = {0, 1};
    cells->InsertNextCell(2, ids);
    pd->SetPoints(pts);
    pd->SetLines(cells);
    pd->Modified();
}

void MeasurementWidget::fillEllipse(vtkPolyData *pd,
                                     double cx,double cy,double a,double b,double z)
{
    const int N = 64;
    auto pts   = vtkSmartPointer<vtkPoints>::New();
    auto cells = vtkSmartPointer<vtkCellArray>::New();
    for (int i = 0; i <= N; ++i) {
        double ang = 2.0 * M_PI * i / N;
        pts->InsertNextPoint(cx + a*std::cos(ang), cy + b*std::sin(ang), z);
    }
    for (int i = 0; i < N; ++i) {
        vtkIdType ids[2] = {(vtkIdType)i, (vtkIdType)(i+1)};
        cells->InsertNextCell(2, ids);
    }
    pd->SetPoints(pts);
    pd->SetLines(cells);
    pd->Modified();
}

void MeasurementWidget::fillRect(vtkPolyData *pd,
                                  double x1,double y1,double x2,double y2,double z)
{
    auto pts   = vtkSmartPointer<vtkPoints>::New();
    auto cells = vtkSmartPointer<vtkCellArray>::New();
    pts->InsertNextPoint(x1, y1, z);
    pts->InsertNextPoint(x2, y1, z);
    pts->InsertNextPoint(x2, y2, z);
    pts->InsertNextPoint(x1, y2, z);
    vtkIdType edges[4][2] = {{0,1},{1,2},{2,3},{3,0}};
    for (auto &e : edges) cells->InsertNextCell(2, e);
    pd->SetPoints(pts);
    pd->SetLines(cells);
    pd->Modified();
}

// Açı yayı: merkez (cx,cy), theta1'den dtheta kadar yay, yarıçap r
void MeasurementWidget::fillArc(vtkPolyData *pd,
                                 double cx,double cy,
                                 double th1,double dth,double r,double z)
{
    const int N = 32;
    auto pts   = vtkSmartPointer<vtkPoints>::New();
    auto cells = vtkSmartPointer<vtkCellArray>::New();
    for (int i = 0; i <= N; ++i) {
        double t = th1 + dth * i / N;
        pts->InsertNextPoint(cx + r*std::cos(t), cy + r*std::sin(t), z);
    }
    for (int i = 0; i < N; ++i) {
        vtkIdType ids[2] = {(vtkIdType)i, (vtkIdType)(i+1)};
        cells->InsertNextCell(2, ids);
    }
    pd->SetPoints(pts);
    pd->SetLines(cells);
    pd->Modified();
}

vtkSmartPointer<vtkTextActor>
MeasurementWidget::makeLabel(double r, double g, double b, int sz)
{
    auto label = vtkSmartPointer<vtkTextActor>::New();
    label->GetTextProperty()->SetFontSize(sz);
    label->GetTextProperty()->SetColor(r, g, b);
    label->GetTextProperty()->SetBold(1);
    label->GetTextProperty()->SetShadow(1);
    label->GetTextProperty()->SetShadowOffset(1,-1);
    label->GetTextProperty()->SetJustificationToCentered();
    return label;
}

MeasurementWidget::MeasurementWidget(QObject *parent)
    : QObject(parent)
{
    m_prevPolyData = vtkSmartPointer<vtkPolyData>::New();
    m_prevActor    = buildActor(m_prevPolyData, 1.0, 1.0, 0.0);
    m_prevActor->GetProperty()->SetLineStipplePattern(0xAAAA);
    m_prevActor->GetProperty()->SetLineStippleRepeatFactor(1);
    m_prevActor->VisibilityOff();

    m_arm1PolyData = vtkSmartPointer<vtkPolyData>::New();
    m_arm1Actor    = buildActor(m_arm1PolyData, 1.0, 0.8, 0.0);
    m_arm1Actor->GetProperty()->SetLineStipplePattern(0xAAAA);
    m_arm1Actor->VisibilityOff();
}

void MeasurementWidget::setRenderer(vtkRenderer *renderer)
{
    m_renderer = renderer;
    if (!m_renderer) return;
    m_renderer->AddActor(m_prevActor);
    m_renderer->AddActor(m_arm1Actor);

    m_renderCallback = vtkSmartPointer<vtkCallbackCommand>::New();
    m_renderCallback->SetClientData(this);
    m_renderCallback->SetCallback([](vtkObject*, unsigned long, void *cd, void*) {
        static_cast<MeasurementWidget*>(cd)->updateLabelPositions();
    });
    m_renderer->AddObserver(vtkCommand::StartEvent, m_renderCallback);
}

void MeasurementWidget::setPixelSpacing(double sx, double sy)
{
    m_spacingX = (sx > 0) ? sx : 1.0;
    m_spacingY = (sy > 0) ? sy : 1.0;
}

void MeasurementWidget::setMeasurementType(MeasurementType t)
{
    if (m_measType == t) return;
    cancelPending();
    m_measType = t;
}

void MeasurementWidget::setCurrentSliceZ(double z) { m_currentSliceZ = z; }
double MeasurementWidget::sliceZ() const { return m_currentSliceZ; }

void MeasurementWidget::setCurrentSlice(int slice)
{
    if (slice == m_currentSlice) return;
    cancelPending();
    m_currentSlice = slice;
    for (auto &m : m_measurements) {
        bool vis = (m.sliceIndex == slice);
        for (auto &a : m.actors) a->SetVisibility(vis ? 1 : 0);
        m.labelActor->SetVisibility(vis ? 1 : 0);
    }
    if (m_renderer) m_renderer->GetRenderWindow()->Render();
}

void MeasurementWidget::enable(bool on)
{
    m_enabled = on;
    if (!on) {
        cancelPending();
        if (m_renderer) m_renderer->GetRenderWindow()->Render();
    }
}

void MeasurementWidget::cancelPending()
{
    m_clickCount = 0;
    m_prevActor->VisibilityOff();
    m_arm1Actor->VisibilityOff();
}

void MeasurementWidget::cancelCurrent()
{
    if (m_clickCount == 0) return;
    cancelPending();
    if (m_renderer) m_renderer->GetRenderWindow()->Render();
}

void MeasurementWidget::undo()
{
    cancelPending();
    if (m_measurements.empty()) return;
    auto &last = m_measurements.back();
    if (m_renderer) {
        for (auto &a : last.actors)  m_renderer->RemoveActor(a);
        m_renderer->RemoveViewProp(last.labelActor);
    }
    m_redoStack.push_back(std::move(last));
    m_measurements.pop_back();
    if (m_renderer) m_renderer->GetRenderWindow()->Render();
}

void MeasurementWidget::redo()
{
    if (m_redoStack.empty()) return;
    cancelPending();
    auto &top = m_redoStack.back();
    if (m_renderer) {
        bool vis = (top.sliceIndex == m_currentSlice);
        for (auto &a : top.actors) {
            m_renderer->AddActor(a);
            a->SetVisibility(vis ? 1 : 0);
        }
        top.labelActor->SetVisibility(vis ? 1 : 0);
        m_renderer->AddViewProp(top.labelActor);
    }
    m_measurements.push_back(std::move(top));
    m_redoStack.pop_back();
    if (m_renderer) m_renderer->GetRenderWindow()->Render();
}

void MeasurementWidget::handleClick(double worldX, double worldY)
{
    if (!m_enabled) return;

    m_pts[m_clickCount][0] = worldX;
    m_pts[m_clickCount][1] = worldY;
    ++m_clickCount;

    double z = sliceZ();
    int needed = (m_measType == MEAS_ANGLE) ? 3 : 2;

    if (m_clickCount == 1) {
        // İlk tık: önizlemeyi başlat
        switch (m_measType) {
        case MEAS_DISTANCE:
            fillLine(m_prevPolyData, worldX,worldY,worldX,worldY,z); break;
        case MEAS_ELLIPSE:
            fillEllipse(m_prevPolyData, worldX,worldY,1e-6,1e-6,z); break;
        case MEAS_ROI:
            fillRect(m_prevPolyData, worldX,worldY,worldX,worldY,z); break;
        case MEAS_ANGLE:
            fillLine(m_prevPolyData, worldX,worldY,worldX,worldY,z); break;
        }
        m_prevActor->VisibilityOn();

    } else if (m_clickCount == 2 && m_measType == MEAS_ANGLE) {
        // Açı 2. tık: kol1'i kilitle, kol2 önizlemesi için sıfırla
        fillLine(m_arm1PolyData, m_pts[0][0],m_pts[0][1], worldX,worldY,z);
        m_arm1Actor->VisibilityOn();
        fillLine(m_prevPolyData, worldX,worldY, worldX,worldY, z);
    }

    // Yeterli tık toplandıysa finalizeye geç
    if (m_clickCount == needed) {
        m_clickCount = 0;
        m_prevActor->VisibilityOff();
        m_arm1Actor->VisibilityOff();
        switch (m_measType) {
        case MEAS_DISTANCE:
            finalizeDistance(m_pts[0][0],m_pts[0][1], m_pts[1][0],m_pts[1][1]); break;
        case MEAS_ELLIPSE:
            finalizeEllipse(m_pts[0][0],m_pts[0][1], m_pts[1][0],m_pts[1][1]);  break;
        case MEAS_ANGLE:
            finalizeAngle(m_pts[0][0],m_pts[0][1],
                           m_pts[1][0],m_pts[1][1],
                           m_pts[2][0],m_pts[2][1]);                             break;
        case MEAS_ROI:
            finalizeROI(m_pts[0][0],m_pts[0][1], m_pts[1][0],m_pts[1][1]);      break;
        }
    }
}

void MeasurementWidget::updatePreview(double worldX, double worldY)
{
    if (!m_enabled || m_clickCount == 0) return;
    double z = sliceZ();

    switch (m_measType) {
    case MEAS_DISTANCE:
        fillLine(m_prevPolyData, m_pts[0][0],m_pts[0][1], worldX,worldY, z);
        break;
    case MEAS_ELLIPSE: {
        double a = std::abs(worldX - m_pts[0][0]);
        double b = std::abs(worldY - m_pts[0][1]);
        fillEllipse(m_prevPolyData, m_pts[0][0],m_pts[0][1],
                    std::max(a,1e-6), std::max(b,1e-6), z);
        break;
    }
    case MEAS_ROI:
        fillRect(m_prevPolyData, m_pts[0][0],m_pts[0][1], worldX,worldY, z);
        break;
    case MEAS_ANGLE:
        if (m_clickCount == 1)
            fillLine(m_prevPolyData, m_pts[0][0],m_pts[0][1], worldX,worldY, z);
        else  // m_clickCount == 2: kol2 önizlemesi
            fillLine(m_prevPolyData, m_pts[1][0],m_pts[1][1], worldX,worldY, z);
        break;
    }
    m_prevPolyData->Modified();
    if (m_renderer) m_renderer->GetRenderWindow()->Render();
}

void MeasurementWidget::finalizeDistance(double x1,double y1,double x2,double y2)
{
    if (!m_renderer) return;
    m_redoStack.clear();
    double dx = (x2-x1)*m_spacingX, dy = (y2-y1)*m_spacingY;
    double mm = std::sqrt(dx*dx + dy*dy);

    double z = sliceZ();
    auto pd = vtkSmartPointer<vtkPolyData>::New();
    fillLine(pd, x1,y1, x2,y2, z);
    auto actor = buildActor(pd, 1.0, 0.35, 0.0);
    m_renderer->AddActor(actor);

    char buf[64]; snprintf(buf,sizeof(buf),"%.2f mm",mm);
    auto label = makeLabel(1.0,1.0,0.0);
    label->SetInput(buf);
    m_renderer->AddActor(label);

    Measurement meas;
    meas.type = MEAS_DISTANCE;
    meas.actors = {actor};
    meas.labelActor = label;
    meas.midWorld[0] = (x1+x2)/2.0;
    meas.midWorld[1] = (y1+y2)/2.0;
    meas.sliceIndex = m_currentSlice;
    m_measurements.push_back(std::move(meas));

    m_renderer->GetRenderWindow()->Render();
    emit measurementDone(mm);
}

void MeasurementWidget::finalizeEllipse(double cx,double cy,double ex,double ey)
{
    if (!m_renderer) return;
    m_redoStack.clear();
    double a = std::abs(ex-cx), b = std::abs(ey-cy);
    if (a < 1e-6 || b < 1e-6) return;

    double z = sliceZ();
    auto pd = vtkSmartPointer<vtkPolyData>::New();
    fillEllipse(pd, cx,cy, a,b, z);
    auto actor = buildActor(pd, 1.0,0.55,0.0);
    m_renderer->AddActor(actor);

    // Alan: semi-axes fiziksel boyutta (spacing çarpıyla — mesafeyle tutarlı)
    double a_mm = a*m_spacingX, b_mm = b*m_spacingY;
    double area = M_PI * a_mm * b_mm;

    char buf[64];
    snprintf(buf, sizeof(buf), "%.2f cm\xc2\xb2", area / 100.0);

    auto label = makeLabel(1.0,0.85,0.0);
    label->SetInput(buf);
    m_renderer->AddActor(label);

    Measurement meas;
    meas.type = MEAS_ELLIPSE;
    meas.actors = {actor};
    meas.labelActor = label;
    meas.midWorld[0] = cx;
    meas.midWorld[1] = cy;
    meas.sliceIndex = m_currentSlice;
    m_measurements.push_back(std::move(meas));

    m_renderer->GetRenderWindow()->Render();
    emit measurementDone(area);
}

void MeasurementWidget::finalizeAngle(double ax,double ay,
                                       double vx,double vy,
                                       double bx,double by)
{
    if (!m_renderer) return;
    m_redoStack.clear();
    double v1[2] = {ax-vx, ay-vy};
    double v2[2] = {bx-vx, by-vy};
    double len1 = std::sqrt(v1[0]*v1[0]+v1[1]*v1[1]);
    double len2 = std::sqrt(v2[0]*v2[0]+v2[1]*v2[1]);
    if (len1 < 1e-9 || len2 < 1e-9) return;

    double dot = v1[0]*v2[0]+v1[1]*v2[1];
    double cos_a = std::max(-1.0, std::min(1.0, dot/(len1*len2)));
    double angle_deg = std::acos(cos_a)*180.0/M_PI;

    double z = sliceZ();

    // Kol 1: a → köşe
    auto pd1 = vtkSmartPointer<vtkPolyData>::New();
    fillLine(pd1, ax,ay, vx,vy, z);
    auto actor1 = buildActor(pd1, 1.0,0.55,0.0);
    m_renderer->AddActor(actor1);

    // Kol 2: köşe → b
    auto pd2 = vtkSmartPointer<vtkPolyData>::New();
    fillLine(pd2, vx,vy, bx,by, z);
    auto actor2 = buildActor(pd2, 1.0,0.55,0.0);
    m_renderer->AddActor(actor2);

    // Yay (köşede, küçük yarıçap)
    double arcR = std::min(len1,len2)*0.25;
    double th1  = std::atan2(v1[1],v1[0]);
    double th2  = std::atan2(v2[1],v2[0]);
    double dth  = th2 - th1;
    // Kısa yayı seç
    while (dth >  M_PI) dth -= 2.0*M_PI;
    while (dth < -M_PI) dth += 2.0*M_PI;
    auto pdArc = vtkSmartPointer<vtkPolyData>::New();
    fillArc(pdArc, vx,vy, th1,dth, arcR, z);
    auto arcActor = buildActor(pdArc, 1.0,0.55,0.0, 1.5f);
    m_renderer->AddActor(arcActor);

    char buf[32]; snprintf(buf,sizeof(buf),"%.1f\xc2\xb0", angle_deg);
    auto label = makeLabel(1.0,0.75,0.0);
    label->SetInput(buf);
    m_renderer->AddActor(label);

    // Label: yay orta noktasına yakın
    double midTh = th1 + dth*0.5;
    Measurement meas;
    meas.type = MEAS_ANGLE;
    meas.actors = {actor1, actor2, arcActor};
    meas.labelActor = label;
    meas.midWorld[0] = vx + std::cos(midTh)*arcR*2.2;
    meas.midWorld[1] = vy + std::sin(midTh)*arcR*2.2;
    meas.sliceIndex = m_currentSlice;
    m_measurements.push_back(std::move(meas));

    m_renderer->GetRenderWindow()->Render();
    emit measurementDone(angle_deg);
}

void MeasurementWidget::finalizeROI(double x1,double y1,double x2,double y2)
{
    if (!m_renderer) return;
    m_redoStack.clear();

    double z = sliceZ();
    auto pd = vtkSmartPointer<vtkPolyData>::New();
    fillRect(pd, x1,y1, x2,y2, z);
    auto actor = buildActor(pd, 0.0,1.0,1.0);
    m_renderer->AddActor(actor);

    // Alan
    double w_mm = std::abs(x2-x1)*m_spacingX;
    double h_mm = std::abs(y2-y1)*m_spacingY;
    double area = w_mm*h_mm;

    // İstatistik
    double mean=0,stddev=0,minV=0,maxV=0; int N=0;
    computeROIStats(x1,y1,x2,y2, mean,stddev,minV,maxV,N);

    char buf[256];
    if (N > 0) {
        snprintf(buf,sizeof(buf),
                 "Ort: %.0f\nSS: \xc2\xb1%.0f\nMin/Max: %.0f/%.0f\nAlan: %.2f cm\xc2\xb2",
                 mean, stddev, minV, maxV, area / 100.0);
    } else {
        snprintf(buf,sizeof(buf),"Alan: %.2f cm\xc2\xb2", area / 100.0);
    }

    auto label = makeLabel(0.0,1.0,1.0, 14);
    label->SetInput(buf);
    label->GetTextProperty()->SetJustificationToLeft();
    m_renderer->AddActor(label);

    Measurement meas;
    meas.type = MEAS_ROI;
    meas.actors = {actor};
    meas.labelActor = label;
    meas.midWorld[0] = (x1+x2)/2.0;
    meas.midWorld[1] = (y1+y2)/2.0;
    meas.sliceIndex = m_currentSlice;
    m_measurements.push_back(std::move(meas));

    m_renderer->GetRenderWindow()->Render();
    emit measurementDone(mean);
}

void MeasurementWidget::computeROIStats(double wx1,double wy1,double wx2,double wy2,
                                         double &mean,double &stddev,
                                         double &minV,double &maxV,int &N)
{
    mean=stddev=minV=maxV=0; N=0;
    if (!ImageDataRef) return;

    double *sp  = ImageDataRef->GetSpacing();
    double *org = ImageDataRef->GetOrigin();
    int    *dim = ImageDataRef->GetDimensions();
    if (sp[0]<=0||sp[1]<=0) return;

    double xMin=std::min(wx1,wx2), xMax=std::max(wx1,wx2);
    double yMin=std::min(wy1,wy2), yMax=std::max(wy1,wy2);
    int iMin=std::max(0,(int)((xMin-org[0])/sp[0]));
    int iMax=std::min(dim[0]-1,(int)((xMax-org[0])/sp[0]));
    int jMin=std::max(0,(int)((yMin-org[1])/sp[1]));
    int jMax=std::min(dim[1]-1,(int)((yMax-org[1])/sp[1]));
    int k   =std::max(0,std::min(dim[2]-1,m_currentSlice));

    if (iMin>iMax||jMin>jMax) return;

    std::vector<double> vals;
    vals.reserve((iMax-iMin+1)*(jMax-jMin+1));
    for (int j=jMin;j<=jMax;++j)
        for (int i=iMin;i<=iMax;++i)
            vals.push_back(ImageDataRef->GetScalarComponentAsDouble(i,j,k,0));
    if (vals.empty()) return;

    N=(int)vals.size();
    double sum=0;
    minV=maxV=vals[0];
    for (double v:vals){sum+=v;if(v<minV)minV=v;if(v>maxV)maxV=v;}
    mean=sum/N;
    double var=0;
    for (double v:vals) var+=(v-mean)*(v-mean);
    stddev=std::sqrt(var/N);
}

void MeasurementWidget::updateLabelPositions()
{
    if (!m_renderer) return;
    for (auto &m : m_measurements) {
        if (m.sliceIndex != m_currentSlice) continue;
        m_renderer->SetWorldPoint(m.midWorld[0], m.midWorld[1], 0.0, 1.0);
        m_renderer->WorldToDisplay();
        const double *d = m_renderer->GetDisplayPoint();
        m.labelActor->SetPosition(d[0], d[1]+10);
    }
}

void MeasurementWidget::clear()
{
    cancelPending();
    if (m_renderer) {
        for (auto &m : m_measurements) {
            for (auto &a : m.actors) m_renderer->RemoveActor(a);
            m_renderer->RemoveViewProp(m.labelActor);
        }
        // Redo stack'teki actor'lar renderer'a eklenmiş değil, sadece belleği serbest bırak
        m_renderer->GetRenderWindow()->Render();
    }
    m_measurements.clear();
    m_redoStack.clear();
}

void MeasurementWidget::clearCurrentSlice()
{
    cancelPending();
    if (m_renderer) {
        auto it = m_measurements.begin();
        while (it != m_measurements.end()) {
            if (it->sliceIndex == m_currentSlice) {
                for (auto &a : it->actors) m_renderer->RemoveActor(a);
                m_renderer->RemoveViewProp(it->labelActor);
                it = m_measurements.erase(it);
            } else {
                ++it;
            }
        }
        m_renderer->GetRenderWindow()->Render();
    }
}
