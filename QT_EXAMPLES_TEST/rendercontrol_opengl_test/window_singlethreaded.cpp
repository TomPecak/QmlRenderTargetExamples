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
     //m_cubeRenderer = new CubeRenderer(nullptr);


    connect(m_qmlRenderer, &QmlOffscreenRenderer::textureUpdated, this, &WindowSingleThreaded::render);








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

    delete m_qmlRenderer;
    m_context->doneCurrent();
    delete m_context;
}





void WindowSingleThreaded::render()
{
    if(!m_context->makeCurrent(this))
        return;

    // Get something onto the screen.
    m_cubeRenderer->render(this, m_context, m_qmlRenderer->m_quickReady ? m_qmlRenderer->m_textureId : 0);
}








void WindowSingleThreaded::exposeEvent(QExposeEvent *)
{
    if (isExposed()) {
        if (!m_qmlRenderer->m_quickInitialized) {
            m_cubeRenderer->render(this, m_context, m_qmlRenderer->m_quickReady ?  m_qmlRenderer->m_textureId : 0);
            m_qmlRenderer->startQuick(QStringLiteral("qrc:/qt/qml/rendercontrol/demo.qml"));
            m_cubeRenderer->resize(width(), height());
        }
    }
}



void WindowSingleThreaded::resizeEvent(QResizeEvent *)
{
    // If this is a resize after the scene is up and running, recreate the texture and the
    // Quick item and scene.
    if (m_qmlRenderer->m_textureId && m_qmlRenderer->m_textureSize != size() * devicePixelRatio()){
        m_qmlRenderer->resizeTexture();
        m_cubeRenderer->resize(width(), height());
    }
}

void WindowSingleThreaded::handleScreenChange()
{
    if (m_qmlRenderer->m_dpr != devicePixelRatio()){
        m_qmlRenderer->resizeTexture();
                m_cubeRenderer->resize(width(), height());
    }
}

void WindowSingleThreaded::mousePressEvent(QMouseEvent *e)
{
    m_qmlRenderer->mousePressEvent(e);

}

void WindowSingleThreaded::mouseReleaseEvent(QMouseEvent *e)
{
    m_qmlRenderer->mouseReleaseEvent(e);
}

void WindowSingleThreaded::keyPressEvent(QKeyEvent *e)
{

    m_qmlRenderer->keyPressEvent(e);
}

void WindowSingleThreaded::keyReleaseEvent(QKeyEvent *e)
{
    m_qmlRenderer->keyReleaseEvent(e);
}
