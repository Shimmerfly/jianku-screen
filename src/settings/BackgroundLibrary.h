#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

class BackgroundLibrary final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList entries READ entries NOTIFY entriesChanged)
    Q_PROPERTY(QString rootPath READ rootPath CONSTANT)

public:
    explicit BackgroundLibrary(QObject *parent = nullptr);

    QVariantList entries() const { return entries_; }
    QString rootPath() const { return rootPath_; }

    Q_INVOKABLE QString urlFor(const QString &relative) const;
    Q_INVOKABLE void refresh();

signals:
    void entriesChanged();

private:
    void scan();

    QVariantList entries_;
    QString rootPath_;
};
