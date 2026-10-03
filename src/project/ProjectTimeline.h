#pragma once
#include <QJsonObject>
#include <QString>

namespace ProjectTimeline {
// Does not modify raw assets or project.json. Derived files are atomic.
QJsonObject build(const QString &directory, const QJsonObject &video);
}
