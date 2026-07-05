#include "ArchiveManager.h"
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QStandardPaths>
#include <QtCore/QDateTime>

ArchiveManager::ArchiveManager(QObject *parent)
    : QObject(parent)
{
}

QString ArchiveManager::_archiveDir() const
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    return base + QStringLiteral("/.agentqgc/archives");
}

QString ArchiveManager::_archivePath(const QString &id) const
{
    return _archiveDir() + QStringLiteral("/") + id + QStringLiteral(".json");
}

QString ArchiveManager::_generateId() const
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
}

QString ArchiveManager::_formatTimestamp(const QString &id) const
{
    // Parse "yyyyMMdd_HHmmss" → human-readable
    QDateTime dt = QDateTime::fromString(id, QStringLiteral("yyyyMMdd_HHmmss"));
    if (dt.isValid()) {
        return dt.toString(QStringLiteral("yyyy-MM-dd hh:mm:ss"));
    }
    return id;
}

QString ArchiveManager::archiveCurrent(const QJsonArray &messages)
{
    // Filter out empty messages and keep only user/assistant/system/tool
    QJsonArray filtered;
    QString preview;
    for (const auto &msgVal : messages) {
        const QJsonObject msg = msgVal.toObject();
        const QString role = msg.value(QStringLiteral("role")).toString();
        const QString content = msg.value(QStringLiteral("content")).toString();
        if (content.isEmpty()) continue;
        if (role == QStringLiteral("tool")) continue; // Skip raw tool results
        filtered.append(msg);

        // Use first user message as preview
        if (preview.isEmpty() && role == QStringLiteral("user")) {
            preview = content.left(80);
            if (content.length() > 80) preview += QStringLiteral("...");
        }
    }

    if (filtered.isEmpty()) {
        return {};
    }

    // Ensure directory exists
    QDir().mkpath(_archiveDir());

    const QString id = _generateId();
    const QString path = _archivePath(id);

    QJsonObject archive;
    archive[QStringLiteral("id")] = id;
    archive[QStringLiteral("timestamp")] = _formatTimestamp(id);
    archive[QStringLiteral("messageCount")] = filtered.size();
    archive[QStringLiteral("preview")] = preview;
    archive[QStringLiteral("messages")] = filtered;

    QJsonDocument doc(archive);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();

    emit archiveSaved(id);
    return id;
}

QVariantList ArchiveManager::listArchives() const
{
    QVariantList result;
    QDir dir(_archiveDir());
    if (!dir.exists()) return result;

    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files, QDir::Name | QDir::Reversed);
    for (const auto &fileName : files) {
        QFile file(dir.absoluteFilePath(fileName));
        if (!file.open(QIODevice::ReadOnly)) continue;

        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();
        if (!doc.isObject()) continue;

        const QJsonObject obj = doc.object();
        QVariantMap entry;
        entry[QStringLiteral("id")] = obj.value(QStringLiteral("id")).toString();
        entry[QStringLiteral("timestamp")] = obj.value(QStringLiteral("timestamp")).toString();
        entry[QStringLiteral("messageCount")] = obj.value(QStringLiteral("messageCount")).toInt();
        entry[QStringLiteral("preview")] = obj.value(QStringLiteral("preview")).toString();
        result.append(entry);
    }

    return result;
}

QJsonArray ArchiveManager::loadArchive(const QString &id) const
{
    if (id.isEmpty()) return {};

    QFile file(_archivePath(id));
    if (!file.open(QIODevice::ReadOnly)) return {};

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    if (!doc.isObject()) return {};

    return doc.object().value(QStringLiteral("messages")).toArray();
}

bool ArchiveManager::deleteArchive(const QString &id)
{
    if (id.isEmpty()) return false;

    const QString path = _archivePath(id);
    if (QFile::remove(path)) {
        emit archiveDeleted(id);
        return true;
    }
    return false;
}

int ArchiveManager::archiveCount() const
{
    QDir dir(_archiveDir());
    if (!dir.exists()) return 0;
    return dir.entryList({QStringLiteral("*.json")}, QDir::Files).size();
}
