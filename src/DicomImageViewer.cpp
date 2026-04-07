#include "DicomImageViewer.h"
#include "MeasurementWidget.h"
#include "ThumbnailStrip.h"

#include <QVTKOpenGLNativeWidget.h>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QLabel>
#include <QMessageBox>
#include <QApplication>
#include <QComboBox>
#include <QPushButton>
#include <QKeyEvent>
#include <QFileInfo>
#include <QDir>
#include <QStringList>

#include <vtkImageData.h>
#include <vtkRenderer.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkObjectFactory.h>
#include <vtkCellPicker.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkShortArray.h>
#include <vtkUnsignedShortArray.h>
#include <vtkUnsignedCharArray.h>
#include <vtkFloatArray.h>
#include <vtkImageFlip.h>
#include <vtkImageShiftScale.h>
#include <vtkImageActor.h>
#include <vtkCamera.h>
#include <vtkTextProperty.h>
#include <vtkCallbackCommand.h>

// GDCM başlıkları
#include <gdcmImageReader.h>
#include <gdcmImage.h>
#include <gdcmAttribute.h>
#include <gdcmFile.h>
#include <gdcmDataSet.h>
#include <gdcmTag.h>
#include <gdcmIPPSorter.h>
#include <gdcmDirectory.h>
#include <gdcmReader.h>
#include <gdcmScanner.h>

#include <cstring>
#include <vector>
#include <string>
#include <algorithm>
#include <cmath>

class DicomInteractorStyle : public vtkInteractorStyleImage
{
public:
    static DicomInteractorStyle *New();
    vtkTypeMacro(DicomInteractorStyle, vtkInteractorStyleImage)

    enum { MODE_PAN = 0, MODE_WL = 1, MODE_MEAS = 2 };
    int Mode = MODE_PAN;

    MeasurementWidget  *Measurement = nullptr;
    vtkImageViewer2    *ImageViewer = nullptr;
    vtkImageData       *ImageData   = nullptr;
    std::function<void(int)>                    SliceChangedCb;
    std::function<void(double,double)>          WLChangedCb;
    std::function<void()>                       ZoomChangedCb;
    std::function<void(double,double,double)>   PixelInfoCb;

    // ---- Mouse wheel: slice scroll ----
    void OnMouseWheelForward() override
    {
        vtkInteractorStyleImage::OnMouseWheelForward();
        notifySlice();
    }
    void OnMouseWheelBackward() override
    {
        vtkInteractorStyleImage::OnMouseWheelBackward();
        notifySlice();
    }

    // ---- Sol tık ----
    void OnLeftButtonDown() override
    {
        if (Mode == MODE_WL) {
            // vtkInteractorStyleImage'in W/L pathway'ini KULLANMIYORUZ.
            // Kendimiz takip ediyoruz → vtkImageViewer2 her zaman senkron kalır.
            if (ImageViewer) {
                int *pos = this->Interactor->GetEventPosition();
                m_wlStartX   = pos[0];
                m_wlStartY   = pos[1];
                m_wlInitW    = ImageViewer->GetColorWindow();
                m_wlInitL    = ImageViewer->GetColorLevel();
                m_wlDragging = true;
            }
        } else if (Mode == MODE_MEAS) {
            double w[3];
            if (pickWorld(w))
                Measurement->handleClick(w[0], w[1]);
        } else {
            StartPan();
        }
    }

    void OnLeftButtonUp() override
    {
        if (Mode == MODE_WL) {
            m_wlDragging = false;
            if (WLChangedCb && ImageViewer)
                WLChangedCb(ImageViewer->GetColorWindow(), ImageViewer->GetColorLevel());
        } else {
            EndPan();
        }
    }

    void OnRightButtonUp() override
    {
        vtkInteractorStyleImage::OnRightButtonUp();
        if (ZoomChangedCb) ZoomChangedCb();
    }

    // ---- Mouse hareketi ----
    void OnMouseMove() override
    {
        if (Mode == MODE_MEAS) {
            double w[3];
            if (pickWorld(w) && Measurement)
                Measurement->updatePreview(w[0], w[1]);
            readPixelInfo();
            return;
        }
        if (Mode == MODE_WL) {
            if (m_wlDragging && ImageViewer) {
                int *pos  = this->Interactor->GetEventPosition();
                int *size = this->Interactor->GetRenderWindow()->GetSize();
                // VTK'nın standart formülü: çarpımsal hassasiyet
                double dx =  (pos[0] - m_wlStartX) * 4.0 / size[0];
                double dy = -(pos[1] - m_wlStartY) * 4.0 / size[1];
                double w = m_wlInitW + m_wlInitW * dx;
                double l = m_wlInitL + m_wlInitL * dy;
                if (std::abs(w) < 0.01) w = w < 0 ? -0.01 : 0.01;
                if (std::abs(l) < 0.01) l = l < 0 ? -0.01 : 0.01;
                // Doğrudan vtkImageViewer2'yi güncelle — tek pathway
                ImageViewer->SetColorWindow(w);
                ImageViewer->SetColorLevel(l);
                ImageViewer->Render();
                if (WLChangedCb) WLChangedCb(w, l);
            } else if (this->State == VTKIS_PAN || this->State == VTKIS_DOLLY) {
                vtkInteractorStyleImage::OnMouseMove();
            }
            readPixelInfo();
            return;
        }
        bool wasDolly = (this->State == VTKIS_DOLLY);
        vtkInteractorStyleImage::OnMouseMove();
        if (wasDolly && ZoomChangedCb) ZoomChangedCb();
        readPixelInfo();
    }

private:
    // W/L sürükleme state'i
    bool   m_wlDragging = false;
    int    m_wlStartX   = 0,  m_wlStartY = 0;
    double m_wlInitW    = 0.0, m_wlInitL = 0.0;

    void readPixelInfo()
    {
        if (!ImageData || !PixelInfoCb || !this->Interactor) return;

        int *epos = this->Interactor->GetEventPosition();
        vtkRenderer *ren = this->Interactor->FindPokedRenderer(epos[0], epos[1]);
        if (!ren) return;

        // Ekran koordinatını world'e çevir (orthographic, Z önemli değil)
        ren->SetDisplayPoint(epos[0], epos[1], 0.5);
        ren->DisplayToWorld();
        double *wp = ren->GetWorldPoint();
        double wx = wp[0], wy = wp[1];

        double *origin  = ImageData->GetOrigin();
        double *spacing = ImageData->GetSpacing();
        int    *dims    = ImageData->GetDimensions();

        if (spacing[0] <= 0.0 || spacing[1] <= 0.0) return;

        int i = static_cast<int>((wx - origin[0]) / spacing[0]);
        int j = static_cast<int>((wy - origin[1]) / spacing[1]);
        int k = ImageViewer ? ImageViewer->GetSlice() : 0;

        if (i < 0 || i >= dims[0] || j < 0 || j >= dims[1] || k < 0 || k >= dims[2]) {
            PixelInfoCb(wx, wy, std::nan(""));
            return;
        }

        double val = ImageData->GetScalarComponentAsDouble(i, j, k, 0);
        PixelInfoCb(wx, wy, val);
    }

