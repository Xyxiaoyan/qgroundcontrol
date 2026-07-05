#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QJsonArray>
#include <QtCore/QVariantList>

class ArchiveManager : public QObject
{
    Q_OBJECT
public:
    explicit ArchiveManager(QObject *parent = nullptr);

    // Save current conversation to ~/.agentqgc/archives/<timestamp>.json
    // Returns the archive ID (timestamp string), or empty on failure.
    Q_INVOKABLE QString archiveCurrent(const QJsonArray &messages);

    // List all archives (metadata only: id, timestamp, messageCount, preview)
    Q_INVOKABLE QVariantList listArchives() const;

    // Load full message array from an archive by ID
    Q_INVOKABLE QJsonArray loadArchive(const QString &id) const;

    // Delete an archive by ID
    Q_INVOKABLE bool deleteArchive(const QString &id);

    // Number of archives
    Q_INVOKABLE int archiveCount() const;

signals:
    void archiveSaved(const QString &id);
    void archiveDeleted(const QString &id);

private:
    QString _archiveDir() const;
    QString _archivePath(const QString &id) const;
    QString _generateId() const;
    QString _formatTimestamp(const QString &id) const;
};
