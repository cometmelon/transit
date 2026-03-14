from fastapi import APIRouter, Depends, HTTPException, Query
from sqlalchemy.orm import Session, joinedload
from app.database import get_db
from app.models import BusStop, BusService, BusStatus
from app.schemas import BusServiceOut, BusStopOut

router = APIRouter(prefix="/api/buses", tags=["Bus Services"])


def _get_route_stop_ids(db: Session, source_id: int, destination_id: int) -> list[int]:
    """
    Build a route of stop IDs from source to destination.
    Stops are ordered by their ID.
    """
    all_stop_ids = [s.id for s in db.query(BusStop).order_by(BusStop.id).all()]

    if source_id <= destination_id:
        route = [sid for sid in all_stop_ids if source_id <= sid <= destination_id]
    else:
        # Reverse direction: go through stops from source down to destination
        route = [sid for sid in all_stop_ids if destination_id <= sid <= source_id]
        route.reverse()

    return route


def _get_eligible_buses(
    db: Session, current_stop_id: int, destination_stop_id: int
) -> list[tuple[BusService, int, list[int]]]:
    """
    Returns a list of (bus, distance_to_current_stop, route) tuples for buses
    that can take a passenger from current_stop to destination_stop.

    A bus is eligible if:
      - Its route passes through BOTH current_stop and destination_stop
      - destination comes AFTER current_stop in the route order
      - The bus hasn't yet passed the current_stop (bus_position <= current_idx)
    """
    # Validate stop IDs
    for stop_id, label in [(current_stop_id, "Current"), (destination_stop_id, "Destination")]:
        if not db.query(BusStop).filter(BusStop.id == stop_id).first():
            raise HTTPException(status_code=404, detail=f"{label} bus stop with id {stop_id} not found")

    buses = (
        db.query(BusService)
        .options(
            joinedload(BusService.source),
            joinedload(BusService.destination),
            joinedload(BusService.current_stop),
        )
        .filter(BusService.status == BusStatus.RUNNING)
        .all()
    )

    all_stop_ids = [s.id for s in db.query(BusStop).order_by(BusStop.id).all()]

    eligible = []
    for bus in buses:
        # Check the bus's CURRENT route
        route = _get_route_stop_ids(db, bus.source_id, bus.destination_id)

        if current_stop_id in route and destination_stop_id in route:
            current_idx = route.index(current_stop_id)
            dest_idx = route.index(destination_stop_id)

            if dest_idx > current_idx:
                try:
                    bus_position = route.index(bus.current_stop_id)
                except ValueError:
                    continue

                if bus_position <= current_idx:
                    distance = current_idx - bus_position
                    eligible.append((bus, distance, route))
                    continue

        # Also check the REVERSED route (bus will reverse when it hits destination)
        rev_src = bus.destination_id
        rev_dst = bus.source_id
        rev_route = _get_route_stop_ids(db, rev_src, rev_dst)

        if current_stop_id in rev_route and destination_stop_id in rev_route:
            current_idx = rev_route.index(current_stop_id)
            dest_idx = rev_route.index(destination_stop_id)

            if dest_idx > current_idx:
                # Bus will serve this route AFTER it reverses
                # Distance = remaining stops to destination + current_idx in reversed route
                try:
                    bus_position = route.index(bus.current_stop_id) if bus.current_stop_id in route else 0
                except ValueError:
                    bus_position = 0
                remaining_on_current = max(0, len(route) - 1 - bus_position)
                total_distance = remaining_on_current + current_idx
                eligible.append((bus, total_distance, rev_route))

    return eligible


@router.get("/", response_model=list[BusServiceOut])
def get_running_buses(
    current_stop_id: int = Query(..., description="User's current bus stop ID"),
    destination_stop_id: int = Query(..., description="User's destination bus stop ID"),
    db: Session = Depends(get_db),
):
    """
    Fetch all running buses that pass through the user's current stop
    and continue to the destination stop.
    Filters OUT buses that have already crossed past the current stop.
    """
    eligible = _get_eligible_buses(db, current_stop_id, destination_stop_id)
    return [bus for bus, _, _ in eligible]


def _stop_to_dict(stop: BusStop) -> dict:
    """Convert a BusStop SQLAlchemy object to a serializable dict."""
    return {
        "id": stop.id,
        "name": stop.name,
        "area": stop.area,
        "latitude": stop.latitude,
        "longitude": stop.longitude,
    }


@router.get("/nearest")
def get_nearest_bus(
    current_stop_id: int = Query(..., description="User's current bus stop ID"),
    destination_stop_id: int = Query(..., description="User's destination bus stop ID"),
    db: Session = Depends(get_db),
):
    """
    Fetch the single nearest bus approaching the user's current stop
    that will continue to the destination stop.
    Returns the bus with the fewest stops remaining before reaching the current stop.
    """
    eligible = _get_eligible_buses(db, current_stop_id, destination_stop_id)

    if not eligible:
        raise HTTPException(
            status_code=404,
            detail="No buses currently approaching this stop toward the given destination",
        )

    # Sort by distance (fewest stops away = closest), pick first
    eligible.sort(key=lambda x: x[1])
    nearest_bus, stops_away, _ = eligible[0]

    return {
        "id": nearest_bus.id,
        "bus_number": nearest_bus.bus_number,
        "source": _stop_to_dict(nearest_bus.source),
        "destination": _stop_to_dict(nearest_bus.destination),
        "current_stop": _stop_to_dict(nearest_bus.current_stop),
        "status": nearest_bus.status,
        "stops_away": stops_away,
    }


@router.get("/{bus_number}/at-stop/{stop_id}")
def check_bus_at_stop(bus_number: str, stop_id: int, db: Session = Depends(get_db)):
    """Check if a specific bus is currently at a given stop."""
    bus = db.query(BusService).options(
        joinedload(BusService.current_stop)
    ).filter(
        BusService.bus_number == bus_number,
        BusService.status == BusStatus.RUNNING,
    ).first()

    if not bus:
        raise HTTPException(status_code=404, detail="Bus not found")

    arrived = bus.current_stop_id == stop_id
    return {
        "arrived": arrived,
        "current_stop_id": bus.current_stop_id,
        "current_stop_name": bus.current_stop.name if bus.current_stop else "Unknown",
    }
