#pragma once

#include <QObject>
#include <QVariantMap>

class QuickScreenshot final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    explicit QuickScreenshot(QObject *parent = nullptr);
    QString status() const { return status_; }
    Q_INVOKABLE void capture(const QVariantMap &options);
signals:
    void statusChanged();
private:
    QString status_;
    void setStatus(const QString &status);
};