    void notifySlice()
    {
        if (ImageViewer && SliceChangedCb)
            SliceChangedCb(ImageViewer->GetSlice());
    }
    bool pickWorld(double out[3])
    {
        if (!this->Interactor) return false;
        int *epos = this->Interactor->GetEventPosition();
        vtkRenderer *ren = this->Interactor->FindPokedRenderer(epos[0], epos[1]);
        if (!ren) return false;
        vtkNew<vtkCellPicker> picker;
        picker->SetTolerance(0.005);
        picker->Pick(epos[0], epos[1], 0, ren);
        picker->GetPickPosition(out);
        if (out[0] == 0 && out[1] == 0 && out[2] == 0) {
            ren->SetDisplayPoint(epos[0], epos[1], 0);
            ren->DisplayToWorld();
            double *wp = ren->GetWorldPoint();
            out[0] = wp[0]; out[1] = wp[1]; out[2] = wp[2];
        }
        return true;
    }
};
vtkStandardNewMacro(DicomInteractorStyle)

static QString formatDicomDate(const QString &s)
{
    // YYYYMMDD → DD.MM.YYYY
    QString t = s.trimmed();
    if (t.length() == 8)
        return t.mid(6, 2) + "." + t.mid(4, 2) + "." + t.left(4);
    return t;
}

static QString formatDicomTime(const QString &s)
{
    // HHMMSS.frac → HH:MM
    QString t = s.trimmed();
    if (t.length() >= 4)
        return t.left(2) + ":" + t.mid(2, 2);
    return t;
}

static QString readDicomStr(const gdcm::DataSet &ds, uint16_t g, uint16_t e)
{
    gdcm::Tag tag(g, e);
    if (!ds.FindDataElement(tag)) return QString();
    const gdcm::DataElement &de = ds.GetDataElement(tag);
    if (de.IsEmpty()) return QString();
    const gdcm::ByteValue *bv = de.GetByteValue();
    if (!bv || bv->GetLength() == 0) return QString();
    std::string val(bv->GetPointer(), bv->GetLength());
    while (!val.empty() && (val.back() == ' ' || val.back() == '\0'))
        val.pop_back();
    return QString::fromStdString(val).trimmed();
}

// Bir direction cosine vektöründen anatomik yön harfi döner (en baskın eksen)
// Hasta koordinat sistemi: +X=L, -X=R, +Y=P, -Y=A, +Z=F(eet), -Z=H(ead)
static char iopDirLetter(double x, double y, double z)
{
    double ax = std::abs(x), ay = std::abs(y), az = std::abs(z);
    if (ax >= ay && ax >= az) return x > 0 ? 'L' : 'R';
    if (ay >= ax && ay >= az) return y > 0 ? 'P' : 'A';
    return z > 0 ? 'H' : 'F';  // DICOM: +Z = Head (superior), -Z = Feet (inferior)
}

static void readOverlayInfo(const gdcm::DataSet &ds, DicomImageViewer::OverlayInfo &info)
{
    info = DicomImageViewer::OverlayInfo{};

    // Patient
    info.patientName      = readDicomStr(ds, 0x0010, 0x0010).replace('^', ' ').simplified();
    info.patientID        = readDicomStr(ds, 0x0010, 0x0020);
    info.patientBirthDate = formatDicomDate(readDicomStr(ds, 0x0010, 0x0030));
    info.patientSex       = readDicomStr(ds, 0x0010, 0x0040);
    // Study / Series
    info.institutionName  = readDicomStr(ds, 0x0008, 0x0080);
    info.studyDate        = formatDicomDate(readDicomStr(ds, 0x0008, 0x0020));
    info.studyTime        = formatDicomTime(readDicomStr(ds, 0x0008, 0x0030));
    info.modality         = readDicomStr(ds, 0x0008, 0x0060);
    info.seriesDescription= readDicomStr(ds, 0x0008, 0x103E);
    // Technical
    info.sliceThickness   = readDicomStr(ds, 0x0018, 0x0050);
    // Single-file IPP Z
    QString ipp = readDicomStr(ds, 0x0020, 0x0032);
    if (!ipp.isEmpty()) {
        QStringList parts = ipp.split('\\');
        if (parts.size() >= 3) {
            bool ok;
            double z = parts[2].toDouble(&ok);
            if (ok) { info.singleSliceZ = z; info.hasSingleSliceZ = true; }
        }
    }
    // IOP → yönelim harfleri
    // IOP: F[0..2]=row direction (displayed right), U[0..2]=col direction (DICOM down)
    // Sonra vtkImageFlip(Y) uygulanıyor → displayed top = -U yönü
    info.orientRight = info.orientLeft = info.orientTop = info.orientBottom = "";
    QString iop = readDicomStr(ds, 0x0020, 0x0037);
    if (!iop.isEmpty()) {
        QStringList p = iop.split('\\');
        if (p.size() == 6) {
            bool ok[6];
            double v[6];
            for (int i = 0; i < 6; ++i) v[i] = p[i].toDouble(&ok[i]);
            if (ok[0]&&ok[1]&&ok[2]&&ok[3]&&ok[4]&&ok[5]) {
                // F = row direction → displayed right
                // U = col direction (DICOM down) → after Y-flip, displayed top = -U
                info.orientRight  = QString(iopDirLetter( v[0],  v[1],  v[2]));
                info.orientLeft   = QString(iopDirLetter(-v[0], -v[1], -v[2]));
                info.orientTop    = QString(iopDirLetter(-v[3], -v[4], -v[5]));
                info.orientBottom = QString(iopDirLetter( v[3],  v[4],  v[5]));
            }
        }
    }
}

// GDCM PixelFormat → VTK scalar type
static int gdcmPixelFormatToVtkType(const gdcm::PixelFormat &pf)
{
    switch (pf.GetScalarType()) {
        case gdcm::PixelFormat::INT8:    return VTK_SIGNED_CHAR;
        case gdcm::PixelFormat::UINT8:   return VTK_UNSIGNED_CHAR;
        case gdcm::PixelFormat::INT12:
        case gdcm::PixelFormat::INT16:   return VTK_SHORT;
        case gdcm::PixelFormat::UINT12:
        case gdcm::PixelFormat::UINT16:  return VTK_UNSIGNED_SHORT;
        case gdcm::PixelFormat::INT32:   return VTK_INT;
        case gdcm::PixelFormat::UINT32:  return VTK_UNSIGNED_INT;
        case gdcm::PixelFormat::FLOAT32: return VTK_FLOAT;
        case gdcm::PixelFormat::FLOAT64: return VTK_DOUBLE;
        default:                         return VTK_UNSIGNED_SHORT;
    }
}

