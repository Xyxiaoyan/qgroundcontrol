#include "CameraProvider.h"
#include "VideoManager.h"
#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(CameraLog, "qgc.agent.cam")

CameraProvider::CameraProvider(QObject *parent) : QObject(parent) {}

QString CameraProvider::buildCameraContext() const
{
    auto *vm = VideoManager::instance();
    if (!vm) return {};

    QString ctx;
    ctx += QStringLiteral("\n## 摄像头与视频\n");
    ctx += _collectVideoStatus();
    return ctx;
}

QString CameraProvider::_collectVideoStatus() const
{
    auto *vm = VideoManager::instance();
    if (!vm) return QStringLiteral("- 视频系统未就绪\n");

    QString s;
    s += QStringLiteral("- 视频连接: %1\n").arg(vm->hasVideo() ? QStringLiteral("已连接") : QStringLiteral("未连接"));
    if (vm->hasVideo()) {
        s += QStringLiteral("- 推流状态: %1\n").arg(vm->streaming() ? QStringLiteral("直播中") : QStringLiteral("已暂停"));
        s += QStringLiteral("- 录制状态: %1\n").arg(vm->recording() ? QStringLiteral("录制中") : QStringLiteral("未录制"));
        s += QStringLiteral("- 分辨率: %1x%2\n").arg(vm->videoSize().width()).arg(vm->videoSize().height());
        s += QStringLiteral("- 视场角: %1°\n").arg(vm->hfov(), 0, 'f', 1);
        s += QStringLiteral("- 热成像: %1\n").arg(vm->hasThermal() ? QStringLiteral("有") : QStringLiteral("无"));
    }
    return s;
}
