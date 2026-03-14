import asyncio
import random
from datetime import datetime
from sqlalchemy.orm import Session
from app.database import SessionLocal
from app.models import BusStop, BusService, BusStatus, Trip, TripStatus, User

# ── Tunable simulation parameters ────────────────────────────────────────────
MIN_TRAVEL_TIME = 15   # minimum seconds between stops
MAX_TRAVEL_TIME = 30   # maximum seconds between stops
MAX_DEPARTURE_OFFSET = 20  # max initial stagger delay (seconds)
HEARTBEAT_INTERVAL = 10    # seconds between simulated heartbeat updates


def _get_route(db: Session, service: BusService) -> list[int]:
    """
    Build a deterministic route for a bus service.
    Route: source → intermediate stops (ordered by id) → destination.
    """
    all_stop_ids = [s.id for s in db.query(BusStop).order_by(BusStop.id).all()]

    src = service.source_id
    dst = service.destination_id

    # Build route from source to destination, passing through stops in order
    if src <= dst:
        route = [sid for sid in all_stop_ids if src <= sid <= dst]
    else:
        # Reverse direction: go through stops from src down to dst
        route = [sid for sid in all_stop_ids if dst <= sid <= src]
        route.reverse()

    # Ensure source is first and destination is last
    if route and route[0] != src:
        route.insert(0, src)
    if route and route[-1] != dst:
        route.append(dst)

    # Remove duplicates while preserving order
    seen = set()
    unique_route = []
    for sid in route:
        if sid not in seen:
            seen.add(sid)
            unique_route.append(sid)

    return unique_route


def _advance_bus(db: Session, service: BusService) -> None:
    """Move a running bus one stop forward along its route."""
    route = _get_route(db, service)

    try:
        current_index = route.index(service.current_stop_id)
    except ValueError:
        # Current stop not on route — reset to source
        service.current_stop_id = service.source_id
        return

    if current_index + 1 < len(route):
        # Move to the next stop
        service.current_stop_id = route[current_index + 1]

        # If we reached the destination, reverse direction
        if service.current_stop_id == service.destination_id:
            _reset_bus(db, service)
    else:
        # At the end of the route — reverse
        _reset_bus(db, service)


def _reset_bus(db: Session, service: BusService) -> None:
    """
    Reverse the bus direction for a natural round-trip.
    Swaps source ↔ destination so the bus runs back and forth on its route.
    """
    old_src = service.source_id
    old_dst = service.destination_id
    service.source_id = old_dst
    service.destination_id = old_src
    service.current_stop_id = old_dst
    service.status = BusStatus.RUNNING
    print(f"[SIM] Bus {service.bus_number} reversed: now {old_dst} -> {old_src}")


# ── Per-bus independent coroutine ─────────────────────────────────────────────

async def _run_single_bus(bus_id: int, bus_number: str, departure_offset: float) -> None:
    """
    Independent async loop for a single bus.
    Each bus waits its own staggered departure offset, then advances
    one stop at a time with a randomized travel time between stops.
    """
    # Staggered departure: wait before first move
    print(f"[SIM] Bus {bus_number} departing in {departure_offset:.0f}s...")
    await asyncio.sleep(departure_offset)
    print(f"[SIM] Bus {bus_number} is now active")

    while True:
        # Randomized travel time for this leg
        travel_time = random.uniform(MIN_TRAVEL_TIME, MAX_TRAVEL_TIME)
        await asyncio.sleep(travel_time)

        db: Session = SessionLocal()
        try:
            service = db.query(BusService).filter(BusService.id == bus_id).first()
            if not service or service.status != BusStatus.RUNNING:
                continue

            old_stop = service.current_stop_id
            _advance_bus(db, service)
            new_stop = service.current_stop_id

            db.commit()
            if old_stop != new_stop:
                print(f"[SIM] Bus {bus_number}: Stop {old_stop} -> {new_stop}  (travel {travel_time:.0f}s)")

        except Exception as e:
            db.rollback()
            print(f"[SIM] Bus {bus_number} error: {e}")
        finally:
            db.close()


