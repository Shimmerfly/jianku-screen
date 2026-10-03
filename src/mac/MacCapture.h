#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <memory>

class VideoFrameStore;

class MacCapture final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QStringList displayNames READ displayNames NOTIFY displayNamesChanged)
    Q_PROPERTY(QString status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged)
    Q_PROPERTY(bool recordingPaused READ recordingPaused NOTIFY recordingPausedChanged)
    Q_PROPERTY(QString recordingStatus READ recordingStatus NOTIFY recordingStatusChanged)
    Q_PROPERTY(QString lastRecordingPath READ lastRecordingPath NOTIFY lastRecordingPathChanged)
    Q_PROPERTY(QString lastProjectPath READ lastProjectPath NOTIFY lastRecordingPathChanged)
    Q_PROPERTY(QObject *frameStore READ frameStore CONSTANT)
    Q_PROPERTY(bool screenAuthorized READ screenAuthorized NOTIFY screenAuthorizedChanged)
    Q_PROPERTY(QString permissionIssue READ permissionIssue NOTIFY permissionIssueChanged)
    Q_PROPERTY(QString permissionIssueKind READ permissionIssueKind NOTIFY permissionIssueChanged)

public:
    explicit MacCapture(QObject *parent = nullptr);
    ~MacCapture() override;

    QStringList displayNames() const { return displayNames_; }
    QString status() const { return status_; }
    bool running() const { return running_; }
    bool busy() const { return busy_; }
    bool recording() const { return recording_; }
    bool recordingPaused() const { return recordingPaused_; }
    QString recordingStatus() const { return recordingStatus_; }
    QString lastRecordingPath() const { return lastRecordingPath_; }
    QString lastProjectPath() const { return lastProjectPath_; }
    QObject *frameStore() const;

    Q_INVOKABLE void refreshDisplays();
    Q_INVOKABLE void startDisplay(int index);
    Q_INVOKABLE void startRecordingDisplay(int index, const QVariantMap &settings);
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void pauseRecording();
    Q_INVOKABLE void resumeRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void openLastProject();
    Q_INVOKABLE void openRecordingDirectory();

    bool screenAuthorized() const;
    // Human-readable reason the last capture/event start failed, plus a stable
    // kind ("screen" / "input" / "microphone") the guide uses to pick a tab.
    QString permissionIssue() const { return permissionIssue_; }
    QString permissionIssueKind() const { return permissionIssueKind_; }
    Q_INVOKABLE void refreshScreenAuthorization();
    Q_INVOKABLE bool requestScreenAuthorization();
    Q_INVOKABLE void openScreenRecordingSettings();
    Q_INVOKABLE void openInputMonitoringSettings();
    Q_INVOKABLE void openMicrophoneSettings();
    Q_INVOKABLE bool requestInputMonitoringAccess();
    Q_INVOKABLE void revealAppInFinder();
    Q_INVOKABLE QString appBundlePath() const;
    Q_INVOKABLE QString appFileUrl() const;
    Q_INVOKABLE void beginAppDrag();
    Q_INVOKABLE void relaunch();
    Q_INVOKABLE void resetScreenPermission();

signals:
    void displayNamesChanged();
    void statusChanged();
    void runningChanged();
    void busyChanged();
    void recordingChanged();
    void recordingPausedChanged();
    void recordingStatusChanged();
    void lastRecordingPathChanged();
    void screenAuthorizedChanged();
    void permissionIssueChanged();
    void captureAccessDenied();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::shared_ptr<VideoFrameStore> frameStore_;
    QStringList displayNames_;
    QString status_ = QStringLiteral("正在准备屏幕来源…");
    bool running_ = false;
    bool busy_ = false;
    bool stopRequested_ = false;
    bool recordWhenReady_ = false;
    bool captureProbeOk_ = false;
    bool recording_ = false;
    bool recordingPaused_ = false;
    bool recordingFinalizing_ = false;
    int activeDisplayIndex_ = -1;
    QString recordingStatus_;
    QString lastRecordingPath_;
    QString lastProjectPath_;
    QVariantMap recordingSettings_;
    QString permissionIssue_;
    QString permissionIssueKind_;

    void setStatus(const QString &value);
    void setPermissionIssue(const QString &kind, const QString &message);
    void setRunning(bool value);
    void setBusy(bool value);
    void setRecording(bool value);
    void setRecordingPaused(bool value);
    void setRecordingStatus(const QString &value);
    void setLastRecordingPath(const QString &value);
    void reportError(const QString &value);
};
