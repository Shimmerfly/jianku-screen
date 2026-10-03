#include "SettingsStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <cmath>

namespace {
QVariantMap referenceFactory() {
    // Values are the reference software's verified base object where known
    // (research/parameters/桌面参数-官方3.7.5静态.json). Remaining editor fields use the
    // effective values observed in a real reference project. Not yet a measured
    // fresh-project preset; keep every uncertain value labelled in the UI.
    return {
        {"backgroundType", "gradient"},
        {"backgroundSystemName", "macOS/tahoe-light.jpg"},
        {"backgroundImagePath", ""},
        {"backgroundColor", "#1b2230"},
        {"gradientStartColor", "#3F37C9"},
        {"gradientEndColor", "#8C87DF"},
        {"gradientAngle", 135.0},
        {"backgroundBlur", 0.0},
        {"backgroundPaddingRatio", 10.0},
        {"windowBorderRadius", 12.0},
        {"cornerSmoothing", 0.0},
        {"insetSize", 0.0},
        {"insetColor", "#000000"},
        {"insetAlpha", 0.5},
        {"shadowIntensity", 0.75},
        {"shadowAngle", 90.0},
        {"shadowDistance", 25.0},
        {"shadowBlur", 20.0},
        {"shadowIsDirectional", false},
        {"outputAspectRatio", "auto"},
        {"cursorSize", 1.5},
        {"cursorSmoothing", "Smooth"},
        {"cursorRotateOnXMovementRatio", 0.5},
        {"cursorBaseRotation", 0.0},
        {"hideCursor", false},
        {"hideNotMovingCursorAfterMs", 0.0},
        {"removeCurshorShakeTreshold", 500.0},
        {"clickEffect", "none"},
        {"alwaysUseDefaultCursor", false},
        {"disableMouseMovementSpring", false},
        {"mouseMovementSpring", QVariantMap{{"stiffness", 470.0}, {"damping", 70.0}, {"mass", 3.0}}},
        {"screenMovementSpring", QVariantMap{{"stiffness", 200.0}, {"damping", 40.0}, {"mass", 2.25}}},
        {"mouseClickSpring", QVariantMap{{"stiffness", 700.0}, {"damping", 30.0}, {"mass", 1.0}}},
        {"autoZoom", true},
        {"defaultZoomLevel", 2.0},
        {"alwaysKeepZoomedIn", false},
        {"snapToEdgesRatio", 0.25},
        {"glideSpeed", 0.5},
        {"hideCamera", true},
        {"cameraSize", 0.35},
        {"cameraRoundness", 0.25},
        {"cameraScaleDuringZoom", 0.7},
        {"mirrorCamera", false},
        {"cameraAspectRatio", "original"},
        {"audioVolume", 1.0},
        {"systemAudioVolume", 1.0},
        {"muteMicrophone", false},
        {"muteSystemAudio", false},
        {"muteExternalDeviceAudio", false},
        {"backgroundAudioVolume", 0.05},
        {"muteBackgroundAudio", false},
        {"clickSoundEffectVolume", 0.25},
        {"improveMicrophoneAudio", false},
        {"showTranscript", false},
        {"transcriptSizeRatio", 1.0},
        {"showShortcuts", false},
        {"shortcutsSizeRatio", 1.0},
        {"showShortcutsWithSingleLetters", false},
        {"playbackSpeed", 1.0},
        {"recordingDirectory", QStandardPaths::writableLocation(QStandardPaths::MoviesLocation)
                                  + "/Jianku Screen"},
        {"screenshotHotkey", "Meta+Shift+7"},
        {"screenshotDirectory", QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
                                + "/Jianku Screen"}
    };
}
} // namespace

SettingsStore::SettingsStore(QObject *parent) : QObject(parent), factory_(referenceFactory()) {
    storagePath_ = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
                   + "/settings.json";
    defaults_ = factory_;
    current_ = defaults_;
    load();
}

QVariant SettingsStore::normalized(const QString &key, const QVariant &value) const {
    if (!factory_.contains(key))
        return {};
    const QVariant exemplar = factory_.value(key);
    if (exemplar.metaType().id() == QMetaType::Double) {
        bool ok = false;
        const double number = value.toDouble(&ok);
        return ok && std::isfinite(number) && number >= 0.0 && number <= 10000.0
            ? QVariant(number) : QVariant();
    }
    if (exemplar.metaType().id() == QMetaType::Bool) {
        if (value.metaType().id() == QMetaType::Bool)
            return value;
        return {};
    }
    if (exemplar.metaType().id() == QMetaType::QString)
        return value.toString();
    if (exemplar.metaType().id() == QMetaType::QVariantMap) {
        const QVariantMap parts = value.toMap();
        if (parts.keys() != QStringList({"damping", "mass", "stiffness"}))
            return {};
        QVariantMap cleaned;
        for (const auto &part : parts.keys()) {
            bool ok = false;
            const double number = parts.value(part).toDouble(&ok);
            if (!ok || !std::isfinite(number) || number < 0.0 || number > 10000.0)
                return {};
            cleaned.insert(part, number);
        }
        return cleaned;
    }
    return {};
}