// GDCM DataSet'ten RescaleSlope/Intercept oku; tag yoksa varsayılan 1/0 döner
static void readRescale(const gdcm::DataSet &ds, double &slope, double &intercept)
{
    slope     = 1.0;
    intercept = 0.0;
    gdcm::Attribute<0x0028, 0x1053> attrSlope;
    if (ds.FindDataElement(attrSlope.GetTag())) {
        attrSlope.SetFromDataSet(ds);
        slope = attrSlope.GetValue();
    }
    gdcm::Attribute<0x0028, 0x1052> attrIntercept;
    if (ds.FindDataElement(attrIntercept.GetTag())) {
        attrIntercept.SetFromDataSet(ds);
        intercept = attrIntercept.GetValue();
    }
}

// Tek bir GDCM dosyasından vtkImageData oluştur
static vtkSmartPointer<vtkImageData>
loadSingleGdcm(const char *path, double &outSpacingX, double &outSpacingY,
               double &outSlope, double &outIntercept)
{
    gdcm::ImageReader reader;
    reader.SetFileName(path);
    if (!reader.Read()) {
        fprintf(stderr, "[GDCM] Okunamadı: %s\n", path);
        return nullptr;
    }

    const gdcm::Image &gimg = reader.GetImage();

    unsigned int nx = gimg.GetDimension(0);
    unsigned int ny = gimg.GetDimension(1);
    unsigned int nz = (gimg.GetNumberOfDimensions() > 2) ? gimg.GetDimension(2) : 1;

    // Spacing
    const double *sp = gimg.GetSpacing();
    outSpacingX = (sp && sp[0] > 0) ? sp[0] : 1.0;
    outSpacingY = (sp && sp[1] > 0) ? sp[1] : 1.0;

    // Rescale slope / intercept
    readRescale(reader.GetFile().GetDataSet(), outSlope, outIntercept);

    int vtkType = gdcmPixelFormatToVtkType(gimg.GetPixelFormat());
    int samples  = static_cast<int>(gimg.GetPixelFormat().GetSamplesPerPixel());

    size_t bufLen = gimg.GetBufferLength();
    std::vector<char> buf(bufLen);
    if (!gimg.GetBuffer(buf.data())) {
        fprintf(stderr, "[GDCM] Buffer alınamadı: %s\n", path);
        return nullptr;
    }

    auto vImg = vtkSmartPointer<vtkImageData>::New();
    vImg->SetDimensions(static_cast<int>(nx),
                        static_cast<int>(ny),
                        static_cast<int>(nz));
    vImg->SetSpacing(outSpacingX, outSpacingY, 1.0);
    vImg->SetOrigin(0.0, 0.0, 0.0);
    vImg->AllocateScalars(vtkType, samples);

    std::memcpy(vImg->GetScalarPointer(), buf.data(), bufLen);

    fprintf(stderr, "[GDCM] %ux%ux%u yüklendi, spacing: %.4f x %.4f, tip: %d, slope: %.4f, intercept: %.4f\n",
            nx, ny, nz, outSpacingX, outSpacingY, vtkType, outSlope, outIntercept);
    return vImg;
}

