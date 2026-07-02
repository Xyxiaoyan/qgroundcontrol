#include "LogAnalysisTool.h"
#include "../LogAnalyzer.h"

LogAnalysisTool::LogAnalysisTool(LogAnalyzer *analyzer, QObject *parent)
    : AgentTool(parent)
    , _analyzer(analyzer)
{
}

QString LogAnalysisTool::description() const
{
    return QStringLiteral(
        "Analyze a previously loaded ULog flight log file. "
        "Analysis types: 'vibration' (IMU FFT analysis), 'gps' (GPS health), "
        "'battery' (battery performance), 'ekf' (EKF innovation health), 'all' (everything). "
        "The file must already be loaded via the load_log_file function. "
        "Use this to diagnose flight issues from log data."
    );
}

QJsonObject LogAnalysisTool::parametersSchema() const
{
    QJsonObject props;

    QJsonObject analysisType;
    analysisType[QStringLiteral("type")] = QStringLiteral("string");
    analysisType[QStringLiteral("description")] = QStringLiteral("Analysis type: vibration, gps, battery, ekf, or all");
    QJsonArray typeEnum = {QStringLiteral("vibration"), QStringLiteral("gps"), QStringLiteral("battery"),
                           QStringLiteral("ekf"), QStringLiteral("all")};
    analysisType[QStringLiteral("enum")] = typeEnum;
    props[QStringLiteral("analysis_type")] = analysisType;

    QJsonArray required;
    required.append(QStringLiteral("analysis_type"));

    QJsonObject schema;
    schema[QStringLiteral("type")] = QStringLiteral("object");
    schema[QStringLiteral("properties")] = props;
    schema[QStringLiteral("required")] = required;
    return schema;
}

QJsonObject LogAnalysisTool::execute(const QJsonObject &parameters)
{
    QJsonObject result;
    if (!_analyzer) {
        result[QStringLiteral("error")] = QStringLiteral("Log analyzer unavailable");
        return result;
    }
    if (!_analyzer->isLoaded()) {
        result[QStringLiteral("error")] = QStringLiteral("No log file loaded. Load a .ulg file first.");
        return result;
    }

    QString analysisType = parameters[QStringLiteral("analysis_type")].toString(QStringLiteral("all"));
    QJsonObject analysisResult = _analyzer->analyze(analysisType);

    result[QStringLiteral("status")] = QStringLiteral("success");
    result[QStringLiteral("report")] = _analyzer->generateReport(analysisResult);
    result[QStringLiteral("result")] = QStringLiteral("日志分析完成 (%1)").arg(analysisType);

    // Include key stats
    if (analysisType == QStringLiteral("vibration") || analysisType == QStringLiteral("all")) {
        QJsonObject vib = analysisResult.value(QStringLiteral("vibration")).toObject();
        if (!vib.isEmpty()) {
            result[QStringLiteral("vibration_rms_x")] = vib[QStringLiteral("rmsX")];
            result[QStringLiteral("vibration_rms_y")] = vib[QStringLiteral("rmsY")];
            result[QStringLiteral("vibration_rms_z")] = vib[QStringLiteral("rmsZ")];
            result[QStringLiteral("vibration_clip_count")] = vib[QStringLiteral("clipCount")];
        }
    }
    if (analysisType == QStringLiteral("gps") || analysisType == QStringLiteral("all")) {
        QJsonObject gps = analysisResult.value(QStringLiteral("gps")).toObject();
        if (!gps.isEmpty()) {
            result[QStringLiteral("gps_hdop_mean")] = gps[QStringLiteral("hdopMean")];
            result[QStringLiteral("gps_3d_fix_pct")] = gps[QStringLiteral("fix3dPct")];
            result[QStringLiteral("gps_position_jumps")] = gps[QStringLiteral("positionJumps")];
        }
    }
    if (analysisType == QStringLiteral("battery") || analysisType == QStringLiteral("all")) {
        QJsonObject bat = analysisResult.value(QStringLiteral("battery")).toObject();
        if (!bat.isEmpty()) {
            result[QStringLiteral("battery_voltage_sag")] = bat[QStringLiteral("voltageSag")];
            result[QStringLiteral("battery_capacity_mah")] = bat[QStringLiteral("capacityConsumedMah")];
        }
    }
    if (analysisType == QStringLiteral("ekf") || analysisType == QStringLiteral("all")) {
        QJsonObject ekf = analysisResult.value(QStringLiteral("ekf")).toObject();
        if (!ekf.isEmpty()) {
            result[QStringLiteral("ekf_health_score")] = ekf[QStringLiteral("healthScore")];
            result[QStringLiteral("ekf_vel_faults")] = ekf[QStringLiteral("velFaults")];
            result[QStringLiteral("ekf_pos_faults")] = ekf[QStringLiteral("posFaults")];
        }
    }

    return result;
}