# ── Heartbeat simulation helpers ──────────────────────────────────────────────

def _generate_bpm() -> int:
    """Generate a realistic BPM value with occasional abnormal readings."""
    roll = random.random()
    if roll < 0.03:
        # ~3% chance of critical reading (for demo)
        return random.randint(151, 180)
    elif roll < 0.13:
        # ~10% chance of warning reading
        return random.choice([
            random.randint(45, 59),    # low warning
            random.randint(101, 130),  # high warning
        ])
    else:
        # ~87% normal reading
        return random.randint(65, 90)


def _bpm_status(bpm: int) -> str:
    """Classify BPM into normal/warning/critical."""
    if bpm < 40 or bpm > 150:
        return "critical"
    if bpm < 60 or bpm > 100:
        return "warning"
    return "normal"


async def _simulate_heartbeats() -> None:
    """
    Background coroutine that generates simulated heartbeat data
    for all boarded passengers. Writes directly to the in-memory
    live_heartbeats store in alerts.py.
    """
    from app.routers.alerts import live_heartbeats

    print("[SIM] Heartbeat simulator starting...")
    await asyncio.sleep(5)  # Wait for initial data to settle

    while True:
        db: Session = SessionLocal()
        try:
            # Find all boarded trips
            boarded_trips = (
                db.query(Trip)
                .filter(Trip.status == TripStatus.BOARDED)
                .all()
            )

            for trip in boarded_trips:
                user = db.query(User).filter(User.id == trip.user_id).first()
                if not user:
                    continue

                bpm = _generate_bpm()
                live_heartbeats[user.rfid_number] = {
                    "rfid": user.rfid_number,
                    "user_name": user.name,
                    "user_id": user.id,
                    "trip_id": trip.id,
                    "bus_number": trip.assigned_bus_number,
                    "bpm": bpm,
                    "status": _bpm_status(bpm),
                    "updated_at": datetime.utcnow().isoformat(),
                }

            if boarded_trips:
                print(f"[SIM] Heartbeat: updated {len(boarded_trips)} boarded passengers")

        except Exception as e:
            print(f"[SIM] Heartbeat error: {e}")
        finally:
            db.close()

        await asyncio.sleep(HEARTBEAT_INTERVAL)


# ── Main simulator entry point ────────────────────────────────────────────────

async def run_simulator() -> None:
    """
    Asynchronous simulation engine.
    Spawns one independent coroutine per bus with a staggered departure offset
    and randomized per-leg travel times, creating realistic asynchronous flow.
    Also spawns a heartbeat simulator for live BPM data on the driver dashboard.
    """
    print("[SIM] Async bus simulator starting...")

    db: Session = SessionLocal()
    try:
        running_buses = db.query(BusService).filter(
            BusService.status == BusStatus.RUNNING
        ).all()

        bus_info = [(b.id, b.bus_number) for b in running_buses]
    finally:
        db.close()

    if not bus_info:
        print("[SIM] No running buses found. Simulator idle.")
        # Keep alive so the task doesn't exit
        while True:
            await asyncio.sleep(60)

    # Spawn one coroutine per bus, each with a random departure offset
    tasks = []
    for bus_id, bus_number in bus_info:
        offset = random.uniform(0, MAX_DEPARTURE_OFFSET)
        task = asyncio.create_task(_run_single_bus(bus_id, bus_number, offset))
        tasks.append(task)

    # Spawn the heartbeat simulator
    hb_task = asyncio.create_task(_simulate_heartbeats())
    tasks.append(hb_task)

    print(f"[SIM] Launched {len(tasks) - 1} bus coroutines + 1 heartbeat simulator")

    # Wait for all (they run forever, so this blocks until cancellation)
    await asyncio.gather(*tasks)