// Klasördeki tüm .dcm dosyalarını sırala, 3D hacim oluştur
static vtkSmartPointer<vtkImageData>
loadDirectoryGdcm(const char *dirPath, double &outSpacingX, double &outSpacingY,
                  double &outSlope, double &outIntercept,
                  DicomImageViewer::SortOrder sortOrder = DicomImageViewer::SORT_IPP,
                  std::vector<std::string> *outSortedFiles = nullptr)
{
    // Klasördeki dosyaları listele
    gdcm::Directory gdcmDir;
    unsigned int nFiles = gdcmDir.Load(dirPath, /*recursive=*/true);
    if (nFiles == 0) return nullptr;

    const gdcm::Directory::FilenamesType &allFiles = gdcmDir.GetFilenames();
    std::vector<std::string> dcmFiles;
    for (const auto &f : allFiles) {
        size_t dot   = f.rfind('.');
        size_t slash = f.find_last_of("/\\");
        if (dot != std::string::npos && dot > (slash == std::string::npos ? 0 : slash)) {
            // Uzantılı dosya — sadece .dcm kabul et
            std::string ext = f.substr(dot + 1);
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            if (ext == "dcm") dcmFiles.push_back(f);
        } else if (dot == std::string::npos || dot <= (slash == std::string::npos ? 0 : slash)) {
            // Uzantısız — DICOM magic bytes kontrolü (offset 128: "DICM")
            char magic[4] = {};
            if (FILE *fp = fopen(f.c_str(), "rb")) {
                if (fseek(fp, 128, SEEK_SET) == 0)
                    (void)fread(magic, 1, 4, fp);
                fclose(fp);
            }
            if (memcmp(magic, "DICM", 4) == 0) dcmFiles.push_back(f);
        }
    }
    if (dcmFiles.empty()) return nullptr;

    std::vector<std::string> sortedFiles;
    gdcm::IPPSorter sorter;  // zSpacing için scope'da tutulmalı

    auto sortByIPP = [&]() -> bool {
        // 1a. IPPSorter
        sorter.SetComputeZSpacing(true);
        sorter.SetZSpacingTolerance(1.0);
        if (sorter.Sort(dcmFiles) && !sorter.GetFilenames().empty()) {
            sortedFiles = sorter.GetFilenames();
            return true;
        }
        // 1b. Manuel IPP projeksiyon
        gdcm::Scanner sc;
        sc.AddTag(gdcm::Tag(0x0020, 0x0032));
        sc.AddTag(gdcm::Tag(0x0020, 0x0037));
        sc.Scan(dcmFiles);
        double normal[3] = {0, 0, 1};
        for (const auto &f : dcmFiles) {
            const char *iop = sc.GetValue(f.c_str(), gdcm::Tag(0x0020, 0x0037));
            if (!iop) continue;
            double F[3], U[3];
            if (sscanf(iop, "%lf\\%lf\\%lf\\%lf\\%lf\\%lf",
                       &F[0],&F[1],&F[2],&U[0],&U[1],&U[2]) == 6) {
                normal[0]=F[1]*U[2]-F[2]*U[1];
                normal[1]=F[2]*U[0]-F[0]*U[2];
                normal[2]=F[0]*U[1]-F[1]*U[0];
            }
            break;
        }
        bool hasIPP = false;
        std::vector<std::pair<double,std::string>> byZ;
        for (const auto &f : dcmFiles) {
            const char *ipp = sc.GetValue(f.c_str(), gdcm::Tag(0x0020, 0x0032));
            double proj = 0;
            if (ipp) { double x,y,z;
                if (sscanf(ipp,"%lf\\%lf\\%lf",&x,&y,&z)==3) {
                    proj=x*normal[0]+y*normal[1]+z*normal[2]; hasIPP=true; } }
            byZ.emplace_back(proj, f);
        }
        if (!hasIPP) return false;
        std::stable_sort(byZ.begin(), byZ.end(),
                         [](const auto &a,const auto &b){return a.first<b.first;});
        sortedFiles.reserve(byZ.size());
        for (auto &p : byZ) sortedFiles.push_back(p.second);
        return true;
    };

    auto sortByInstanceNumber = [&]() {
        gdcm::Scanner sc;
        sc.AddTag(gdcm::Tag(0x0020, 0x0013));
        sc.Scan(dcmFiles);
        std::vector<std::pair<int,std::string>> numbered;
        for (const auto &f : dcmFiles) {
            int inst = 0;
            const char *val = sc.GetValue(f.c_str(), gdcm::Tag(0x0020, 0x0013));
            if (val) inst = std::atoi(val);
            numbered.emplace_back(inst, f);
        }
        std::stable_sort(numbered.begin(), numbered.end());
        sortedFiles.reserve(numbered.size());
        for (auto &p : numbered) sortedFiles.push_back(p.second);
    };

    auto sortByFilename = [&]() {
        sortedFiles = dcmFiles;
        std::sort(sortedFiles.begin(), sortedFiles.end());
    };

    switch (sortOrder) {
    case DicomImageViewer::SORT_IPP:
        if (!sortByIPP()) sortByInstanceNumber();
        break;
    case DicomImageViewer::SORT_INSTANCE_NUMBER:
        sortByInstanceNumber();
        break;
    case DicomImageViewer::SORT_FILENAME:
        sortByFilename();
        break;
    }

    if (sortedFiles.empty()) return nullptr;

    // İlk okunabilir dosyayı bul
    gdcm::ImageReader firstReader;
    std::string firstReadable;
    for (const auto &f : sortedFiles) {
        firstReader.SetFileName(f.c_str());
        if (firstReader.Read()) { firstReadable = f; break; }
    }
    if (firstReadable.empty()) return nullptr;

    const gdcm::Image &firstImg = firstReader.GetImage();
    unsigned int nx = firstImg.GetDimension(0);
    unsigned int ny = firstImg.GetDimension(1);
    int vtkType     = gdcmPixelFormatToVtkType(firstImg.GetPixelFormat());
    int samples     = static_cast<int>(firstImg.GetPixelFormat().GetSamplesPerPixel());

    const double *sp = firstImg.GetSpacing();
    outSpacingX = (sp && sp[0] > 0) ? sp[0] : 1.0;
    outSpacingY = (sp && sp[1] > 0) ? sp[1] : 1.0;

    // Rescale slope / intercept — ilk dilimden oku, tüm hacme uygulanır
    readRescale(firstReader.GetFile().GetDataSet(), outSlope, outIntercept);

    // Z spacing
    double zSpacing = sorter.GetZSpacing();
    if (zSpacing <= 0) zSpacing = 1.0;

    int nz = static_cast<int>(sortedFiles.size());
    size_t sliceBytes = firstImg.GetBufferLength();

    // Hacim vtkImageData oluştur
    auto vImg = vtkSmartPointer<vtkImageData>::New();
    vImg->SetDimensions(static_cast<int>(nx), static_cast<int>(ny), nz);
    vImg->SetSpacing(outSpacingX, outSpacingY, zSpacing);
    vImg->SetOrigin(0.0, 0.0, 0.0);
    vImg->AllocateScalars(vtkType, samples);

    char *volumePtr = static_cast<char *>(vImg->GetScalarPointer());

    // Her dilimi oku ve hacme kopyala
    for (int z = 0; z < nz; ++z) {
        gdcm::ImageReader sliceReader;
        sliceReader.SetFileName(sortedFiles[z].c_str());
        if (!sliceReader.Read()) {
            fprintf(stderr, "[GDCM] Dilim okunamadı: %s\n", sortedFiles[z].c_str());
            continue;
        }
        const gdcm::Image &sliceImg = sliceReader.GetImage();
        size_t bytes = sliceImg.GetBufferLength();
        std::vector<char> buf(bytes);
        if (sliceImg.GetBuffer(buf.data())) {
            std::memcpy(volumePtr + z * sliceBytes, buf.data(),
                        std::min(bytes, sliceBytes));
        }
    }

    if (outSortedFiles)
        *outSortedFiles = sortedFiles;

    fprintf(stderr, "[GDCM] Klasör: %dx%dx%d yüklendi, spacing: %.4f x %.4f x %.4f\n",
            nx, ny, nz, outSpacingX, outSpacingY, zSpacing);
    return vImg;
}

// =====================================================
DicomImageViewer::DicomImageViewer(QWidget *parent)
    : QWidget(parent)
{
    setupVTK();
    setupLayout();
}

