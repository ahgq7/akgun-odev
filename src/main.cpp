#include <QApplication>
#include <QSurfaceFormat>
#include <QVTKOpenGLNativeWidget.h>
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());

    QApplication app(argc, argv);
    app.setApplicationName("DICOM Viewer");
    app.setApplicationVersion("1.0");
    app.setOrganizationName("Akgun");

    MainWindow window;
    window.setWindowTitle("DICOM Görüntüleyici");
    window.showMaximized();

    return app.exec();
}
