#include "ConfigStore.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QSysInfo>
#include <QtCore/QRandomGenerator>
#include <QtCore/QByteArray>

ConfigStore::ConfigStore(QObject *parent)
    : QObject(parent)
    , _settings(QStringLiteral("AgentQGC"), QStringLiteral("AgentQGC"))
{
}

QString ConfigStore::apiKey() const
{
    const QByteArray encrypted = _settings.value(kKeyApiKey).toByteArray();
    if (encrypted.isEmpty()) return {};
    return _decrypt(encrypted);
}

void ConfigStore::setApiKey(const QString &key)
{
    if (key.isEmpty()) {
        _settings.remove(kKeyApiKey);
    } else {
        _settings.setValue(kKeyApiKey, _encrypt(key));
    }
}

QString ConfigStore::apiEndpoint() const
{
    return _settings.value(kKeyEndpoint).toString();
}

void ConfigStore::setApiEndpoint(const QString &url)
{
    _settings.setValue(kKeyEndpoint, url);
}

QString ConfigStore::modelName() const
{
    return _settings.value(kKeyModel).toString();
}

void ConfigStore::setModelName(const QString &model)
{
    _settings.setValue(kKeyModel, model);
}

QString ConfigStore::geocodeApiKey() const
{
    return _settings.value(kKeyGeocodeApiKey).toString();
}

void ConfigStore::setGeocodeApiKey(const QString &key)
{
    if (key.isEmpty()) {
        _settings.remove(kKeyGeocodeApiKey);
    } else {
        _settings.setValue(kKeyGeocodeApiKey, key);
    }
}

QByteArray ConfigStore::_deriveKey() const
{
    static const QByteArray salt("AgentQGC.Agent.2026");
    QByteArray machineId = QSysInfo::machineUniqueId();
    if (machineId.isEmpty()) {
        machineId = QSysInfo::bootUniqueId();
    }

    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(machineId);
    hash.addData(salt);
    return hash.result();
}

QByteArray ConfigStore::_encrypt(const QString &plain) const
{
    const QByteArray key = _deriveKey();
    QByteArray plainBytes = plain.toUtf8();

    // Generate random prefix to make identical plaintexts produce different ciphertexts
    const int prefixLen = 16;
    QByteArray randomPrefix(prefixLen, '\0');
    for (int i = 0; i < prefixLen; ++i) {
        randomPrefix[i] = static_cast<char>(QRandomGenerator::global()->bounded(256));
    }

    QByteArray data = randomPrefix + plainBytes;
    QByteArray result(data.size(), '\0');

    for (int i = 0; i < data.size(); ++i) {
        result[i] = data[i] ^ key[i % key.size()];
    }

    return result.toBase64();
}

QString ConfigStore::_decrypt(const QByteArray &cipherBase64) const
{
    const QByteArray key = _deriveKey();
    QByteArray data = QByteArray::fromBase64(cipherBase64);

    if (data.size() <= 16) return {}; // Invalid: shorter than prefix

    QByteArray decrypted(data.size(), '\0');
    for (int i = 0; i < data.size(); ++i) {
        decrypted[i] = data[i] ^ key[i % key.size()];
    }

    // Strip the 16-byte random prefix
    return QString::fromUtf8(decrypted.mid(16));
}
