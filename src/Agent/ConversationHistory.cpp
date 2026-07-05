#include "ConversationHistory.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>

ConversationHistory::ConversationHistory(QObject *parent)
    : QObject(parent)
    , _settings(QStringLiteral("AgentQGC"), QStringLiteral("AgentQGC"))
{
}

QJsonArray ConversationHistory::load()
{
    const QByteArray raw = _settings.value(kSettingsKey).toByteArray();
    if (raw.isEmpty()) {
        return {};
    }

    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(raw, &error);
    if (error.error != QJsonParseError::NoError) {
        return {};
    }

    _cachedMessages = doc.array();
    return _cachedMessages;
}

void ConversationHistory::save(const QJsonArray &messages)
{
    QJsonArray toStore = stripSystemMessage(messages);

    // Cap to max persisted messages
    if (toStore.size() > kMaxPersistedMessages) {
        QJsonArray trimmed;
        int start = toStore.size() - kMaxPersistedMessages;
        for (int i = start; i < toStore.size(); i++) {
            trimmed.append(toStore[i]);
        }
        toStore = trimmed;
    }

    _cachedMessages = toStore;
    QJsonDocument doc(toStore);
    _settings.setValue(kSettingsKey, doc.toJson(QJsonDocument::Compact));
}

void ConversationHistory::clear()
{
    _cachedMessages = QJsonArray{};
    _settings.remove(kSettingsKey);
    emit historyCleared();
}

QJsonArray ConversationHistory::stripSystemMessage(const QJsonArray &messages)
{
    QJsonArray result;
    for (int i = 0; i < messages.size(); i++) {
        QJsonObject msg = messages[i].toObject();
        if (msg[QStringLiteral("role")].toString() != QStringLiteral("system")) {
            result.append(msg);
        }
    }
    return result;
}
