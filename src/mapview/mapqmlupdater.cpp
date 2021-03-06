#include "mapqmlupdater.h"
#include "../simulation/vessel.h"
#include <QVariant>
#include <QDebug>

MapQmlUpdater::MapQmlUpdater(QObject *parent) :
    QObject(parent)
{
}

void MapQmlUpdater::init(QObject *s, QObject *h, QObject *v) {
    subObject = s;
    helmObject = h;
    vesselsObject = v;
}

void MapQmlUpdater::vesselUpdated(Vessel *vessel) {
    QObject *vesselObject = 0;
    if(vessel->id==0) {
        vesselObject = subObject;
    } else {
        QString objectname = "vessel-" + QString::number(vessel->id);
        vesselObject = vesselsObject->findChild<QObject*>(objectname);
    }
    Q_ASSERT(vesselObject);
    vesselObject->setProperty("lat", vessel->x);
    vesselObject->setProperty("lon", vessel->y);
    vesselObject->setProperty("depth", vessel->depth);
    vesselObject->setProperty("rotation", vessel->heading);
    vesselObject->setProperty("speed", vessel->speed);
}

void MapQmlUpdater::createVessel(Vessel *sub) {
    qDebug() << Q_FUNC_INFO;
    QMetaObject::invokeMethod(vesselsObject, "createVessel",
                              Q_ARG(QVariant, sub->id),
                              Q_ARG(QVariant, sub->x),
                              Q_ARG(QVariant, sub->y),
                              Q_ARG(QVariant, sub->type));
}

void MapQmlUpdater::vesselDeleted(Vessel *sub) {
    qDebug() << Q_FUNC_INFO;
    QMetaObject::invokeMethod(vesselsObject, "deleteVessel", Q_ARG(QVariant, sub->id));
}
