from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session, joinedload
from app.database import get_db
from app.models import Trip, TripStatus, User, BusService, BusStatus, BusStop
from app.schemas import TripOut, TripCreateRequest, TripVerifyRequest

router = APIRouter(prefix="/api/trips", tags=["Trips"])


# ── List all trips ────────────────────────────────────────────────────────────

@router.get("/", response_model=list[TripOut])
def list_trips(db: Session = Depends(get_db)):
    """Return every trip (all statuses), newest first."""
    return (
        db.query(Trip)
        .options(joinedload(Trip.from_stop), joinedload(Trip.to_stop))
        .order_by(Trip.created_at.desc())
        .all()
    )


# ── Check active trip by RFID ─────────────────────────────────────────────────

@router.get("/active/{rfid}", response_model=TripOut)
def get_active_trip(rfid: str, db: Session = Depends(get_db)):
    """Check if this RFID card has an active trip (WAITING or BOARDED)."""
    user = db.query(User).filter(User.rfid_number == rfid).first()
    if not user:
        raise HTTPException(status_code=404, detail="User not found")

    trip = (
        db.query(Trip)
        .options(joinedload(Trip.from_stop), joinedload(Trip.to_stop))
        .filter(
            Trip.user_id == user.id,
            Trip.status.in_([TripStatus.WAITING, TripStatus.BOARDED]),
        )
        .order_by(Trip.created_at.desc())
        .first()
    )

    if not trip:
        raise HTTPException(status_code=404, detail="No active trip")

    return trip


# ── Get current bus position for a boarded trip ───────────────────────────────

@router.get("/bus-position/{rfid}")
def get_bus_position(rfid: str, db: Session = Depends(get_db)):
    """Return the current stop of the bus assigned to this passenger's boarded trip."""
    user = db.query(User).filter(User.rfid_number == rfid).first()
    if not user:
        raise HTTPException(status_code=404, detail="User not found")

    trip = (
        db.query(Trip)
        .filter(Trip.user_id == user.id, Trip.status == TripStatus.BOARDED)
        .order_by(Trip.created_at.desc())
        .first()
    )
    if not trip:
        raise HTTPException(status_code=404, detail="No boarded trip")

    bus = db.query(BusService).filter(
        BusService.bus_number == trip.assigned_bus_number,
        BusService.status == BusStatus.RUNNING,
    ).first()
    if not bus:
        raise HTTPException(status_code=404, detail="Bus not found")

    stop = db.query(BusStop).filter(BusStop.id == bus.current_stop_id).first()
    return {"stop_id": stop.id, "stop_name": stop.name}


# ── Create a new trip ─────────────────────────────────────────────────────────

@router.post("/", response_model=TripOut, status_code=201)
def create_trip(payload: TripCreateRequest, db: Session = Depends(get_db)):
    """Create a new trip after passenger books a destination."""
    # Cancel any existing active trips for this user
    active_trips = (
        db.query(Trip)
        .filter(
            Trip.user_id == payload.user_id,
            Trip.status.in_([TripStatus.WAITING, TripStatus.BOARDED]),
        )
        .all()
    )
    for t in active_trips:
        t.status = TripStatus.COMPLETED

    trip = Trip(
        user_id=payload.user_id,
        from_stop_id=payload.from_stop_id,
        to_stop_id=payload.to_stop_id,
        assigned_bus_number=payload.assigned_bus_number,
        status=TripStatus.WAITING,
    )
    db.add(trip)
    db.commit()
    db.refresh(trip)

    return (
        db.query(Trip)
        .options(joinedload(Trip.from_stop), joinedload(Trip.to_stop))
        .filter(Trip.id == trip.id)
        .first()
    )


# ── Verify trip (tap at stop/on bus) ──────────────────────────────────────────

