#ifndef OHMDHANDLER_H
#define OHMDHANDLER_H

#include <atomic>
#include <QThread>
#include <QMatrix4x4>

struct ohmd_context;
struct ohmd_device;

class OhmdHandler : public QObject
{
    Q_OBJECT

public:
    OhmdHandler(QObject *parent);
    ~OhmdHandler();

    bool init();

    std::atomic_bool isRunning;

    QMatrix4x4 rightProjection;
    QMatrix4x4 leftProjection;

    QMatrix4x4 rightModelView;
    QMatrix4x4 leftModelView;

    void update();

    const char *distortionFragShader = nullptr;
    const char *distortionVertShader = nullptr;

    QSize displaySize;

    //viewport is half the screen
    float viewport_scale[2]{};
    float aberr_scale[3];
    float warp_scale = 1.f;
    float warp_adj = 1.0f;
    float distortion_coeffs[4];
    float left_lens_center[2]{};
    float right_lens_center[2]{};

    float horiz_sep = 0.;

private:
    ohmd_context *m_ohmdContext;
    ohmd_device *m_ohmdDevice;
};

#endif // OHMDHANDLER_H
