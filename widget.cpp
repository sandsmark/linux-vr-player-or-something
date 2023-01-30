#include "widget.h"

#include "ohmdhandler.h"

#include <stdexcept>
#include <QOpenGLContext>
#include <QMetaObject>
#include <QTimer>
#include <QOpenGLPaintDevice>
#include <QStandardPaths>
#include <QPainter>
#include <QScreen>
#include <QApplication>
#include <QTime>
#include <QOpenGLExtraFunctions>
#include <QKeyEvent>
#include <cmath>

/********************/

static const QVector3D cube_vertices[] =
{
    // back
    QVector3D(-1.0f,  1.0f, -1.0f),
    QVector3D(-1.0f, -1.0f, -1.0f),
    QVector3D( 1.0f, -1.0f, -1.0f),

    QVector3D( 1.0f, -1.0f, -1.0f),
    QVector3D( 1.0f,  1.0f, -1.0f),
    QVector3D(-1.0f,  1.0f, -1.0f),

    // front
    QVector3D( 1.0f,  1.0f,  1.0f),
    QVector3D( 1.0f, -1.0f,  1.0f),
    QVector3D(-1.0f, -1.0f,  1.0f),

    QVector3D(-1.0f, -1.0f,  1.0f),
    QVector3D(-1.0f,  1.0f,  1.0f),
    QVector3D( 1.0f,  1.0f,  1.0f),

    // left
    QVector3D(-1.0f,  1.0f,  1.0f),
    QVector3D(-1.0f, -1.0f,  1.0f),
    QVector3D(-1.0f, -1.0f, -1.0f),

    QVector3D(-1.0f, -1.0f, -1.0f),
    QVector3D(-1.0f,  1.0f, -1.0f),
    QVector3D(-1.0f,  1.0f,  1.0f),

    // right
    QVector3D( 1.0f,  1.0f, -1.0f),
    QVector3D( 1.0f, -1.0f, -1.0f),
    QVector3D( 1.0f, -1.0f,  1.0f),

    QVector3D( 1.0f, -1.0f,  1.0f),
    QVector3D( 1.0f,  1.0f,  1.0f),
    QVector3D( 1.0f,  1.0f, -1.0f),

    // top
    QVector3D(-1.0f,  1.0f, -1.0f),
    QVector3D( 1.0f,  1.0f, -1.0f),
    QVector3D( 1.0f,  1.0f,  1.0f),

    QVector3D( 1.0f,  1.0f,  1.0f),
    QVector3D(-1.0f,  1.0f,  1.0f),
    QVector3D(-1.0f,  1.0f, -1.0f),

    // bottom
    QVector3D( 1.0f, -1.0f, -1.0f),
    QVector3D(-1.0f, -1.0f, -1.0f),
    QVector3D(-1.0f, -1.0f,  1.0f),

    QVector3D(-1.0f, -1.0f,  1.0f),
    QVector3D( 1.0f, -1.0f,  1.0f),
    QVector3D( 1.0f, -1.0f, -1.0f)
};

/***************************************/
static void wakeup(void *ctx)
{
    QMetaObject::invokeMethod((MpvWidget*)ctx, &MpvWidget::on_mpv_events, Qt::QueuedConnection);
}

static void *get_proc_address(void *ctx, const char *name)
{
    Q_UNUSED(ctx);
    QOpenGLContext *glctx = QOpenGLContext::currentContext();
    if (!glctx)
        return nullptr;
    return reinterpret_cast<void *>(glctx->getProcAddress(QByteArray(name)));
}

