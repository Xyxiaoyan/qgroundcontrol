#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QJsonArray>
#include <QtCore/QSettings>

class ConversationHistory : public QObject
{
    Q_OBJECT
public:
    explicit ConversationHistory(QObject *parent = nullptr);

    // Load persisted messages from QSettings. Returns the stored array
    // (without system prompt — system messages are stripped on save).
    QJsonArray load();

    // Save messages to QSettings. Strips system-role messages before
    // persisting (system prompt is regenerated each session). Caps at
    // kMaxPersistedMessages.
    void save(const QJsonArray &messages);

    // Clear both in-memory cache and QSettings key.
    Q_INVOKABLE void clear();

    bool isEmpty() const { return _cachedMessages.isEmpty(); }

    // Return a copy of messages with all system-role entries removed.
    static QJsonArray stripSystemMessage(const QJsonArray &messages);

signals:
    void historyCleared();

private:
    QJsonArray _cachedMessages;
    QSettings _settings;

    static constexpr const char *kSettingsKey = "agent/conversationHistory";
    static constexpr int kMaxPersistedMessages = 30;
};
