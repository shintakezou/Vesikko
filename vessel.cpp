#include "vessel.h"
#include <QDebug>
#include <math.h>

Vessel::Vessel(QObject *parent, int i) :
    QObject(parent), id(i)
{
    x = y = depth = heading = speed = helm = verticalVelocity = 0;
    type = 0;
}

void Vessel::setHelm(int h){
    helm = h;
}

void Vessel::setSpeed(int s){
    if(s == -1) speed = -5;
    if(s == 0) speed = 0;
    if(s == 1) speed = 5;
    if(s == 2) speed = 10;
    if(s == 3) speed = 20;
}

void Vessel::setDepthChange(int s){
    if(s == -1) verticalVelocity = -5;
    if(s == 0) verticalVelocity = 0;
    if(s == 1) verticalVelocity = 5;
}

void Vessel::tickTime(double dt, int total) {
    x += sin(heading * (M_PI/180.0)) * speed * dt;
    y -= cos(heading * (M_PI/180.0)) * speed * dt;
    depth += verticalVelocity * dt;
    if(depth < 0) depth = 0;
    heading += helm * 3 * dt;
    emit vesselUpdated(this);
}
