#include "qmloffscreenrenderer.h"

#include <QQmlEngine>
#include <QQuickRenderControl>
#include <QQuickWindow>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>

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
