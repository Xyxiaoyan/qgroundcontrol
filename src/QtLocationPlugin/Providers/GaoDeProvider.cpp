#include "GaoDeProvider.h"

QString GaoDeProvider::_getURL(int x, int y, int zoom) const
{
    // Gaode tile URL pattern:
    //   Street:  https://webrd0{1-4}.is.autonavi.com/appmaptile?lang=zh_cn&size=1&scale=1&style=8&x=&y=&z=
    //   Satellite: https://webst0{1-4}.is.autonavi.com/appmaptile?style=6&x=&y=&z=
    const QString prefix = (_mapType == QStringLiteral("8")) ? QStringLiteral("webrd") : QStringLiteral("webst");
    const int server = _getServerNum(x, y, 4) + 1; // server 1-4

    QString url = QStringLiteral("https://%1%2.is.autonavi.com/appmaptile?style=%3&x=%4&y=%5&z=%6")
        .arg(prefix)
        .arg(server, 2, 10, QChar('0'))
        .arg(_mapType);

    if (_mapType == QStringLiteral("8")) {
        url += QStringLiteral("&lang=zh_cn&size=1&scale=1");
    }

    return url.arg(x).arg(y).arg(zoom);
}
