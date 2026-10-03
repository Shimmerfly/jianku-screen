#pragma once

#include <QQuickItem>

class VideoFrameStore;
class QSGNode;

class VideoSurface : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject *frameStore READ frameStore WRITE setFrameStore NOTIFY frameStoreChanged)

public:
    explicit VideoSurface(QQuickItem *parent = nullptr);
    QObject *frameStore() const;
    void setFrameStore(QObject *store);

signals:
    void frameStoreChanged();

protected:
    QSGNode *updatePaintNode(QSGNode *oldNode, UpdatePaintNodeData *) override;

private:
    VideoFrameStore *store_ = nullptr;
    QMetaObject::Connection frameConnection_;
};
