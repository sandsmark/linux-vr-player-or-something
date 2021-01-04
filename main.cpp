#include "widget.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    if (argc < 2) {
        qWarning() << "Please pass video";
        return 1;
    }

    const QString videoAngle360 = "--360";
    const QString videoAngle180 = "--180";
    char *path = nullptr;
    float videoAngle = 180;
    if (argc > 2) {
        for (int i=1; i<argc; i++) {
            if (argv[i] == videoAngle180) {
                videoAngle = 180;
                continue;
            }
            if (argv[i] == videoAngle360) {
                videoAngle = 360;
                continue;
            }
            if (path != nullptr) {
                qWarning() << "Usage:" << argv[0] << "[--360|--180] videofile";
                return 1;
            }
            path = argv[i];
        }
    } else {
        path = argv[1];
    }

    QSurfaceFormat format;
    format.setMajorVersion(3);
    format.setMinorVersion(3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(4);
    QSurfaceFormat::setDefaultFormat(format);

    QApplication a(argc, argv);
    MpvWidget w;
    w.videoAngle = videoAngle;
    w.show();
    //w.play(path);
    return a.exec();
}
