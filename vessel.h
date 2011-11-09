#ifndef SUBMARINE_H
#define SUBMARINE_H

#include <QObject>

class Vessel : public QObject
{
    Q_OBJECT
public:
    explicit Vessel(QObject *parent, int i);
    double x, y, depth, verticalVelocity, heading, speed, helm;
    int id, type;
signals:
    void vesselUpdated(Vessel *v);

public slots:
    void setHelm(int h);
    void setSpeed(int s);
    void setDepthChange(int s);
    void tickTime(double dt, int total);
};

#endif // SUBMARINE_H
