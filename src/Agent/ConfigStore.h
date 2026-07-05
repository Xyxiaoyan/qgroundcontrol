#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QSettings>

class ConfigStore : public QObject
{
    Q_OBJECT
public:
    explicit ConfigStore(QObject *parent = nullptr);

    QString apiKey() const;
    void setApiKey(const QString &key);

    QString apiEndpoint() const;
    void setApiEndpoint(const QString &url);

    QString modelName() const;
    void setModelName(const QString &model);

    QString geocodeApiKey() const;
    void setGeocodeApiKey(const QString &key);

    // RAG configuration
    QString ragEndpoint() const;
    void setRagEndpoint(const QString &url);

    QString ragApiKey() const;
    void setRagApiKey(const QString &key);

    bool ragEnabled() const;
    void setRagEnabled(bool enabled);

private:
    QByteArray _deriveKey() const;
    QByteArray _encrypt(const QString &plain) const;
    QString _decrypt(const QByteArray &cipherBase64) const;

    QSettings _settings;
    static constexpr const char *kKeyApiKey = "agent/encryptedApiKey";
    static constexpr const char *kKeyEndpoint = "agent/apiEndpoint";
    static constexpr const char *kKeyModel = "agent/modelName";
    static constexpr const char *kKeyGeocodeApiKey = "agent/geocodeApiKey";
    static constexpr const char *kKeyRagEndpoint = "agent/ragEndpoint";
    static constexpr const char *kKeyRagApiKey = "agent/encryptedRagApiKey";
    static constexpr const char *kKeyRagEnabled = "agent/ragEnabled";
};
