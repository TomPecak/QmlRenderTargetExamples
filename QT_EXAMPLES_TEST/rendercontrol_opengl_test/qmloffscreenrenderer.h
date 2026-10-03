#pragma once

#include <QObject>
#include <QTimer>
#include <QSize>

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
QT_END_NAMESPACE

class QmlOffscreenRenderer : public QObject
{
    Q_OBJECT
public:
    explicit QmlOffscreenRenderer(QWindow *hostWindow, QOpenGLContext *context, QObject *parent = nullptr);
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

      public slots:
               void createTexture();

 signals:
     // Sygnał emitowany, gdy QML przeliczył animacje i wyrenderował nową klatkę do tekstury
     void textureUpdated();

private slots:
     void render();
         void destroyTexture();
             void requestUpdate();


private:



    QWindow *m_hostWindow;
};
