// Copyright (C) 2017 The Qt Company Ltd.
// SPDX-License-Identifier: LicenseRef-Qt-Commercial OR BSD-3-Clause

#include "window_singlethreaded.h"
#include "cuberenderer.h"
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOffscreenSurface>
#include <QScreen>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickRenderControl>
#include <QCoreApplication>
#include <QQuickRenderTarget>
#include <QQuickGraphicsDevice>

#include "qmloffscreenrenderer.h"


WindowSingleThreaded::WindowSingleThreaded()
    : m_rootItem(nullptr),

      m_quickInitialized(false)
{
    setSurfaceType(QSurface::OpenGLSurface);

    QSurfaceFormat format;
    // Qt Quick may need a depth and stencil buffer. Always make sure these are available.
    format.setDepthBufferSize(16);
    format.setStencilBufferSize(8);
    setFormat(format);

    m_context = new QOpenGLContext;
    m_context->setFormat(format);
    m_context->create();



    m_qmlRenderer = new QmlOffscreenRenderer(this, m_context, this);


//--------------------------------------
    //to wypierdolić
    m_quickWindow = m_qmlRenderer->m_quickWindow;
    m_renderControl = m_qmlRenderer->m_renderControl;
    m_qmlEngine = m_qmlRenderer->m_qmlEngine;
    m_updateTimer = &(m_qmlRenderer->m_updateTimer);
    m_offscreenSurface = m_qmlRenderer->m_offscreenSurface;
//-------------------------------------

    m_cubeRenderer = new CubeRenderer(m_offscreenSurface);


    connect(m_qmlRenderer, &QmlOffscreenRenderer::textureUpdated, this, &WindowSingleThreaded::render);



    // Now hook up the signals. For simplicy we don't differentiate between
    // renderRequested (only render is needed, no sync) and sceneChanged (polish and sync
    // is needed too).




    // Just recreating the texture on resize is not sufficient, when moving between screens
    // with different devicePixelRatio the QWindow size may remain the same but the texture
    // dimension is to change regardless.
    connect(this, &QWindow::screenChanged, this, &WindowSingleThreaded::handleScreenChange);
}

WindowSingleThreaded::~WindowSingleThreaded()
{
    // Make sure the context is current while doing cleanup. Note that we use the
    // offscreen surface here because passing 'this' at this point is not safe: the
    // underlying platform window may already be destroyed. To avoid all the trouble, use
    // another surface that is valid for sure.
    m_context->makeCurrent(m_offscreenSurface);

    delete m_cubeRenderer;




    delete m_qmlComponent;
    delete m_qmlEngine;
    delete m_quickWindow;
    delete m_renderControl;

    if (m_qmlRenderer->m_textureId)
        m_context->functions()->glDeleteTextures(1, &(m_qmlRenderer->m_textureId));

    m_context->doneCurrent();



    delete m_offscreenSurface;
    delete m_context;
}





void WindowSingleThreaded::render()
{
    if(!m_context->makeCurrent(this))
        return;

    // Get something onto the screen.
    m_cubeRenderer->render(this, m_context, m_qmlRenderer->m_quickReady ? m_qmlRenderer->m_textureId : 0);
}



void WindowSingleThreaded::run()
{
    disconnect(m_qmlComponent, &QQmlComponent::statusChanged, this, &WindowSingleThreaded::run);

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

void WindowSingleThreaded::updateSizes()
{
    // Behave like SizeRootObjectToView.
    m_rootItem->setWidth(width());
    m_rootItem->setHeight(height());

    m_quickWindow->setGeometry(0, 0, width(), height());

    m_cubeRenderer->resize(width(), height());
}

void WindowSingleThreaded::startQuick(const QString &filename)
{
    m_qmlComponent = new QQmlComponent(m_qmlEngine, QUrl(filename));
    if (m_qmlComponent->isLoading())
        connect(m_qmlComponent, &QQmlComponent::statusChanged, this, &WindowSingleThreaded::run);
    else
        run();
}

void WindowSingleThreaded::exposeEvent(QExposeEvent *)
{
    if (isExposed()) {
        if (!m_quickInitialized) {
            m_cubeRenderer->render(this, m_context, m_qmlRenderer->m_quickReady ?  m_qmlRenderer->m_textureId : 0);
            startQuick(QStringLiteral("qrc:/qt/qml/rendercontrol/demo.qml"));
        }
    }
}

void WindowSingleThreaded::resizeTexture()
{
    if (m_rootItem && m_context->makeCurrent(m_offscreenSurface)) {
        m_context->functions()->glDeleteTextures(1, &(m_qmlRenderer->m_textureId));
         m_qmlRenderer->m_textureId = 0;
        m_qmlRenderer->createTexture();
        m_context->doneCurrent();
        updateSizes();
        render();
    }
}

void WindowSingleThreaded::resizeEvent(QResizeEvent *)
{
    // If this is a resize after the scene is up and running, recreate the texture and the
    // Quick item and scene.
    if (m_qmlRenderer->m_textureId && m_qmlRenderer->m_textureSize != size() * devicePixelRatio())
        resizeTexture();
}

void WindowSingleThreaded::handleScreenChange()
{
    if (m_qmlRenderer->m_dpr != devicePixelRatio())
        resizeTexture();
}

void WindowSingleThreaded::mousePressEvent(QMouseEvent *e)
{
    // Use the constructor taking position and globalPosition. That puts position into the
    // event's position and scenePosition, and globalPosition into the event's globalPosition. This way
    // the scenePosition in e is ignored and is replaced by position. This is necessary
    // because QQuickWindow thinks of itself as a top-level window always.
    QMouseEvent mappedEvent(e->type(), e->position(), e->globalPosition(), e->button(), e->buttons(), e->modifiers());
    QCoreApplication::sendEvent(m_quickWindow, &mappedEvent);
}

void WindowSingleThreaded::mouseReleaseEvent(QMouseEvent *e)
{
    QMouseEvent mappedEvent(e->type(), e->position(), e->globalPosition(), e->button(), e->buttons(), e->modifiers());
    QCoreApplication::sendEvent(m_quickWindow, &mappedEvent);
}

void WindowSingleThreaded::keyPressEvent(QKeyEvent *e)
{
    QCoreApplication::sendEvent(m_quickWindow, e);
}

void WindowSingleThreaded::keyReleaseEvent(QKeyEvent *e)
{
    QCoreApplication::sendEvent(m_quickWindow, e);
}
