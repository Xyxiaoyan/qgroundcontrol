#include "LogAnalysisModel.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>

LogAnalysisModel::LogAnalysisModel(QObject *parent)
    : QObject(parent)
{
}

void LogAnalysisModel::setData(const QJsonObject &analysisResult,
                                const QJsonArray &flightModesArr,
                                const QJsonArray &accelTS,
                                const QJsonArray &gyroTS,
                                const QJsonArray &gpsTS,
                                const QJsonArray &batteryTS,
                                const QJsonArray &ekfTS,
                                const QJsonArray &pidTS,
                                const QJsonArray &opticalFlowTS)
{
    _hasData = true;
    _filePath = analysisResult.value(QStringLiteral("file")).toString();

    // Summary stats
    _vibration = analysisResult.value(QStringLiteral("accelerometer")).toObject();
    _gps = analysisResult.value(QStringLiteral("gps")).toObject();
    _battery = analysisResult.value(QStringLiteral("battery")).toObject();
    _ekf = analysisResult.value(QStringLiteral("ekf")).toObject();
    _opticalFlow = analysisResult.value(QStringLiteral("opticalFlow")).toObject();

    // Time-series
    _accelTimeSeries = accelTS;
    _gyroTimeSeries = gyroTS;
    _gpsTimeSeries = gpsTS;
    _batteryTimeSeries = batteryTS;
    _ekfTimeSeries = ekfTS;
    _pidTimeSeries = pidTS;
    _opticalFlowTimeSeries = opticalFlowTS;

    // Flight mode transitions
    _flightModes = flightModesArr;

    // Compute overall time range from the longest time-series
    _timeRangeStart = 0;
    _timeRangeEnd = 0;
    auto computeRange = [&](const QJsonArray &arr) {
        if (arr.size() < 2) return;
        double last = arr.at(arr.size() - 1).toObject().value(QStringLiteral("t")).toDouble(0);
        if (last > _timeRangeEnd) _timeRangeEnd = last;
    };
    computeRange(accelTS);
    computeRange(gyroTS);
    computeRange(gpsTS);
    computeRange(batteryTS);
    computeRange(ekfTS);
    computeRange(pidTS);
    computeRange(opticalFlowTS);

    emit dataChanged();
}

void LogAnalysisModel::clear()
{
    _hasData = false;
    _filePath.clear();
    _vibration = QJsonObject();
    _gps = QJsonObject();
    _battery = QJsonObject();
    _ekf = QJsonObject();
    _opticalFlow = QJsonObject();
    _accelTimeSeries = QJsonArray();
    _gyroTimeSeries = QJsonArray();
    _gpsTimeSeries = QJsonArray();
    _batteryTimeSeries = QJsonArray();
    _ekfTimeSeries = QJsonArray();
    _pidTimeSeries = QJsonArray();
    _opticalFlowTimeSeries = QJsonArray();
    _flightModes = QJsonArray();
    _timeRangeStart = 0;
    _timeRangeEnd = 0;
    emit dataChanged();
}

QJsonArray LogAnalysisModel::subsample(const QJsonArray &data, int maxPoints) const
{
    const int n = data.size();
    if (n <= maxPoints || maxPoints <= 0) return data;

    const int stride = n / maxPoints;
    QJsonArray result;

    // Always include first and last points
    result.append(data.at(0));
    for (int i = stride; i < n - 1; i += stride) {
        result.append(data.at(i));
    }
    result.append(data.at(n - 1));

    return result;
}
