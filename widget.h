#ifndef PLAYERWINDOW_H
#define PLAYERWINDOW_H

#include <QOpenGLWindow>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include "mpv-qthelper.hpp"
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLFramebufferObject>

class OhmdHandler;

#define DEFAULT_FOV 90

class MpvWidget Q_DECL_FINAL: public QOpenGLWindow //, protected QOpenGLFunctions
{
    Q_OBJECT
public:
    enum VideoProjectionMode
    {
        Monoscopic,
        OverUnder,
        SideBySide
    } video_projection_mode = SideBySide;

    MpvWidget();
    ~MpvWidget();
    QSize sizeHint() const { return QSize(480, 270);}

    void play(const char *path);

    float videoAngle = 180;

public slots:
    void on_mpv_events();

Q_SIGNALS:
    void durationChanged(int value);
    void positionChanged(int value);

protected:
    void initializeGL() Q_DECL_OVERRIDE;
    void paintGL() Q_DECL_OVERRIDE;
    void showEvent(QShowEvent *e) override;

private Q_SLOTS:
    void maybeUpdate();
    void onScreenAdded();

private:
    void renderEye(int eye, const QMatrix4x4 &modelview, QMatrix4x4 projection);
    void handle_mpv_event(mpv_event *event);
    static void on_update(void *ctx);

    mpv_handle *m_mpv = nullptr;
    mpv_render_context *m_mpvGl = nullptr;
    OhmdHandler *m_ohmd;

    bool invert_stereo = true;

    float m_fieldOfView = DEFAULT_FOV;
    float m_distance = 100.f;
    double m_duration = 0;
    double m_position = 0;

    float m_rotHor = 0, m_rotVert = 0;

    QOpenGLShaderProgram *m_sphereShader = nullptr;
    QOpenGLShaderProgram *m_distortionShader = nullptr;

    QOpenGLBuffer m_cubeVbo;
    QOpenGLBuffer m_indexBo;
    QOpenGLVertexArrayObject m_cubeVao;
    QOpenGLFramebufferObject *m_videoFbo = nullptr;
    const char *m_path = nullptr;

    QImage m_posImage;
    //QPainterPath m_posString;
    //QPainterPath m_posStringStroke;

protected:
    void resizeGL(int w, int h) override;
    void keyPressEvent(QKeyEvent *event) override;
    quint32 sphereVbo[3];
    quint32 nIndices;
};



#endif // PLAYERWINDOW_H
