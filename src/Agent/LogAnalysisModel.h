#pragma once

#include <QtCore/QObject>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonArray>
#include <QtCore/QString>

class LogAnalysisModel : public QObject
{
    Q_OBJECT
public:
    explicit LogAnalysisModel(QObject *parent = nullptr);

    Q_PROPERTY(bool hasData READ hasData NOTIFY dataChanged)
    Q_PROPERTY(QString filePath READ filePath NOTIFY dataChanged)

    // Summary stats (from LogAnalyzer::analyze())
    Q_PROPERTY(QJsonObject vibration READ vibration NOTIFY dataChanged)
    Q_PROPERTY(QJsonObject gps READ gps NOTIFY dataChanged)
    Q_PROPERTY(QJsonObject battery READ battery NOTIFY dataChanged)
    Q_PROPERTY(QJsonObject ekf READ ekf NOTIFY dataChanged)
    Q_PROPERTY(QJsonObject opticalFlow READ opticalFlow NOTIFY dataChanged)

    // Time-series data (from LogAnalyzer::timeSeriesJson())
    Q_PROPERTY(QJsonArray accelTimeSeries READ accelTimeSeries NOTIFY dataChanged)
    Q_PROPERTY(QJsonArray gyroTimeSeries READ gyroTimeSeries NOTIFY dataChanged)
    Q_PROPERTY(QJsonArray gpsTimeSeries READ gpsTimeSeries NOTIFY dataChanged)
    Q_PROPERTY(QJsonArray batteryTimeSeries READ batteryTimeSeries NOTIFY dataChanged)
    Q_PROPERTY(QJsonArray ekfTimeSeries READ ekfTimeSeries NOTIFY dataChanged)
    Q_PROPERTY(QJsonArray pidTimeSeries READ pidTimeSeries NOTIFY dataChanged)
    Q_PROPERTY(QJsonArray opticalFlowTimeSeries READ opticalFlowTimeSeries NOTIFY dataChanged)

    // Flight mode transitions
    Q_PROPERTY(QJsonArray flightModes READ flightModes NOTIFY dataChanged)

    // Time range
    Q_PROPERTY(double timeRangeStart READ timeRangeStart NOTIFY dataChanged)
    Q_PROPERTY(double timeRangeEnd READ timeRangeEnd NOTIFY dataChanged)

    bool hasData() const { return _hasData; }
    QString filePath() const { return _filePath; }
    QJsonObject vibration() const { return _vibration; }
    QJsonObject gps() const { return _gps; }
    QJsonObject battery() const { return _battery; }
    QJsonObject ekf() const { return _ekf; }
    QJsonObject opticalFlow() const { return _opticalFlow; }
    QJsonArray accelTimeSeries() const { return _accelTimeSeries; }
    QJsonArray gyroTimeSeries() const { return _gyroTimeSeries; }
    QJsonArray gpsTimeSeries() const { return _gpsTimeSeries; }
    QJsonArray batteryTimeSeries() const { return _batteryTimeSeries; }
    QJsonArray ekfTimeSeries() const { return _ekfTimeSeries; }
    QJsonArray pidTimeSeries() const { return _pidTimeSeries; }
    QJsonArray opticalFlowTimeSeries() const { return _opticalFlowTimeSeries; }
    QJsonArray flightModes() const { return _flightModes; }
    double timeRangeStart() const { return _timeRangeStart; }
    double timeRangeEnd() const { return _timeRangeEnd; }

    /// Called by AgentManager after analysis completes
    void setData(const QJsonObject &analysisResult,
                 const QJsonArray &flightModesArr,
                 const QJsonArray &accelTS,
                 const QJsonArray &gyroTS,
                 const QJsonArray &gpsTS,
                 const QJsonArray &batteryTS,
                 const QJsonArray &ekfTS,
                 const QJsonArray &pidTS,
                 const QJsonArray &opticalFlowTS);

    Q_INVOKABLE void clear();

    /// Stride-based downsampling for chart performance
    Q_INVOKABLE QJsonArray subsample(const QJsonArray &data, int maxPoints) const;

signals:
    void dataChanged();

private:
    bool _hasData = false;
    QString _filePath;
    QJsonObject _vibration, _gps, _battery, _ekf, _opticalFlow;
    QJsonArray _accelTimeSeries, _gyroTimeSeries, _gpsTimeSeries;
    QJsonArray _batteryTimeSeries, _ekfTimeSeries, _pidTimeSeries, _opticalFlowTimeSeries;
    QJsonArray _flightModes;
    double _timeRangeStart = 0;
    double _timeRangeEnd = 0;
};