MpvWidget::MpvWidget() :
    m_indexBo(QOpenGLBuffer::IndexBuffer)
    //m_cubeVbo(QOpenGLBuffer::VertexBuffer)
{
    setFlag(Qt::Dialog);

    video_projection_mode = SideBySide;
    invert_stereo = false;

    setlocale(LC_NUMERIC, "C");
    m_mpv = mpv_create();

    if (!m_mpv)
        throw std::runtime_error("could not create mpv context");

    mpv_set_option_string(m_mpv, "terminal", "yes");
    if (mpv_initialize(m_mpv) < 0)
        throw std::runtime_error("could not initialize mpv context");
//    mpv_set_option_string(m_mpv, "msg-level", "all=v");
    mpv_set_option_string(m_mpv, "input-default-bindings", "yes");
    mpv_set_option_string(m_mpv, "osc", "no");
    mpv_set_option_string(m_mpv, "osd-bar", "no");

    mpv_set_option_string(m_mpv, "save-position-on-quit", "yes");
    mpv_set_option_string(m_mpv, "keep-open-pause", "no");
    mpv_set_option_string(m_mpv, "keep-open", "yes");
    const QString watchLaterDir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/mpv/watch_later/";
    mpv_set_option_string(m_mpv, "watch-later-directory", watchLaterDir.toUtf8().constData());

    //mpv::qt::set_option_variant(m_mpv, "hwdec", "auto");

    mpv_observe_property(m_mpv, 0, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "playback-time", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, 0, "width", MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, 0, "height", MPV_FORMAT_INT64);
    mpv_set_wakeup_callback(m_mpv, wakeup, this);

    m_updateFboTimer.setSingleShot(true);
    m_updateFboTimer.setInterval(10);
    connect(&m_updateFboTimer, &QTimer::timeout, this, &MpvWidget::resizeFbo);

    m_ohmd = new OhmdHandler(this);

    connect(qGuiApp, &QGuiApplication::screenAdded, this, &MpvWidget::onScreenAdded);

    setMinimumSize(QSize(640, 480));
}

MpvWidget::~MpvWidget()
{
    makeCurrent();
    if (m_mpvGl)
        mpv_render_context_free(m_mpvGl);
    mpv_terminate_destroy(m_mpv);
}

void MpvWidget::play(const char *path)
{
    m_path = path;

    if (m_mpvGl)
    {
        const char *args[] = {"loadfile", path, NULL};
        mpv_command(m_mpv, args);
        m_path = nullptr;
    }
    else
    {
        qWarning() << "init gl not done yet";
    }
}

/*
 * Data used to seed our vertex array and element array buffers:
 */
static const GLfloat g_vertex_buffer_data[] =
{
    -1.0f, -1.0f,
        1.0f, -1.0f,
        -1.0f,  1.0f,
        1.0f,  1.0f
    };
static const GLushort g_element_buffer_data[] = { 0, 1, 2, 3 };

void GLAPIENTRY s_messageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* userParam)
{
    fprintf( stderr, "GL CALLBACK: %s type = 0x%x, severity = 0x%x, message = %s\n",
             ( type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : "" ),
             type, severity, message );
}

