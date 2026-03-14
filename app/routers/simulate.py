from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session, joinedload
from app.database import get_db
from app.models import BusService, BusStop, BusStatus

router = APIRouter(prefix="/api/simulate", tags=["Simulate"])


@router.get("/arrive")
def simulate_bus_arrival(bus_number: str, stop_id: int, db: Session = Depends(get_db)):
    """
    Force a bus to arrive at a specific stop.
    Usage: GET /api/simulate/arrive?bus_number=101&stop_id=1
    """
    bus = (
        db.query(BusService)
        .filter(BusService.bus_number == bus_number)
        .first()
    )
    if not bus:
        raise HTTPException(status_code=404, detail=f"Bus {bus_number} not found")

    stop = db.query(BusStop).filter(BusStop.id == stop_id).first()
    if not stop:
        raise HTTPException(status_code=404, detail=f"Stop {stop_id} not found")

    bus.current_stop_id = stop_id
    db.commit()

    return {
        "message": f"Bus {bus_number} moved to {stop.name}",
        "bus_number": bus_number,
        "stop_id": stop_id,
        "stop_name": stop.name,
    }


@router.get("/buses")
def list_all_buses(db: Session = Depends(get_db)):
    """Show all buses and their current positions — useful for demo."""
    buses = (
        db.query(BusService)
        .options(joinedload(BusService.current_stop))
        .filter(BusService.status == BusStatus.RUNNING)
        .all()
    )
    return [
        {
            "bus_number": b.bus_number,
            "current_stop": b.current_stop.name,
            "current_stop_id": b.current_stop_id,
            "source_id": b.source_id,
            "destination_id": b.destination_id,
        }
        for b in buses
    ]


@router.get("/stops")
def list_all_stops(db: Session = Depends(get_db)):
    """Show all stops — useful for picking stop_id."""
    stops = db.query(BusStop).order_by(BusStop.id).all()
    return [{"id": s.id, "name": s.name} for s in stops]
