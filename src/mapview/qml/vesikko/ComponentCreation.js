var component
var vessel

function createVessel(id, lat, lon, type) {
    console.log("CreateVessel " + id + " " + lat + "/" + lon + " type " + type );
    component = Qt.createComponent("Vessel.qml");
    vessel = component.createObject(map);
    vessel.lat = lat;
    vessel.lon = lon;
    vessel.objectName = "vessel-" + id
    vessel.vesselId = id
    if(type==1)
        vessel.source = "ship.png"
    if(type==2)
        vessel.source = "torpedo.png"
    map.deleteVesselSignal.connect(vessel.deleteVesselSignal)
}

function deleteVessel(id) {
    console.log("DeleteVessel " + id);
    map.deleteVesselSignal(id)
}