void DicomImageViewer::setupVTK()
{
    m_renderWindow = vtkSmartPointer<vtkGenericOpenGLRenderWindow>::New();

    m_vtkWidget = new QVTKOpenGLNativeWidget(this);
    m_vtkWidget->setRenderWindow(m_renderWindow);
    m_vtkWidget->installEventFilter(this);

    m_imageViewer = vtkSmartPointer<vtkImageViewer2>::New();
    m_imageViewer->SetRenderWindow(m_renderWindow);
    m_imageViewer->SetupInteractor(m_renderWindow->GetInteractor());

    // Veri yüklenmeden önce pipeline'ı boş görüntüyle başlat (hata mesajlarını önler)
    auto placeholder = vtkSmartPointer<vtkImageData>::New();
    placeholder->SetDimensions(1, 1, 1);
    placeholder->AllocateScalars(VTK_UNSIGNED_CHAR, 1);
    static_cast<unsigned char*>(placeholder->GetScalarPointer())[0] = 0;
    m_imageViewer->SetInputData(placeholder);

    m_style = vtkSmartPointer<DicomInteractorStyle>::New();
    m_style->SetInteractionModeToImage2D();
    m_renderWindow->GetInteractor()->SetInteractorStyle(m_style);

    m_measureWidget = new MeasurementWidget(this);
    m_measureWidget->setRenderer(m_imageViewer->GetRenderer());
    m_style->Measurement    = m_measureWidget;
    m_style->ImageViewer    = m_imageViewer;
    m_style->SliceChangedCb = [this](int slice) {
        // Mouse wheel ile değişen dilimi measurement widget'a bildir
        updateMeasureSlice(slice);
        emit sliceChanged(slice);
    };
    m_style->WLChangedCb = [this](double w, double l) {
        updatePresetHighlight(w, l);
        emit windowLevelChanged(w, l);
    };
    m_style->ZoomChangedCb = [this]() {
        updateOverlay();
        m_imageViewer->Render();
    };
    m_style->PixelInfoCb = [this](double x, double y, double val) {
        emit pixelInfoChanged(x, y, val);
    };

    // Her Render() öncesinde overlay'i güncelle — zoom/kamera değişikliklerini yakalar
    vtkNew<vtkCallbackCommand> preRenderCb;
    preRenderCb->SetClientData(this);
    preRenderCb->SetCallback([](vtkObject *, unsigned long, void *cd, void *) {
        static_cast<DicomImageViewer *>(cd)->updateOverlay();
    });
    m_renderWindow->AddObserver(vtkCommand::StartEvent, preRenderCb);
    
    auto makeActor = [](double x, double y, int hJust, int vJust)
    {
        auto a = vtkSmartPointer<vtkTextActor>::New();
        a->GetTextProperty()->SetFontSize(13);
        a->GetTextProperty()->SetColor(1.0, 1.0, 0.8);
        a->GetTextProperty()->SetShadow(1);
        a->GetTextProperty()->SetShadowOffset(1, -1);
        a->GetTextProperty()->SetFontFamilyToArial();
        a->GetTextProperty()->SetJustification(hJust);
        a->GetTextProperty()->SetVerticalJustification(vJust);
        a->GetPositionCoordinate()->SetCoordinateSystemToNormalizedViewport();
        a->SetPosition(x, y);
        a->SetVisibility(0);
        return a;
    };

    // VTK justification constants: LEFT=0 CENTER=1 RIGHT=2 | BOTTOM=0 CENTER=1 TOP=2
    m_overlayTL = makeActor(0.01, 0.98, 0, 2);
    m_overlayTR = makeActor(0.99, 0.98, 2, 2);
    m_overlayBL = makeActor(0.01, 0.02, 0, 0);
    m_overlayBR = makeActor(0.99, 0.02, 2, 0);

    vtkRenderer *ren = m_imageViewer->GetRenderer();
    ren->AddActor2D(m_overlayTL);
    ren->AddActor2D(m_overlayTR);
    ren->AddActor2D(m_overlayBL);
    ren->AddActor2D(m_overlayBR);
    
    auto makeOrient = [](double x, double y, int hJust, int vJust)
    {
        auto a = vtkSmartPointer<vtkTextActor>::New();
        a->GetTextProperty()->SetFontSize(18);
        a->GetTextProperty()->SetBold(1);
        a->GetTextProperty()->SetColor(1.0, 1.0, 1.0);
        a->GetTextProperty()->SetShadow(1);
        a->GetTextProperty()->SetShadowOffset(1, -1);
        a->GetTextProperty()->SetFontFamilyToArial();
        a->GetTextProperty()->SetJustification(hJust);
        a->GetTextProperty()->SetVerticalJustification(vJust);
        a->GetPositionCoordinate()->SetCoordinateSystemToNormalizedViewport();
        a->SetPosition(x, y);
        a->SetVisibility(0);
        return a;
    };

    m_orientTop    = makeOrient(0.5,  0.97, 1, 2); // center, top
    m_orientBottom = makeOrient(0.5,  0.03, 1, 0); // center, bottom
    m_orientLeft   = makeOrient(0.01, 0.5,  0, 1); // left,   center
    m_orientRight  = makeOrient(0.99, 0.5,  2, 1); // right,  center

    ren->AddActor2D(m_orientTop);
    ren->AddActor2D(m_orientBottom);
    ren->AddActor2D(m_orientLeft);
    ren->AddActor2D(m_orientRight);
}

void DicomImageViewer::setupLayout()
{
    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(2);

    auto *topWidget = new QWidget(this);
    auto *topLayout = new QHBoxLayout(topWidget);
    topLayout->setContentsMargins(0, 0, 0, 0);
    topLayout->setSpacing(0);

    m_thumbStrip = new ThumbnailStrip(this);
    m_thumbStrip->setVisible(false); // hidden until a multi-slice image is loaded
    connect(m_thumbStrip, &ThumbnailStrip::sliceClicked,
            this,          &DicomImageViewer::setSlice);

    topLayout->addWidget(m_thumbStrip);
    topLayout->addWidget(m_vtkWidget, 1);
    mainLayout->addWidget(topWidget, 1);

    m_infoLabel = new QLabel("Dosya yüklenmedi", this);
    m_infoLabel->setAlignment(Qt::AlignCenter);
    m_infoLabel->setStyleSheet("background:#1e1e1e; color:#aaa; padding:3px;");
    mainLayout->addWidget(m_infoLabel);

    {
        struct Preset { const char *label; double ww; double wc; };
        static const Preset kPresets[] = {
            { "Kemik",        1500,  300 },
            { "Akciğer",      1500, -600 },
            { "Yumuşak Doku",  400,   40 },
            { "Beyin",          80,   40 },
            { "Karaciğer",     150,   60 },
            { "Anjiyo",        600,  300 },
            { "Varsayılan",      0,    0 },
        };
        auto *presetRow = new QHBoxLayout;
        presetRow->setSpacing(2);
        presetRow->setContentsMargins(2, 0, 2, 0);
        for (const auto &p : kPresets) {
            auto *btn = new QPushButton(p.label, this);
            btn->setCheckable(true);
            btn->setEnabled(false);
            btn->setFixedHeight(22);
            btn->setStyleSheet(
                "QPushButton { background:#3a3a3a; color:#ccc; border:1px solid #555;"
                "  padding:0 4px; border-radius:3px; font-size:11px; }"
                "QPushButton:hover { background:#4a4a4a; }"
                "QPushButton:checked { background:#1e6e9e; border-color:#4fc3f7; color:#fff; }"
                "QPushButton:focus { outline:none; }");
            const double ww = p.ww, wc = p.wc;
            connect(btn, &QPushButton::clicked, this, [this, ww, wc]() {
                setWindowLevel(ww, wc);
            });
            presetRow->addWidget(btn, 1);
            m_bottomPresetBtns.append(btn);
        }
        mainLayout->addLayout(presetRow);
    }

    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel("Dilim:", this));
    m_sliceSlider = new QSlider(Qt::Horizontal, this);
    m_sliceSlider->setEnabled(false);
    connect(m_sliceSlider, &QSlider::valueChanged, this, &DicomImageViewer::setSlice);
    row->addWidget(m_sliceSlider, 1);

    m_sortCombo = new QComboBox(this);
    m_sortCombo->addItem("IPP",             SORT_IPP);
    m_sortCombo->addItem("Instance Number", SORT_INSTANCE_NUMBER);
    m_sortCombo->addItem("Dosya adı",       SORT_FILENAME);
    m_sortCombo->setStyleSheet(
        "QComboBox{background:#3c3c3c;color:#ddd;border:1px solid #555;"
        "padding:2px 6px;border-radius:3px;min-width:110px;}"
        "QComboBox QAbstractItemView{background:#3c3c3c;color:#ddd;}");
    connect(m_sortCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        setSortOrder(static_cast<SortOrder>(idx));
    });
    row->addWidget(m_sortCombo);
    mainLayout->addLayout(row);
}

