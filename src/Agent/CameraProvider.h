#pragma once

#include <QtCore/QObject>
#include <QtCore/QString>

class Vehicle;
class VideoManager;
class GimbalController;

class CameraProvider : public QObject
{
    Q_OBJECT
public:
    explicit CameraProvider(QObject *parent = nullptr);

    QString buildCameraContext() const;

private:
    QString _collectVideoStatus() const;
    QString _collectGimbalStatus() const;
    QString _collectCameraSettings() const;
};