void MpvWidget::initializeGL()
{
    glEnable (GL_DEBUG_OUTPUT);
    QOpenGLExtraFunctions(context()).glDebugMessageCallback(s_messageCallback, 0);

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize);

    for (const QScreen *screen : qApp->screens())
    {
        qDebug() << "during init" << screen->refreshRate() << screen->size();
    }
    /* Sphere shader */
    m_sphereShader = new QOpenGLShaderProgram(this);
    m_sphereShader->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/shader/sphere.vert");
    const char *fragsource =
        "#version 330\n"
        "uniform sampler2D warpTexture;\n"
        "in vec2 T;\n"
        "out vec4 color;\n"
        "void main(void)\n"
        "{\n"
        "    color = vec4(1.0, 1.0, 0.0, 1.0);\n"
        "}\n";
    //m_sphereShader->addShaderFromSourceCode(QOpenGLShader::Fragment, fragsource);
    m_sphereShader->addShaderFromSourceFile(QOpenGLShader::Fragment, ":/shader/sphere.frag");
    m_sphereShader->link();

    m_sphereShader->bind();
    m_sphereShader->setUniformValue("tex_uni", 0);

    m_sphereShader->bindAttributeLocation("vertex_attr", 0);

    m_cubeVao.create();
    m_cubeVao.bind();

    m_cubeVbo.create();
    m_cubeVbo.bind();
    m_cubeVbo.setUsagePattern(QOpenGLBuffer::StaticDraw);

    const quint32 slices = 50;
    const quint32 stacks = 50;
    nIndices = slices * stacks;

    m_cubeVbo.allocate(cube_vertices, sizeof(cube_vertices));
    nIndices = sizeof(cube_vertices)/sizeof(cube_vertices[0]);
    qDebug() << "indices" << nIndices << sizeof(cube_vertices) << sizeof(cube_vertices[0]);
    //m_cubeVbo.release();

    m_sphereShader->enableAttributeArray(0);
    m_sphereShader->setAttributeBuffer(0, GL_FLOAT, 0, 3);

    m_sphereShader->release();
    m_cubeVbo.release();
    //m_indexBo.release();
    m_cubeVao.release();



    m_videoFbo = new QOpenGLFramebufferObject(size());
    m_videoFbo->bind();

    mpv_opengl_init_params gl_init_params{get_proc_address, nullptr};
    mpv_render_param params[]
    {
        {MPV_RENDER_PARAM_API_TYPE, const_cast<char *>(MPV_RENDER_API_TYPE_OPENGL)},
        {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &gl_init_params},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };

    if (mpv_render_context_create(&m_mpvGl, m_mpv, params) < 0)
        throw std::runtime_error("failed to initialize mpv GL context");
    mpv_render_context_set_update_callback(m_mpvGl, MpvWidget::on_update, reinterpret_cast<void *>(this));

    if (m_path)
    {
        const char *args[] = {"loadfile", m_path, NULL};
        mpv_command(m_mpv, args);
        m_path = nullptr;
    }
}

void MpvWidget::paintGL()
{
    if (!m_videoFbo)
    {
        return;
    }

    mpv_opengl_fbo mpfbo{static_cast<int>(m_videoFbo->handle()), m_videoFbo->width(), m_videoFbo->height(), GL_RGBA8};
    int flip_y{0};

    mpv_render_param params[] =
    {
        {MPV_RENDER_PARAM_OPENGL_FBO, &mpfbo},
        {MPV_RENDER_PARAM_FLIP_Y, &flip_y},
        {MPV_RENDER_PARAM_INVALID, nullptr}
    };
    mpv_render_context_render(m_mpvGl, params);


    m_ohmd->update();

    makeCurrent();
    glClear(GL_COLOR_BUFFER_BIT);
    //glEnable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    //glDisable(GL_BLEND);
    //glDepthMask(GL_FALSE);

    renderEye(0, m_ohmd->modelView[0], m_ohmd->projection[0]);
    renderEye(1, m_ohmd->modelView[1], m_ohmd->projection[1]);

    makeCurrent();

//    if (!m_posImage.isNull()) {
//        QPainter p(this);
//        p.setRenderHint(QPainter::Antialiasing);
//        p.setPen(Qt::transparent);

//        p.setBrush(QColor(255, 255, 255, 64));
//        p.drawPath(m_posString);
//        p.setBrush(QColor(0, 0, 0, 32));
//        p.drawPath(m_posStringStroke);

//        p.drawImage(0, 0, m_posImage);
//        p.drawImage(width() / 2, 0, m_posImage);
//        p.translate(width()/2, 0);
//        p.drawPath(m_posString);
//        p.setBrush(QColor(0, 0, 0, 32));
//        p.drawPath(m_posStringStroke);
    //    }
}

void MpvWidget::showEvent(QShowEvent *e)
{
    qWarning() << "===============" << e;
}

void MpvWidget::on_mpv_events()
{
    // Process all events, until the event queue is empty.
    while (m_mpv)
    {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE)
        {
            break;
        }
        handle_mpv_event(event);
    }
}

