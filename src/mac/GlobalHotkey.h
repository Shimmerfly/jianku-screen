#pragma once

#include <QObject>
#include <QString>

class GlobalHotkey final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
public:
    explicit GlobalHotkey(QObject *parent = nullptr);
    ~GlobalHotkey() override;
    QString status() const { return status_; }
    Q_INVOKABLE void setShortcut(const QString &shortcut);
signals:
    void activated();
    void statusChanged();
private:
    void *hotkey_ = nullptr;
    void *handler_ = nullptr;
    QString registeredShortcut_;
    QString status_;
    void setStatus(const QString &status);
};
