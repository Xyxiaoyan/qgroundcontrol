#include "VehicleContextTool.h"
#include "../ContextProvider.h"

VehicleContextTool::VehicleContextTool(ContextProvider *ctx, QObject *parent)
    : AgentTool(parent)
    , _ctx(ctx)
{
}

QString VehicleContextTool::description() const
{
    return QStringLiteral(
        "Get the current vehicle state snapshot including: attitude, battery level, GPS status, "
        "flight mode, health, and trend data. Use this to understand the vehicle's current condition "
        "before making decisions or executing actions."
    );
}

QJsonObject VehicleContextTool::parametersSchema() const
{
    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");
    schema[QStringLiteral("properties")] = QJsonObject{};
    schema[QStringLiteral("required")] = QJsonArray{};
    return schema;
}

QJsonObject VehicleContextTool::execute(const QJsonObject &parameters)
{
    Q_UNUSED(parameters)
    QJsonObject result;
    if (!_ctx) {
        result[QStringLiteral("error")] = QStringLiteral("Context provider unavailable");
        return result;
    }
    result[QStringLiteral("status")] = QStringLiteral("success");
    result[QStringLiteral("context")] = _ctx->buildVehicleContext();
    result[QStringLiteral("result")] = QStringLiteral("已获取飞行器状态");
    return result;
}