bool SettingsStore::setCurrent(const QString &key, const QVariant &value) {
    if (!factory_.contains(key) && key.contains('.')) {
        const QString group = key.section('.', 0, 0);
        const QString component = key.section('.', 1, 1);
        QVariantMap spring = current_.value(group).toMap();
        if (!factory_.value(group).toMap().contains(component)) return false;
        spring[component] = value;
        return setCurrent(group, spring);
    }
    const QVariant cleaned = normalized(key, value);
    if (!cleaned.isValid()) {
        emit errorOccurred(QStringLiteral("设置值无效：") + key);
        return false;
    }
    if (current_.value(key) == cleaned)
        return true;
    current_.insert(key, cleaned);
    emit currentChanged();
    return save();
}

bool SettingsStore::setAsDefault(const QString &key) {
    if (!factory_.contains(key) && key.contains('.')) {
        const QString group = key.section('.', 0, 0);
        const QString component = key.section('.', 1, 1);
        if (!factory_.value(group).toMap().contains(component)) return false;
        QVariantMap spring = defaults_.value(group).toMap();
        spring[component] = current_.value(group).toMap().value(component);
        defaults_[group] = spring;
        emit defaultsChanged();
        return save();
    }
    if (!factory_.contains(key))
        return false;
    defaults_.insert(key, current_.value(key));
    emit defaultsChanged();
    return save();
}

bool SettingsStore::restoreFactory(const QString &key) {
    if (!factory_.contains(key) && key.contains('.')) {
        const QString group = key.section('.', 0, 0);
        const QString component = key.section('.', 1, 1);
        if (!factory_.value(group).toMap().contains(component)) return false;
        QVariantMap currentSpring = current_.value(group).toMap();
        QVariantMap defaultSpring = defaults_.value(group).toMap();
        currentSpring[component] = factory_.value(group).toMap().value(component);
        defaultSpring[component] = currentSpring.value(component);
        current_[group] = currentSpring;
        defaults_[group] = defaultSpring;
        emit currentChanged();
        emit defaultsChanged();
        return save();
    }
    if (!factory_.contains(key))
        return false;
    current_.insert(key, factory_.value(key));
    defaults_.insert(key, factory_.value(key));
    emit currentChanged();
    emit defaultsChanged();
    return save();
}

void SettingsStore::restoreAllFactory() {
    current_ = factory_;
    defaults_ = factory_;
    emit currentChanged();
    emit defaultsChanged();
    save();
}

void SettingsStore::useDefaults() {
    current_ = defaults_;
    emit currentChanged();
    save();
}

void SettingsStore::load() {
    QFile file(storagePath_);
    if (!file.open(QIODevice::ReadOnly))
        return;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        emit errorOccurred(QStringLiteral("无法读取已保存的设置。"));
        return;
    }
    const QJsonObject root = document.object();
    if (root.value("schemaVersion").toInt() != 1)
        return;
    const QVariantMap storedDefaults = root.value("defaults").toObject().toVariantMap();
    const QVariantMap storedCurrent = root.value("current").toObject().toVariantMap();
    for (const auto &key : factory_.keys()) {
        if (storedDefaults.contains(key)) {
            if (const QVariant value = normalized(key, storedDefaults.value(key)); value.isValid())
                defaults_[key] = value;
        }
        if (storedCurrent.contains(key)) {
            if (const QVariant value = normalized(key, storedCurrent.value(key)); value.isValid())
                current_[key] = value;
        }
    }
}

bool SettingsStore::save() {
    QDir().mkpath(QFileInfo(storagePath_).absolutePath());
    QSaveFile file(storagePath_);
    if (!file.open(QIODevice::WriteOnly)) {
        emit errorOccurred(QStringLiteral("无法写入设置文件：") + storagePath_);
        return false;
    }
    const QJsonObject root{{"schemaVersion", 1},
                           {"defaults", QJsonObject::fromVariantMap(defaults_)},
                           {"current", QJsonObject::fromVariantMap(current_)}};
    if (file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !file.commit()) {
        emit errorOccurred(QStringLiteral("保存设置失败：") + storagePath_);
        return false;
    }
    return true;
}
