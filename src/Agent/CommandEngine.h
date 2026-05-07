#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantMap>

class Vehicle;

struct AgentCommand {
    QString action;
    QList<double> params;
    QString raw;
    bool isReadOnly = false;    // paramq, paramlist — no confirmation needed
    bool isValid = false;
};

class CommandEngine : public QObject
{
    Q_OBJECT
public:
    static CommandEngine *instance();
    explicit CommandEngine(QObject *parent = nullptr);

    AgentCommand parseCommand(const QString &text) const;
    QString describeCommand(const AgentCommand &cmd) const;
    Q_INVOKABLE bool executeCommand(const AgentCommand &cmd);

signals:
    void commandPending(const QString &description, const QVariantMap &cmdMap);
    void commandExecuted(const QString &result);
    void commandFailed(const QString &error);

private:
    double _safeParam(const QList<double> &params, int index, double def) const;
};
