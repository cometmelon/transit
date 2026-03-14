from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session, joinedload
from app.database import get_db
from app.models import StationModule, BusStop
from app.schemas import StationModuleOut, SetBusStopRequest

router = APIRouter(prefix="/api/station", tags=["Station Module"])


def _get_or_create_station(db: Session) -> StationModule:
    """Return the station module record, creating a default one if none exists."""
    station = db.query(StationModule).first()
    if not station:
        first_stop = db.query(BusStop).order_by(BusStop.id).first()
        if not first_stop:
            raise HTTPException(status_code=500, detail="No bus stops in database")
        station = StationModule(name="Default Station", assigned_stop_id=first_stop.id)
        db.add(station)
        db.commit()
        db.refresh(station)
    return station


@router.get("/", response_model=StationModuleOut)
def get_station(db: Session = Depends(get_db)):
    """Get the station module's currently assigned bus stop."""
    station = _get_or_create_station(db)
    return (
        db.query(StationModule)
        .options(joinedload(StationModule.assigned_stop))
        .filter(StationModule.id == station.id)
        .first()
    )


@router.post("/", response_model=StationModuleOut)
def set_station_stop(payload: SetBusStopRequest, db: Session = Depends(get_db)):
    """Set the bus stop assigned to the station module."""
    stop = db.query(BusStop).filter(BusStop.id == payload.stop_id).first()
    if not stop:
        raise HTTPException(status_code=404, detail="Bus stop not found")

    station = _get_or_create_station(db)
    station.assigned_stop_id = stop.id
    db.commit()
    db.refresh(station)

    return (
        db.query(StationModule)
        .options(joinedload(StationModule.assigned_stop))
        .filter(StationModule.id == station.id)
        .first()
    )