void MpvWidget::handle_mpv_event(mpv_event *event)
{
    switch (event->event_id)
    {
    case MPV_EVENT_PROPERTY_CHANGE:
    {
        mpv_event_property *prop = (mpv_event_property *)event->data;
        if (strcmp(prop->name, "playback-time") == 0)
        {
            if (prop->format == MPV_FORMAT_DOUBLE)
            {
                int time = (*(double *)prop->data) * 1000;
                if (time == m_position)
                {
                    return;
                }
                m_position = time;
                Q_EMIT positionChanged(time);
            }
        }
        else if (strcmp(prop->name, "duration") == 0)
        {
            if (prop->format == MPV_FORMAT_DOUBLE)
            {
                int time = (*(double *)prop->data) * 1000.;
                if (time == m_duration)
                {
                    return;
                }
                m_duration = time;
                Q_EMIT durationChanged(time);
            }
        }
        else if (strcmp(prop->name, "width") == 0)
        {
            if (prop->format != MPV_FORMAT_INT64)
            {
                return;
            }
            m_videoWidth = (*(int64_t *)prop->data);
            m_updateFboTimer.start();
        }
        else if (strcmp(prop->name, "height") == 0)
        {
            if (prop->format != MPV_FORMAT_INT64)
            {
                return;
            }
            m_videoHeight = (*(int64_t *)prop->data);
            m_updateFboTimer.start();
        }
        else
        {
            return;
        }
        break;
    }
    default:
        ;
        return;
    }
//    QString posString = QTime::fromMSecsSinceStartOfDay(m_position).toString() + "/" + QTime::fromMSecsSinceStartOfDay(m_duration).toString();
//    QPainterPath posPath;

//    QFont font = qApp->font();
//    font.setPointSize(20);
//    font.setBold(true);
//    posPath.addText(10, QFontMetrics(font).ascent(), font, posString);

//    QPainterPathStroker stroker;
//    stroker.setWidth(2);
//    QPainterPath posPathOutline = stroker.createStroke(posPath);
//    const QRect outlineRect = posPathOutline.boundingRect().toAlignedRect();
//    m_posImage = QImage(outlineRect.right(), outlineRect.bottom(), QImage::Format_ARGB32_Premultiplied);
//    m_posImage.fill(Qt::transparent);
//    QPainter p(&m_posImage);
//    p.setPen(QPen(QColor(0, 0, 0, 32), 2));
//    p.setBrush(QColor(255, 255, 255, 64));
//    p.drawPath(posPath);
}

// Make Qt invoke mpv_opengl_cb_draw() to draw a new/updated video frame.
void MpvWidget::maybeUpdate()
{
    // If the Qt window is not visible, Qt's update() will just skip rendering.
    // This confuses mpv's opengl-cb API, and may lead to small occasional
    // freezes due to video rendering timing out.
    // Handle this by manually redrawing.
    // Note: Qt doesn't seem to provide a way to query whether update() will
    //       be skipped, and the following code still fails when e.g. switching
    //       to a different workspace with a reparenting window manager.
//    if (!isExposed()) {
//        makeCurrent();
//        paintGL();
//        context()->swapBuffers(context()->surface());
//        doneCurrent();
//    } else {
    update();
    //    }
}

void MpvWidget::onScreenAdded()
{
    // quick hack to try to position into the correct display
    if (m_ohmd->displaySize.isEmpty())
    {
        qWarning() << "Display size not fetched!";
        return;
    }

    if (screen()->size() == m_ohmd->displaySize)
    {
        qDebug() << "Already correct size";
        return;
    }

    for (QScreen *other : qApp->screens())
    {
        if (other == screen())
        {
            continue;
        }
        if (other->size() == m_ohmd->displaySize)
        {
            qDebug() << "Moving from" << screen() << "to" << other->geometry() << geometry();
            disconnect(qGuiApp, &QGuiApplication::screenAdded, this, &MpvWidget::onScreenAdded);
            setGeometry(other->geometry());
            return;
        }
    }
    qDebug() << "No other screens?";

}