// Image actor bounds'tan mevcut dilimin dünya-z'sini alır, measurement widget'ı günceller
void DicomImageViewer::updateMeasureSlice(int slice)
{
    // ImageActor, SetSlice çağrısından sonra doğru Z sınırlarına sahiptir
    double b[6];
    m_imageViewer->GetImageActor()->GetBounds(b);
    double sliceZ = (b[4] + b[5]) / 2.0;

    // Kameranın hangi tarafta olduğunu belirle → çizgiyi kameraya doğru kaydır
    double camPos[3];
    m_imageViewer->GetRenderer()->GetActiveCamera()->GetPosition(camPos);
    double offset = (camPos[2] > sliceZ) ? 0.5 : -0.5;

    m_measureWidget->setCurrentSliceZ(sliceZ + offset);
    m_measureWidget->setCurrentSlice(slice);
}

bool DicomImageViewer::applyImage()
{
    if (!m_imageData) return false;

    int dims[3];
    m_imageData->GetDimensions(dims);
    if (dims[0] == 0 || dims[1] == 0) return false;

    // Pixel spacing → ölçüm widgetına ilet
    m_measureWidget->setPixelSpacing(m_spacingX, m_spacingY);

    // HU dönüşümü: output = (input + intercept/slope) * slope
    // slope=1, intercept=0 ise (veya tag yoksa) dönüşüm atlanır
    vtkSmartPointer<vtkImageData> huData = m_imageData;
    if (m_rescaleSlope != 1.0 || m_rescaleIntercept != 0.0) {
        double safeSlope = (m_rescaleSlope != 0.0) ? m_rescaleSlope : 1.0;
        auto shifter = vtkSmartPointer<vtkImageShiftScale>::New();
        shifter->SetInputData(m_imageData);
        shifter->SetShift(m_rescaleIntercept / safeSlope);
        shifter->SetScale(safeSlope);
        shifter->SetOutputScalarTypeToShort();
        shifter->ClampOverflowOn();
        shifter->Update();
        huData = shifter->GetOutput();
        fprintf(stderr, "[HU] slope=%.4f intercept=%.4f uygulandı\n",
                m_rescaleSlope, m_rescaleIntercept);
    }

    // GDCM Y ekseni VTK ile ters — flip uygula
    auto flipper = vtkSmartPointer<vtkImageFlip>::New();
    flipper->SetInputData(huData);
    flipper->SetFilteredAxis(1); // Y ekseni
    flipper->Update();

    m_displayImageData = flipper->GetOutput();
    m_style->ImageData        = m_displayImageData;
    m_measureWidget->ImageDataRef = m_displayImageData;

    m_imageViewer->SetInputData(m_displayImageData);
    m_imageViewer->SetSliceOrientationToXY();

    int minZ = m_imageViewer->GetSliceMin();
    int maxZ = m_imageViewer->GetSliceMax();
    m_imageViewer->SetSlice(minZ);

    // Yeni görüntüde eski ölçümleri temizle, dilimi sıfırla
    m_measureWidget->clear();

    // İlk render: pipeline bounds'larını oluşturur
    m_imageViewer->Render();
    // Bounds hazır olduktan sonra kamerayı sıfırla (görüntü tam sığsın)
    m_imageViewer->GetRenderer()->ResetCamera();

    m_imageLoaded = true;
    updateMeasureSlice(minZ);
    updateOverlay();

    m_imageViewer->Render();

    m_sliceSlider->setRange(minZ, maxZ);
    m_sliceSlider->setValue(minZ);
    m_sliceSlider->setEnabled(maxZ > minZ);

    double ww = m_imageViewer->GetColorWindow();
    double wl = m_imageViewer->GetColorLevel();
    m_thumbStrip->setImageData(m_displayImageData, ww, wl);
    m_thumbStrip->setActiveSlice(minZ);
    // Show only for multi-slice images
    m_thumbStrip->setVisible(maxZ > minZ);

    for (auto *btn : m_bottomPresetBtns) btn->setEnabled(true);
    updatePresetHighlight(ww, wl);
    emit imageLoaded(minZ, maxZ);
    emit windowLevelChanged(ww, wl);
    return true;
}

bool DicomImageViewer::loadFile(const QString &filePath)
{
    if (filePath.isEmpty()) return false;

    m_rescaleSlope    = 1.0;
    m_rescaleIntercept = 0.0;
    const std::string pathStd = filePath.toUtf8().constData();
    m_imageData = loadSingleGdcm(pathStd.c_str(),
                                 m_spacingX, m_spacingY,
                                 m_rescaleSlope, m_rescaleIntercept);
    // Overlay tag'larını oku
    {
        gdcm::Reader r;
        r.SetFileName(pathStd.c_str());
        if (r.Read())
            readOverlayInfo(r.GetFile().GetDataSet(), m_overlay);
        else
            m_overlay = OverlayInfo{};
    }
    if (!applyImage()) {
        QMessageBox mb(QMessageBox::Warning, "Hata", "DICOM okunamadı:\n" + filePath,
                       QMessageBox::Ok, this);
        mb.setStyleSheet("QLabel{color:#ddd;} QMessageBox{background:#2b2b2b;} "
                         "QPushButton{color:#ddd;background:#4a4a4a;border:1px solid #666;"
                         "padding:4px 12px;border-radius:3px;}");
        mb.exec();
        return false;
    }
    m_infoLabel->setText(QFileInfo(filePath).fileName());
    return true;
}

