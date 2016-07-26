#include "auxiliary/kspaths.h"
#include <QFileInfo>
#include <QDebug>

QString KSPaths::locate(QStandardPaths::StandardLocation location, const QString &fileName,
                        QStandardPaths::LocateOptions options) {
#ifdef ANDROID
    QString file = QStandardPaths::locate(location,fileName,options);
    if(file.isEmpty()) {
        file = "/data/data/org.kde.kstars/qt-reserved-files/share/kstars/" + fileName;
        if (!QFileInfo(file).exists()) {
            return QString();
        }
    }
    return file;
#else
    QString dir;
    if(location == QStandardPaths::GenericDataLocation) dir = "kstars";
    return QStandardPaths::locate(location, dir + QDir::separator() +
                                  fileName,options);
#endif
}

QStringList KSPaths::locateAll(QStandardPaths::StandardLocation location, const QString &fileName,
                        QStandardPaths::LocateOptions options) {
#ifdef ANDROID
    QStringList file = QStandardPaths::locateAll(location,fileName,options);
    if(file.isEmpty()) {
        QString f = "/data/data/org.kde.kstars/qt-reserved-files/share/kstars/" + fileName;
        if (QFileInfo(f).exists()) {
            file[0] = f;
        }
    }
    return file;
#else
    QString dir;
    if(location == QStandardPaths::GenericDataLocation) dir = "kstars";
    return QStandardPaths::locateAll(location, dir + QDir::separator() +
                                     fileName,options);
#endif
}

QString KSPaths::writableLocation(QStandardPaths::StandardLocation type) {
    QString dir;
    if(type == QStandardPaths::GenericDataLocation) dir = "kstars";
    return QStandardPaths::writableLocation(type) + dir;
}

