#include "qmloffscreenrenderer.h"

#include <QQmlEngine>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQuickRenderTarget>


class RenderControl : public QQuickRenderControl
{
public:
    RenderControl(QWindow *w)
        : m_window(w)
    {}
    QWindow *renderWindow(QPoint *offset) override;

private:
    QWindow *m_window;
};

QWindow *RenderControl::renderWindow(QPoint *offset)
{
    if (offset)
        *offset = QPoint(0, 0);
    return m_window;
}

QmlOffscreenRenderer::QmlOffscreenRenderer(QWindow *hostWindow,
                                           QOpenGLContext *context,
                                           QObject *parent)
    : QObject(parent)
    ,  m_quickReady(false)
    ,       m_textureId(0)
    ,       m_dpr(0)
    , m_hostWindow(hostWindow)
    , m_context(context)
{
    m_offscreenSurface = new QOffscreenSurface;
    // Pass m_context->format(), not format. Format does not specify and color buffer
    // sizes, while the context, that has just been created, reports a format that has
    // these values filled in. Pass this to the offscreen surface to make sure it will be
    // compatible with the context's configuration.
    m_offscreenSurface->setFormat(m_context->format());
    m_offscreenSurface->create();


    m_renderControl = new RenderControl(m_hostWindow);

    // Create a QQuickWindow that is associated with out render control. Note that this
    // window never gets created or shown, meaning that it will never get an underlying
    // native (platform) window.
    m_quickWindow = new QQuickWindow(m_renderControl);

    // Create a QML engine.
    m_qmlEngine = new QQmlEngine;
    if (!m_qmlEngine->incubationController())
        m_qmlEngine->setIncubationController(m_quickWindow->incubationController());

    // When Quick says there is a need to render, we will not render immediately. Instead,
    // a timer with a small interval is used to get better performance.
    m_updateTimer.setSingleShot(true);
    m_updateTimer.setInterval(5);
    connect(&m_updateTimer, &QTimer::timeout, this, &QmlOffscreenRenderer::render);

        connect(m_quickWindow, &QQuickWindow::sceneGraphInitialized, this, &QmlOffscreenRenderer::createTexture);
        connect(m_quickWindow, &QQuickWindow::sceneGraphInvalidated, this, &QmlOffscreenRenderer::destroyTexture);

        connect(m_renderControl, &QQuickRenderControl::renderRequested, this, &QmlOffscreenRenderer::requestUpdate);
        connect(m_renderControl, &QQuickRenderControl::sceneChanged, this, &QmlOffscreenRenderer::requestUpdate);
}

QmlOffscreenRenderer::~QmlOffscreenRenderer() {}

void QmlOffscreenRenderer::render()
{
    if (!m_context->makeCurrent(m_offscreenSurface))
        return;

    // Polish, synchronize and render the next frame (into our texture).  In this example
    // everything happens on the same thread and therefore all three steps are performed
    // in succession from here. In a threaded setup the render() call would happen on a
    // separate thread.
    m_renderControl->beginFrame();
    m_renderControl->polishItems();
    m_renderControl->sync();
    m_renderControl->render();
    m_renderControl->endFrame();

    QOpenGLFramebufferObject::bindDefault();
    m_context->functions()->glFlush();

    m_quickReady = true;

    Q_EMIT textureUpdated();
}

void QmlOffscreenRenderer::destroyTexture()
{
    m_context->functions()->glDeleteTextures(1, &(m_textureId));
    m_textureId = 0;
}

void QmlOffscreenRenderer::requestUpdate()
{
    if (!m_updateTimer.isActive())
        m_updateTimer.start();
}

void QmlOffscreenRenderer::createTexture()
{
    // The scene graph has been initialized. It is now time to create an texture and associate
    // it with the QQuickWindow.
    m_dpr = m_hostWindow->devicePixelRatio();
    m_textureSize = m_hostWindow->size() * m_dpr;
    QOpenGLFunctions *f = m_context->functions();
    f->glGenTextures(1, &m_textureId);
    f->glBindTexture(GL_TEXTURE_2D, m_textureId);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_textureSize.width(), m_textureSize.height(), 0,
                    GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    m_quickWindow->setRenderTarget(QQuickRenderTarget::fromOpenGLTexture(m_textureId, m_textureSize));
}