bool DicomImageViewer::loadDirectory(const QString &dirPath)
{
    if (dirPath.isEmpty()) return false;
    m_lastDirPath = dirPath;

    m_rescaleSlope    = 1.0;
    m_rescaleIntercept = 0.0;
    auto order = m_sortOrder;
    std::vector<std::string> sortedFiles;
    m_imageData = loadDirectoryGdcm(dirPath.toUtf8().constData(),
                                    m_spacingX, m_spacingY,
                                    m_rescaleSlope, m_rescaleIntercept,
                                    order, &sortedFiles);
    // Overlay: ilk dosyadan static tag'lar, scanner ile per-slice IPP
    m_overlay = OverlayInfo{};
    if (!sortedFiles.empty()) {
        gdcm::Reader r;
        r.SetFileName(sortedFiles[0].c_str());
        if (r.Read())
            readOverlayInfo(r.GetFile().GetDataSet(), m_overlay);
        // Per-slice IPP Z değerleri
        gdcm::Scanner sc;
        sc.AddTag(gdcm::Tag(0x0020, 0x0032));
        sc.Scan(sortedFiles);
        m_overlay.slicePositionsZ.assign(sortedFiles.size(), 0.0);
        for (size_t i = 0; i < sortedFiles.size(); ++i) {
            const char *ipp = sc.GetValue(sortedFiles[i].c_str(), gdcm::Tag(0x0020, 0x0032));
            if (ipp) {
                double x, y, z;
                if (sscanf(ipp, "%lf\\%lf\\%lf", &x, &y, &z) == 3)
                    m_overlay.slicePositionsZ[i] = z;
            }
        }
    }
    if (!applyImage()) {
        // SOP Class'a bakarak açıklayıcı mesaj göster
        QString detail;
        gdcm::Scanner sc;
        sc.AddTag(gdcm::Tag(0x0008, 0x0016)); // SOP Class UID
        gdcm::Directory gd;
        if (gd.Load(dirPath.toUtf8().constData(), true) > 0) {
            sc.Scan(gd.GetFilenames());
            for (const auto &f : gd.GetFilenames()) {
                const char *v = sc.GetValue(f.c_str(), gdcm::Tag(0x0008, 0x0016));
                if (!v) continue;
                std::string uid(v);
                // Görüntü içermeyen bilinen DICOM türleri
                if      (uid.find("1.2.840.10008.5.1.4.1.1.481") != std::string::npos)
                    detail = "\n\nBu seri RT (Radyoterapi) verisi içeriyor — görüntülenemez.\n"
                             "(RT Structure Set, RT Plan vb.)";
                else if (uid.find("1.2.840.10008.5.1.4.1.1.88") != std::string::npos)
                    detail = "\n\nBu seri SR (Yapılandırılmış Rapor) içeriyor — görüntülenemez.";
                else if (uid.find("1.2.840.10008.5.1.4.1.1.9") != std::string::npos)
                    detail = "\n\nBu seri Waveform (ECG/EEG vb.) içeriyor — görüntülenemez.";
                else if (uid.find("1.2.840.10008.5.1.4.1.1.104") != std::string::npos)
                    detail = "\n\nBu seri gömülü PDF içeriyor — görüntülenemez.";
                else if (uid.find("1.2.840.10008.5.1.4.1.1.11") != std::string::npos)
                    detail = "\n\nBu seri Presentation State içeriyor — görüntülenemez.";
                else if (uid.find("1.2.840.10008.5.1.4.1.1.66") != std::string::npos)
                    detail = "\n\nBu seri Registration/Raw Data içeriyor — görüntülenemez.";
                else if (!uid.empty())
                    detail = QString("\n\nSOP Class: %1\nBu tür desteklenmiyor.")
                                 .arg(QString::fromStdString(uid));
                break;
            }
        }
        QMessageBox mb(QMessageBox::Warning, "Hata",
            "Klasörde görüntü DICOM bulunamadı:\n" + dirPath + detail,
            QMessageBox::Ok, this);
        mb.setStyleSheet("QLabel{color:#ddd;} QMessageBox{background:#2b2b2b;} "
                         "QPushButton{color:#ddd;background:#4a4a4a;border:1px solid #666;"
                         "padding:4px 12px;border-radius:3px;}");
        mb.exec();
        return false;
    }
    m_infoLabel->setText("Klasör: " + QFileInfo(dirPath).fileName());
    return true;
}

void DicomImageViewer::setMode(int mode)
{
    m_style->Mode = mode;
    m_measureWidget->enable(mode == DicomInteractorStyle::MODE_MEAS);
}

void DicomImageViewer::setSlice(int slice)
{
    if (!m_imageLoaded) return;
    m_imageViewer->SetSlice(slice);
    updateMeasureSlice(slice);
    updateOverlay();
    m_imageViewer->Render();

    // Alt slider'ı güncelle (sinyal döngüsü olmadan)
    m_sliceSlider->blockSignals(true);
    m_sliceSlider->setValue(slice);
    m_sliceSlider->blockSignals(false);

    m_thumbStrip->setActiveSlice(slice);

    emit sliceChanged(slice);
}

void DicomImageViewer::setSortOrder(SortOrder order)
{
    if (m_sortOrder == order) return;
    m_sortOrder = order;

    // Alt combo'yu güncelle (sinyal döngüsü olmasın)
    if (m_sortCombo) {
        m_sortCombo->blockSignals(true);
        m_sortCombo->setCurrentIndex(static_cast<int>(order));
        m_sortCombo->blockSignals(false);
    }

    emit sortOrderChanged(order);  // sağ panel radio'ları için
    if (!m_lastDirPath.isEmpty()) loadDirectory(m_lastDirPath);
}

void DicomImageViewer::resetCamera()
{
    m_imageViewer->GetRenderer()->ResetCamera();
    updateOverlay();
    m_imageViewer->Render();
}

void DicomImageViewer::setWindowLevel(double window, double level)
{
    if (!m_imageLoaded) return;

    if (window == 0.0 && level == 0.0) {
        // Auto: scalar range'den hesapla
        double range[2];
        m_imageViewer->GetInput()->GetScalarRange(range);
        window = range[1] - range[0];
        level  = (range[0] + range[1]) / 2.0;
    }
    m_imageViewer->SetColorWindow(window);
    m_imageViewer->SetColorLevel(level);
    updatePresetHighlight(window, level);
    updateOverlay();
    m_imageViewer->Render();
    emit windowLevelChanged(window, level);
}

void DicomImageViewer::setOverlayVisible(bool visible)
{
    m_overlayVisible = visible;
    updateOverlay();
    m_imageViewer->Render();
}

bool DicomImageViewer::eventFilter(QObject *obj, QEvent *ev)
{
    if (obj != m_vtkWidget || ev->type() != QEvent::KeyPress)
        return QWidget::eventFilter(obj, ev);

    auto *ke  = static_cast<QKeyEvent*>(ev);
    const bool ctrl = ke->modifiers() & Qt::ControlModifier;
    const int  key  = ke->key();

    // Ctrl+Z / Ctrl+Y — undo/redo
    if (ctrl && key == Qt::Key_Z) { m_measureWidget->undo(); return true; }
    if (ctrl && key == Qt::Key_Y) { m_measureWidget->redo(); return true; }

    // Diğer Ctrl kombinasyonları (Ctrl+A, Ctrl+D, Ctrl+T...) → QAction'a bırak
    if (ctrl) return false;

    // ESC — devam eden çizimi iptal et
    if (key == Qt::Key_Escape) {
        if (m_style->Mode == DicomInteractorStyle::MODE_MEAS)
            m_measureWidget->cancelCurrent();
        return true;
    }

    // W / S — dilim ileri/geri
    if (key == Qt::Key_W) {
        if (m_imageLoaded) setSlice(m_imageViewer->GetSlice() + 1);
        return true;
    }
    if (key == Qt::Key_S) {
        if (m_imageLoaded) setSlice(m_imageViewer->GetSlice() - 1);
        return true;
    }

    // Mod kısayolları: (qtKey, modeId, measType)
    struct Entry { int qtKey; int modeId; MeasurementWidget::MeasurementType meas; };
    static const Entry table[] = {
        { Qt::Key_X, 0, MeasurementWidget::MEAS_DISTANCE },
        { Qt::Key_C, 1, MeasurementWidget::MEAS_DISTANCE },
        { Qt::Key_V, 2, MeasurementWidget::MEAS_DISTANCE },
        { Qt::Key_B, 3, MeasurementWidget::MEAS_ELLIPSE  },
        { Qt::Key_N, 4, MeasurementWidget::MEAS_ANGLE    },
        { Qt::Key_M, 5, MeasurementWidget::MEAS_ROI      },
    };
    for (const auto &t : table) {
        if (key == t.qtKey) {
            const int vtkMode = (t.modeId <= 1) ? t.modeId : 2;
            if (t.modeId >= 2)
                m_measureWidget->setMeasurementType(t.meas);
            setMode(vtkMode);
            emit modeChangeRequested(t.modeId);
            return true;
        }
    }

    return false; // diğer tuşları VTK'ya ilet
}

