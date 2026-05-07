#pragma once

#include <QtCore/QObject>

#include "MapProvider.h"

static constexpr const quint32 AVERAGE_GAODE_ROAD      = 13000;
static constexpr const quint32 AVERAGE_GAODE_SATELLITE = 28000;

class GaoDeProvider : public MapProvider
{
protected:
    GaoDeProvider(const QString &mapName, const QString &mapTypeCode, const QString &imageFormat,
                  quint32 averageSize, MapProvider::MapStyle mapType)
        : MapProvider(mapName, QStringLiteral("https://www.amap.com/"), imageFormat, averageSize, mapType)
        , _mapType(mapTypeCode) {}

private:
    QString _getURL(int x, int y, int zoom) const final;

    const QString _mapType;
};

class GaoDeRoadProvider : public GaoDeProvider
{
public:
    GaoDeRoadProvider()
        : GaoDeProvider(
            QObject::tr("GaoDe Road"),
            QStringLiteral("8"),
            QStringLiteral("png"),
            AVERAGE_GAODE_ROAD,
            MapProvider::StreetMap) {}
};

class GaoDeSatelliteProvider : public GaoDeProvider
{
public:
    GaoDeSatelliteProvider()
        : GaoDeProvider(
            QObject::tr("GaoDe Satellite"),
            QStringLiteral("6"),
            QStringLiteral("jpg"),
            AVERAGE_GAODE_SATELLITE,
            MapProvider::SatelliteMapDay) {}
};
