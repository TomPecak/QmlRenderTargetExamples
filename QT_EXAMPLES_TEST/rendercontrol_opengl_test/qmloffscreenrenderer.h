#pragma once

#include <QObject>
#include <QSize>
#include <QTimer>

QT_BEGIN_NAMESPACE
class QWindow;
class QOpenGLContext;
class QOffscreenSurface;
class QQuickRenderControl;
class QQuickWindow;
class QQmlEngine;
class QQmlComponent;
class QQuickItem;
class QEvent;
class QKeyEvent;
class QMouseEvent;
class QResizeEvent;
class QExposeEvent;
QT_END_NAMESPACE

class QmlOffscreenRenderer : public QObject
{
    Q_OBJECT
public:
    explicit QmlOffscreenRenderer(QWindow *hostWindow,
                                  QOpenGLContext *context,
                                  QObject *parent = nullptr);
    virtual ~QmlOffscreenRenderer() override;

public:
    QQuickWindow *m_quickWindow;
    QQuickRenderControl *m_renderControl;
    QQmlEngine *m_qmlEngine;
    QTimer m_updateTimer;
    QOpenGLContext *m_context;
    QOffscreenSurface *m_offscreenSurface;
    bool m_quickReady;
    qreal m_dpr;

    uint m_textureId;
    QSize m_textureSize;

    QQmlComponent *m_qmlComponent;
    QQuickItem *m_rootItem;

    bool m_quickInitialized;

    void startQuick(const QString &filename);


        void resizeTexture(); //move to private

    void exposeEvent(QExposeEvent *e);
    void resizeEvent(QResizeEvent *e);
    void mousePressEvent(QMouseEvent *e);
    void mouseReleaseEvent(QMouseEvent *e);
    void keyPressEvent(QKeyEvent *e);
    void keyReleaseEvent(QKeyEvent *e);

public slots:
    void createTexture();

signals:
    // Sygnał emitowany, gdy QML przeliczył animacje i wyrenderował nową klatkę do tekstury
    void textureUpdated();

private slots:
    void render();
    void destroyTexture();
    void requestUpdate();
    void run();
    void updateSizes();

private:


    QWindow *m_hostWindow;
};
