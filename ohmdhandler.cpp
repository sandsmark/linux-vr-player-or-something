#include "ohmdhandler.h"

#include <openhmd.h>
#include <QDebug>

OhmdHandler::OhmdHandler(QObject *parent) : QThread(parent),
    isRunning(true),
    m_modelViewMatrices(4)
{
//    m_modelViewMatrices.first.setToIdentity();
//    m_modelViewMatrices.second.setToIdentity();

    init();
}

OhmdHandler::~OhmdHandler()
{
    isRunning = false;
    m_waitCondition.notify_all();
    wait();
}

bool OhmdHandler::init()
{
    qDebug() << "starting ohmd thread";
    m_ohmdContext = ohmd_ctx_create();
    int num_devices = ohmd_ctx_probe(m_ohmdContext);
    if(num_devices < 0){
        printf("failed to probe devices: %s\n", ohmd_ctx_get_error(m_ohmdContext));
        return false;
    }
    int ret = ohmd_gets(OHMD_GLSL_330_DISTORTION_FRAG_SRC, &distortionFragShader);
    if (ret < 0) {
        qWarning() << "Failed to get distortion frag shader";
        return false;
    }
    ret = ohmd_gets(OHMD_GLSL_330_DISTORTION_VERT_SRC, &distortionVertShader);
    if (ret < 0) {
        qWarning() << "Failed to get distortion vert shader";
        return false;
    }
//    qDebug() << distortionFragShader;
//    qDebug() << "================\n"
//             << distortionVertShader;

    ohmd_device_settings* settings = ohmd_device_settings_create(m_ohmdContext);

    // If OHMD_IDS_AUTOMATIC_UPDATE is set to 0, ohmd_ctx_update() must be called at least 10 times per second.
    // It is enabled by default.

    int auto_update = 1;
    ohmd_device_settings_seti(settings, OHMD_IDS_AUTOMATIC_UPDATE, &auto_update);

    m_ohmdDevice = ohmd_list_open_device_s(m_ohmdContext, 0, settings);
    if(!m_ohmdDevice){
        printf("failed to open device: %s\n", ohmd_ctx_get_error(m_ohmdContext));
        return false;
    }


    const char *vendor = ohmd_list_gets(m_ohmdContext, 0, OHMD_VENDOR);
    const char *product = ohmd_list_gets(m_ohmdContext, 0, OHMD_PRODUCT);
    qDebug() << vendor << product;

    int hmd_w = 0, hmd_h = 0;
    ohmd_device_geti(m_ohmdDevice, OHMD_SCREEN_HORIZONTAL_RESOLUTION, &hmd_w);
    ohmd_device_geti(m_ohmdDevice, OHMD_SCREEN_VERTICAL_RESOLUTION, &hmd_h);
    displaySize.setWidth(hmd_w);
    displaySize.setHeight(hmd_h);
    qDebug() << hmd_w << hmd_h;

    float ipd;
    ohmd_device_getf(m_ohmdDevice, OHMD_EYE_IPD, &ipd);

    ohmd_device_getf(m_ohmdDevice, OHMD_SCREEN_HORIZONTAL_SIZE, &(viewport_scale[0]));
    viewport_scale[0] /= 2.0f;
    qDebug() << "viewport scale 1" << viewport_scale[0];
    ohmd_device_getf(m_ohmdDevice, OHMD_SCREEN_VERTICAL_SIZE, &(viewport_scale[1]));
    qDebug() << "viewport scale 2" << viewport_scale[1];

    //distortion coefficients
    ohmd_device_getf(m_ohmdDevice, OHMD_UNIVERSAL_DISTORTION_K, &(distortion_coeffs[0]));
    qDebug() << "dist coeff 1" << distortion_coeffs[0];
    ohmd_device_getf(m_ohmdDevice, OHMD_UNIVERSAL_ABERRATION_K, &(aberr_scale[0]));

    //calculate lens centers (assuming the eye separation is the distance between the lens centers)
    float sep;
    ohmd_device_getf(m_ohmdDevice, OHMD_LENS_HORIZONTAL_SEPARATION, &sep);
    ohmd_device_getf(m_ohmdDevice, OHMD_LENS_VERTICAL_POSITION, &(left_lens_center[1]));
    ohmd_device_getf(m_ohmdDevice, OHMD_LENS_VERTICAL_POSITION, &(right_lens_center[1]));
    left_lens_center[0] = viewport_scale[0] - sep/2.0f;
    right_lens_center[0] = sep/2.0f;
    //assume calibration was for lens view to which ever edge of screen is further away from lens center
    warp_scale = (left_lens_center[0] > right_lens_center[0]) ? left_lens_center[0] : right_lens_center[0];
    qDebug() << "Warp" << warp_scale;
    qDebug() << "sep" << sep;
    qDebug() << left_lens_center[1];
    qDebug() << right_lens_center[1];

//    float foo = 0;
//    ohmd_device_getf(m_ohmdDevice, OHMD_PROJECTION_ZFAR, &foo);
//    qDebug() << "zfar:" << foo;
//    ohmd_device_getf(m_ohmdDevice, OHMD_PROJECTION_ZNEAR, &foo);
//    qDebug() << "znear:" << foo;
//    foo = 100.f;
//    ohmd_device_setf(m_ohmdDevice, OHMD_PROJECTION_ZFAR, &foo);
//    ohmd_device_getf(m_ohmdDevice, OHMD_PROJECTION_ZFAR, &foo);
//    qDebug() << "zfar:" << foo;

    ohmd_device_settings_destroy(settings);

    return true;
}

QVector<QMatrix4x4> OhmdHandler::modelViewMatrices()
{
    m_mutex.lock();
    QVector<QMatrix4x4> ret = m_modelViewMatrices;
    m_mutex.unlock();
    m_waitCondition.notify_all();
    return ret;
}

void OhmdHandler::run()
{


    while (isRunning) {
        ohmd_ctx_update(m_ohmdContext);
        float leftMvMatrix[16];
        ohmd_device_getf(m_ohmdDevice, OHMD_LEFT_EYE_GL_MODELVIEW_MATRIX, leftMvMatrix);
        float rightMvMatrix[16];
        ohmd_device_getf(m_ohmdDevice, OHMD_RIGHT_EYE_GL_MODELVIEW_MATRIX, rightMvMatrix);

        float leftPMatrix[16];
        ohmd_device_getf(m_ohmdDevice, OHMD_LEFT_EYE_GL_PROJECTION_MATRIX, leftPMatrix);
        float rightPMatrix[16];
        ohmd_device_getf(m_ohmdDevice, OHMD_RIGHT_EYE_GL_PROJECTION_MATRIX, rightPMatrix);

        std::unique_lock<std::mutex> guard(m_mutex);
//        m_modelViewMatrices.first = QMatrix4x4(leftMatrix).inverted();
        m_modelViewMatrices[0] = QMatrix4x4(leftMvMatrix).inverted();
        m_modelViewMatrices[1] = QMatrix4x4(rightMvMatrix).inverted();
        m_modelViewMatrices[2] = QMatrix4x4(leftPMatrix);
        m_modelViewMatrices[3] = QMatrix4x4(rightPMatrix);
//        qDebug() << m_modelViewMatrices.first;
//        m_modelViewMatrices.second = QMatrix4x4(rightMatrix).inverted();

        m_waitCondition.wait(guard);
    }
    ohmd_ctx_destroy(m_ohmdContext);
}
