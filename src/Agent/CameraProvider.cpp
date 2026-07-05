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
    ctx += QStringLiteral("\n## Camera & Video\n");
    ctx += _collectVideoStatus();
    return ctx;
}

QString CameraProvider::_collectVideoStatus() const
{
    auto *vm = VideoManager::instance();
    if (!vm) return QStringLiteral("- Video system not ready\n");

    QString s;
    s += QStringLiteral("- Video: %1\n").arg(vm->hasVideo() ? QStringLiteral("Connected") : QStringLiteral("Not connected"));
    if (vm->hasVideo()) {
        s += QStringLiteral("- Streaming: %1\n").arg(vm->streaming() ? QStringLiteral("Live") : QStringLiteral("Paused"));
        s += QStringLiteral("- Recording: %1\n").arg(vm->recording() ? QStringLiteral("Recording") : QStringLiteral("Not recording"));
        s += QStringLiteral("- Resolution: %1x%2\n").arg(vm->videoSize().width()).arg(vm->videoSize().height());
        s += QStringLiteral("- FOV: %1deg\n").arg(vm->hfov(), 0, 'f', 1);
        s += QStringLiteral("- Thermal: %1\n").arg(vm->hasThermal() ? QStringLiteral("Yes") : QStringLiteral("No"));
    }
    return s;
}