@router.post("/verify/{rfid}")
def verify_trip(
    rfid: str, payload: TripVerifyRequest, db: Session = Depends(get_db)
):
    """
    Verify a trip when passenger taps card again.
    - WAITING → checks if they're still at the origin → reminds them to wait
    - WAITING + at a bus stop → advances to BOARDED
    - BOARDED + at destination → advances to COMPLETED
    - BOARDED + not at destination → tells them to stay on bus
    """
    user = db.query(User).filter(User.rfid_number == rfid).first()
    if not user:
        raise HTTPException(status_code=404, detail="User not found")

    trip = (
        db.query(Trip)
        .options(joinedload(Trip.from_stop), joinedload(Trip.to_stop))
        .filter(
            Trip.user_id == user.id,
            Trip.status.in_([TripStatus.WAITING, TripStatus.BOARDED]),
        )
        .order_by(Trip.created_at.desc())
        .first()
    )

    if not trip:
        # Check if there's a recently completed trip (auto-completed by simulator)
        completed_trip = (
            db.query(Trip)
            .options(joinedload(Trip.from_stop), joinedload(Trip.to_stop))
            .filter(
                Trip.user_id == user.id,
                Trip.status == TripStatus.COMPLETED,
            )
            .order_by(Trip.created_at.desc())
            .first()
        )
        if completed_trip:
            return {
                "status": "COMPLETED",
                "message": "You have reached your destination",
                "destination": completed_trip.to_stop.name,
                "bus_number": completed_trip.assigned_bus_number,
                "bus_at": completed_trip.to_stop.name,
                "tapped_at": "",
            }
        raise HTTPException(status_code=404, detail="No active trip")

    current_stop_id = payload.current_stop_id

    if trip.status == TripStatus.WAITING:
        # Check if the assigned bus is actually at this stop
        bus = (
            db.query(BusService)
            .options(joinedload(BusService.current_stop))
            .filter(
                BusService.bus_number == trip.assigned_bus_number,
                BusService.status == BusStatus.RUNNING,
            )
            .first()
        )

        if bus and bus.current_stop_id == current_stop_id:
            # Bus IS at this stop — allow boarding!
            trip.status = TripStatus.BOARDED
            db.commit()
            return {
                "status": "BOARDED",
                "message": "Your bus has arrived. Please board",
                "bus_number": trip.assigned_bus_number,
                "destination": trip.to_stop.name,
            }
        else:
            # Bus is NOT at this stop yet
            bus_location = bus.current_stop.name if bus else "unknown"
            return {
                "status": "BUS_NOT_HERE",
                "message": "Your bus has not arrived yet",
                "bus_number": trip.assigned_bus_number,
                "destination": trip.to_stop.name,
                "bus_at": bus_location,
            }

    elif trip.status == TripStatus.BOARDED:
        # Get the device's tapped stop info (for logging)
        device_stop = db.query(BusStop).filter(BusStop.id == current_stop_id).first()
        device_stop_name = device_stop.name if device_stop else "unknown"

        # Check the bus's simulated position to determine if destination reached
        bus = (
            db.query(BusService)
            .options(joinedload(BusService.current_stop))
            .filter(
                BusService.bus_number == trip.assigned_bus_number,
                BusService.status == BusStatus.RUNNING,
            )
            .first()
        )
        bus_current_stop_name = bus.current_stop.name if bus else "unknown"

        # Check if the bus has reached or passed the destination
        bus_at_or_past_dest = False
        if bus:
            from app.routers.buses import _get_route_stop_ids
            route = _get_route_stop_ids(db, bus.source_id, bus.destination_id)
            try:
                bus_pos = route.index(bus.current_stop_id)
                dest_pos = route.index(trip.to_stop_id)
                stops_remaining = max(0, dest_pos - bus_pos)
                # Bus is at or has passed the destination
                if bus_pos >= dest_pos:
                    bus_at_or_past_dest = True
            except ValueError:
                stops_remaining = 0
                # If destination is no longer on the route (bus reversed past it),
                # that means it already passed through — allow completion
                if trip.to_stop_id not in route:
                    bus_at_or_past_dest = True
        else:
            stops_remaining = 0

        if bus_at_or_past_dest:
            # Bus has reached/passed destination — card tap completes the trip
            trip.status = TripStatus.COMPLETED
            db.commit()
            return {
                "status": "COMPLETED",
                "message": "You have reached your destination",
                "destination": trip.to_stop.name,
                "bus_at": bus_current_stop_name,
                "tapped_at": device_stop_name,
            }
        else:
            # Bus hasn't reached destination yet
            return {
                "status": "ON_ROUTE",
                "message": "Not your stop yet",
                "destination": trip.to_stop.name,
                "bus_at": bus_current_stop_name,
                "tapped_at": device_stop_name,
                "stops_remaining": stops_remaining,
            }


