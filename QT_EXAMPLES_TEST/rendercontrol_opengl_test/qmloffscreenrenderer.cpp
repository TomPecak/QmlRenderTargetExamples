#include "qmloffscreenrenderer.h"

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLFramebufferObject>
#include <QOpenGLFunctions>
#include <QQmlEngine>
#include <QQuickGraphicsDevice>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QEvent>
#include <QExposeEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QResizeEvent>

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
    , m_quickReady(false)
    , m_textureId(0)
    , m_dpr(0)
    , m_hostWindow(hostWindow)
    , m_context(context)
    , m_rootItem(nullptr)
    , m_quickInitialized(false)
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

    // Now hook up the signals. For simplicy we don't differentiate between
    // renderRequested (only render is needed, no sync) and sceneChanged (polish and sync
    // is needed too).

    connect(m_quickWindow,
            &QQuickWindow::sceneGraphInitialized,
            this,
            &QmlOffscreenRenderer::createTexture);
    connect(m_quickWindow,
            &QQuickWindow::sceneGraphInvalidated,
            this,
            &QmlOffscreenRenderer::destroyTexture);
    connect(m_renderControl,
            &QQuickRenderControl::renderRequested,
            this,
            &QmlOffscreenRenderer::requestUpdate);
    connect(m_renderControl,
            &QQuickRenderControl::sceneChanged,
            this,
            &QmlOffscreenRenderer::requestUpdate);
}

QmlOffscreenRenderer::~QmlOffscreenRenderer()
{
    m_context->makeCurrent(m_offscreenSurface);

    delete m_qmlComponent;
    delete m_qmlEngine;
    delete m_quickWindow;
    delete m_renderControl;

    if (m_textureId)
        m_context->functions()->glDeleteTextures(1, &m_textureId);

    delete m_offscreenSurface;
}

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

void QmlOffscreenRenderer::updateSizes()
{
    // Behave like SizeRootObjectToView.
    m_rootItem->setWidth(m_hostWindow->width());
    m_rootItem->setHeight(m_hostWindow->height());

    m_quickWindow->setGeometry(0, 0, m_hostWindow->width(), m_hostWindow->height());
}

void QmlOffscreenRenderer::startQuick(const QString &filename)
{
    m_qmlComponent = new QQmlComponent(m_qmlEngine, QUrl(filename));
    if (m_qmlComponent->isLoading())
        connect(m_qmlComponent, &QQmlComponent::statusChanged, this, &QmlOffscreenRenderer::run);
    else
        run();
}

void QmlOffscreenRenderer::resizeTexture()
{
    if (m_rootItem && m_context->makeCurrent(m_offscreenSurface)) {
        m_context->functions()->glDeleteTextures(1, &(m_textureId));
        m_textureId = 0;
        createTexture();
        m_context->doneCurrent();
        updateSizes();
        render();
    }
}

void QmlOffscreenRenderer::exposeEvent(QExposeEvent *e) {}

void QmlOffscreenRenderer::resizeEvent(QResizeEvent *e) {}

void QmlOffscreenRenderer::mousePressEvent(QMouseEvent *e) {
    // Use the constructor taking position and globalPosition. That puts position into the
    // event's position and scenePosition, and globalPosition into the event's globalPosition. This way
    // the scenePosition in e is ignored and is replaced by position. This is necessary
    // because QQuickWindow thinks of itself as a top-level window always.
    QMouseEvent mappedEvent(e->type(), e->position(), e->globalPosition(), e->button(), e->buttons(), e->modifiers());
    QCoreApplication::sendEvent(m_quickWindow, &mappedEvent);
}

void QmlOffscreenRenderer::mouseReleaseEvent(QMouseEvent *e) {
    QMouseEvent mappedEvent(e->type(), e->position(), e->globalPosition(), e->button(), e->buttons(), e->modifiers());
    QCoreApplication::sendEvent(m_quickWindow, &mappedEvent);
}

void QmlOffscreenRenderer::keyPressEvent(QKeyEvent *e) {
      QCoreApplication::sendEvent(m_quickWindow, e);
}

void QmlOffscreenRenderer::keyReleaseEvent(QKeyEvent *e) {
       QCoreApplication::sendEvent(m_quickWindow, e);
}

void QmlOffscreenRenderer::run()
{
    disconnect(m_qmlComponent, &QQmlComponent::statusChanged, this, &QmlOffscreenRenderer::run);

    if (m_qmlComponent->isError()) {
        const QList<QQmlError> errorList = m_qmlComponent->errors();
        for (const QQmlError &error : errorList)
            qWarning() << error.url() << error.line() << error;
        return;
    }

    QObject *rootObject = m_qmlComponent->create();
    if (m_qmlComponent->isError()) {
        const QList<QQmlError> errorList = m_qmlComponent->errors();
        for (const QQmlError &error : errorList)
            qWarning() << error.url() << error.line() << error;
        return;
    }

    m_rootItem = qobject_cast<QQuickItem *>(rootObject);
    if (!m_rootItem) {
        qWarning("run: Not a QQuickItem");
        delete rootObject;
        return;
    }

    // The root item is ready. Associate it with the window.
    m_rootItem->setParentItem(m_quickWindow->contentItem());

    // Update item and rendering related geometries.
    updateSizes();

    // Ensure key events are received by the root Rectangle.
    m_rootItem->forceActiveFocus();

    // Initialize the render control and our OpenGL resources.
    m_context->makeCurrent(m_offscreenSurface);
    m_quickWindow->setGraphicsDevice(QQuickGraphicsDevice::fromOpenGLContext(m_context));
    m_renderControl->initialize();
    m_quickInitialized = true;
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
    f->glTexImage2D(GL_TEXTURE_2D,
                    0,
                    GL_RGBA,
                    m_textureSize.width(),
                    m_textureSize.height(),
                    0,
                    GL_RGBA,
                    GL_UNSIGNED_BYTE,
                    nullptr);
    m_quickWindow->setRenderTarget(
        QQuickRenderTarget::fromOpenGLTexture(m_textureId, m_textureSize));
}