void MpvWidget::renderEye(int eye, const QMatrix4x4 &modelview, QMatrix4x4 projectionl)
{
    int w = width();
    int h = height();

    int eye_inv = invert_stereo ? 1 - eye : eye;

    if (eye_inv == 1)
    {
        glViewport(w/2, 0, w/2, h);
    }
    else
    {
        glViewport(0, 0, w/2, h);
    }

    m_sphereShader->bind();

    QMatrix4x4 projection;
    projection.perspective(m_fieldOfView, ((float)(w/2)) / (float)h, 0.1f, 1000.0f);
    projection.rotate(m_rotHor, QVector3D(0, 1, 0));
    projection.rotate(m_rotVert, QVector3D(1, 0, 0));
    //projection.translate(0, 0, -1);
//    qDebug() << "==========" << eye << "=============";
//    qDebug() << projection;
//    qDebug() << projectionl;
//    qDebug() << projectionl.inverted();

    m_sphereShader->setUniformValue("modelview_projection_uni", projection * modelview);

//    m_sphereShader->setUniformValue("mvp", projection * modelview);
//    m_sphereShader->setUniformValue("WarpScale", m_ohmd->warp_scale * m_ohmd->warp_adj);
//    m_sphereShader->setUniformValueArray("HmdWarpParam", m_ohmd->distortion_coeffs, 4, 1);
//    m_sphereShader->setUniformValueArray("LensCenter", eye == 0 ? m_ohmd->left_lens_center : m_ohmd->right_lens_center, 2, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_videoFbo->texture());

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);


    switch(video_projection_mode)
    {
    case Monoscopic:
        m_sphereShader->setUniformValue("min_max_uv_uni", 0.0f, 0.0f, 1.0f, 1.0f);
        break;
    case OverUnder:
        if(eye_inv == 1)
            m_sphereShader->setUniformValue("min_max_uv_uni", 0.0f, 0.5f, 1.0f, 1.0f);
        else
            m_sphereShader->setUniformValue("min_max_uv_uni", 0.0f, 0.0f, 1.0f, 0.5f);
        break;
    case SideBySide:
        if(eye_inv == 1)
        {
            m_sphereShader->setUniformValue("min_max_uv_uni",
                                            0.5f, 0.0f,
                                            1.0f, 1.0f);
        }
        else
        {
            m_sphereShader->setUniformValue("min_max_uv_uni",
                                            0.0f, 0.0f,
                                            0.5f, 1.0f);
        }
        break;
    }
    if (eye == 0)
    {
        m_sphereShader->setUniformValue("eye_offset", 0.f);//-m_ohmd->horiz_sep);
    }
    else
    {
        m_sphereShader->setUniformValue("eye_offset", 0.f);//m_ohmd->horiz_sep);
    }

    m_sphereShader->setUniformValue("projection_angle_factor_uni", 360.0f / float(videoAngle));

    //glBegin(GL_QUADS);
    //glTexCoord2d( 0,  0);
    //glVertex3d(  -1, -1, 0);
    //glTexCoord2d( 1,  0);
    //glVertex3d(   0, -1, 0);
    //glTexCoord2d( 1,  1);
    //glVertex3d(   0,  1, 0);
    //glTexCoord2d( 0,  1);
    //glVertex3d(  -1,  1, 0);
    //glEnd();

    //glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereVbo[2]);
    //glDrawElements(GL_TRIANGLE_STRIP, nIndices, GL_UNSIGNED_SHORT, nullptr);
    //glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    m_cubeVao.bind();
    glDrawArrays(GL_TRIANGLES, 0, 6 * 6);
    m_cubeVao.release();

    //m_indexBo.release();

    //QOpenGLFunctions *f = context()->functions();

    //qDebug() << "getting";
    //int vertexattr = m_sphereShader->attributeLocation("vertex_attr");
    //qDebug() << vertexattr;
    ////m_sphereShader->enableAttributeArray(vertexattr);
    //m_cubeVbo.bind();
    //qDebug() << "bound";
    //f->glEnableVertexAttribArray(vertexattr);
    //f->glVertexAttribPointer(vertexattr, 3, GL_FLOAT, 0, 0, 0);
    ////m_sphereShader->setAttributeBuffer(vertexattr, GL_FLOAT, 0, 3);
    //qDebug() << "drawing";
    //glDrawArrays(GL_TRIANGLE_FAN, 0, nIndices);
    //qDebug() << "releasing";
    //m_cubeVbo.release();

    m_sphereShader->release();

}