void DicomImageViewer::updatePresetHighlight(double w, double l)
{
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
    const int n = static_cast<int>(m_bottomPresetBtns.size());
    for (int i = 0; i < n; ++i) {
        const bool match = (std::abs(w - kPresets[i].ww) < 0.5 &&
                            std::abs(l - kPresets[i].wc) < 0.5);
        m_bottomPresetBtns[i]->setChecked(match);
    }
}

void DicomImageViewer::updateOverlay()
{
    if (!m_imageLoaded) {
        m_overlayTL->SetVisibility(0);
        m_overlayTR->SetVisibility(0);
        m_overlayBL->SetVisibility(0);
        m_overlayBR->SetVisibility(0);
        m_orientTop->SetVisibility(0);
        m_orientBottom->SetVisibility(0);
        m_orientLeft->SetVisibility(0);
        m_orientRight->SetVisibility(0);
        return;
    }

    int vis = m_overlayVisible ? 1 : 0;

    {
        QStringList lines;
        if (!m_overlay.patientName.isEmpty())
            lines << m_overlay.patientName;
        if (!m_overlay.patientID.isEmpty())
            lines << "ID: " + m_overlay.patientID;
        if (!m_overlay.patientBirthDate.isEmpty())
            lines << "DT: " + m_overlay.patientBirthDate;
        if (!m_overlay.patientSex.isEmpty())
            lines << m_overlay.patientSex;
        m_overlayTL->SetInput(lines.join("\n").toUtf8().constData());
        m_overlayTL->SetVisibility(vis);
    }

    {
        QStringList lines;
        if (!m_overlay.institutionName.isEmpty())
            lines << m_overlay.institutionName;
        if (!m_overlay.studyDate.isEmpty()) {
            QString dt = m_overlay.studyDate;
            if (!m_overlay.studyTime.isEmpty()) dt += "  " + m_overlay.studyTime;
            lines << dt;
        }
        if (!m_overlay.modality.isEmpty())
            lines << m_overlay.modality;
        if (!m_overlay.seriesDescription.isEmpty())
            lines << m_overlay.seriesDescription;
        m_overlayTR->SetInput(lines.join("\n").toUtf8().constData());
        m_overlayTR->SetVisibility(vis);
    }

    {
        int slice = m_imageViewer->GetSlice();
        int maxZ  = m_imageViewer->GetSliceMax();
        QStringList lines;
        // Konum
        if (!m_overlay.slicePositionsZ.empty() &&
            slice < static_cast<int>(m_overlay.slicePositionsZ.size())) {
            lines << QString("Konum: %1 mm")
                         .arg(m_overlay.slicePositionsZ[slice], 0, 'f', 1);
        } else if (m_overlay.hasSingleSliceZ) {
            lines << QString("Konum: %1 mm").arg(m_overlay.singleSliceZ, 0, 'f', 1);
        }
        if (!m_overlay.sliceThickness.isEmpty())
            lines << "Kalınlık: " + m_overlay.sliceThickness + " mm";
        lines << QString("Dilim: %1 / %2").arg(slice + 1).arg(maxZ + 1);
        m_overlayBL->SetInput(lines.join("\n").toUtf8().constData());
        m_overlayBL->SetVisibility(vis);
    }

    {
        double w = m_imageViewer->GetColorWindow();
        double l = m_imageViewer->GetColorLevel();
        QStringList lines;
        lines << QString("W: %1  L: %2").arg(static_cast<int>(w)).arg(static_cast<int>(l));
        lines << QString("Sp: %1 x %2 mm")
                     .arg(m_spacingX, 0, 'f', 2).arg(m_spacingY, 0, 'f', 2);
        
        vtkCamera *cam = m_imageViewer->GetRenderer()->GetActiveCamera();
        int *sz = m_renderWindow->GetSize();
        double vp[4];
        m_imageViewer->GetRenderer()->GetViewport(vp);
        double vpH = sz[1] * (vp[3] - vp[1]);
        double ps  = cam ? cam->GetParallelScale() : 0.0;
        if (ps > 0.0 && vpH > 0.0 && m_spacingY > 0.0) {
            double worldPerPx = (2.0 * ps) / vpH;
            double zoomY   = m_spacingY / worldPerPx;
            double zoomX   = (m_spacingX > 0.0) ? m_spacingX / worldPerPx : zoomY;
            double zoomPct = std::min(zoomX, zoomY) * 100.0;
            lines << QString("Zoom: %1%").arg(static_cast<int>(zoomPct));
        }
        m_overlayBR->SetInput(lines.join("\n").toUtf8().constData());
        m_overlayBR->SetVisibility(vis);
    }

    {
        bool hasOrient = !m_overlay.orientRight.isEmpty();
        int ov = (vis && hasOrient) ? 1 : 0;
        m_orientTop->SetInput(m_overlay.orientTop.toUtf8().constData());
        m_orientBottom->SetInput(m_overlay.orientBottom.toUtf8().constData());
        m_orientLeft->SetInput(m_overlay.orientLeft.toUtf8().constData());
        m_orientRight->SetInput(m_overlay.orientRight.toUtf8().constData());
        m_orientTop->SetVisibility(ov);
        m_orientBottom->SetVisibility(ov);
        m_orientLeft->SetVisibility(ov);
        m_orientRight->SetVisibility(ov);
    }
}

int DicomImageViewer::currentSlice() const { return m_imageViewer->GetSlice(); }
int DicomImageViewer::minSlice()     const { return m_imageViewer->GetSliceMin(); }
int DicomImageViewer::maxSlice()     const { return m_imageViewer->GetSliceMax(); }