void MpvWidget::on_update(void *ctx)
{
    QMetaObject::invokeMethod((MpvWidget*)ctx, "maybeUpdate");
}

void MpvWidget::resizeFbo()
{
    if (m_videoWidth <= 0 || m_videoHeight <= 0)
    {
        return;
    }
    QSize videoSize(m_videoWidth, m_videoHeight);
    qDebug() << m_maxTextureSize;
    if (m_videoWidth > m_maxTextureSize || m_videoHeight > m_maxTextureSize)
    {
        QSize maxSize(m_maxTextureSize, m_maxTextureSize);
        videoSize = videoSize.scaled(maxSize, Qt::KeepAspectRatio);
    }
    if (m_videoFbo->size() == videoSize)
    {
        return;
    }
    qDebug() << "new size" << videoSize;
    delete m_videoFbo;
    m_videoFbo = new QOpenGLFramebufferObject(videoSize);
}

void MpvWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal)
    {
        m_fieldOfView-= 10;
        if (m_fieldOfView < 45)
        {
            m_fieldOfView = 45;
        }
        return;
    }
    if (event->key() == Qt::Key_Minus)
    {
        m_fieldOfView += 10;
        if (m_fieldOfView > 150)
        {
            m_fieldOfView = 150;
        }
        return;
    }
    if (event->key() == Qt::Key_Escape)
    {
        m_rotVert = 0;
        m_rotHor = 0;
        m_fieldOfView = DEFAULT_FOV;
        return;
    }

    if (event->key() == Qt::Key_W)
    {
        m_rotVert--;
        return;
    }
    if (event->key() == Qt::Key_S)
    {
        m_rotVert++;
        return;
    }
    if (event->key() == Qt::Key_A)
    {
        m_rotHor--;
        return;
    }
    if (event->key() == Qt::Key_D)
    {
        m_rotHor++;
        return;
    }

    if (event->key() == Qt::Key_Q)
    {
        close();
        return;
    }

    if (event->key() == Qt::Key_Shift ||
            event->key() == Qt::Key_Control ||
            event->key() == Qt::Key_Meta ||
            event->key() == Qt::Key_Alt)
    {
        return;
    }

    const QString sequenceString = QKeySequence(event->key() + event->modifiers()).toString();
    for (const QChar &c : sequenceString)
    {
        if (!c.isPrint())
        {
            return;
        }
    }

    QByteArray keyString = sequenceString.toLower().toUtf8();

    const QHash<QByteArray, QByteArray> mpvMapping(
    {
        {"pgdown", "pgdwn"},
        {"backspace", "bs"},
        {"return", "enter"},
    });

    if (mpvMapping.contains(keyString))
    {
        keyString = mpvMapping[keyString];
    }

    if (keyString.endsWith('+'))
    {
        keyString.chop(1);
    }

    const char *args[] = {"keypress", keyString.constData(), NULL};
    mpv_command(m_mpv, args);
//    switch(event->key()) {
//    case Qt::Key_Right:
//        break;
//    case Qt::Key_Left:
//        break;
//    }
}
