import random
from sqlalchemy.orm import Session
from app.models import BusStop, BusService, BusStatus, SystemUser, UserRole, StationModule

# ── 5 Real Chennai Bus Stops ─────────────────────────────────────────────────

CHENNAI_STOPS = [
    {"name": "Koyambedu",  "area": "Western Terminus",       "latitude": 13.0694, "longitude": 80.1948},
    {"name": "T. Nagar",   "area": "Central Shopping District", "latitude": 13.0418, "longitude": 80.2341},
    {"name": "Tambaram",   "area": "Southern Suburb",         "latitude": 12.9249, "longitude": 80.1000},
    {"name": "Broadway",   "area": "Central Chennai",         "latitude": 13.0878, "longitude": 80.2785},
    {"name": "Adyar",      "area": "South Chennai",           "latitude": 13.0012, "longitude": 80.2565},
]

# Realistic Chennai MTC bus numbers
BUS_NUMBERS = [
    "1C", "5B", "5C", "12G", "17A", "21G", "23C", "27H", "29C", "33D",
    "36A", "45A", "47D", "49", "52K", "54A", "70", "101", "102", "115",
]


def seed_database(db: Session) -> None:
    """Populate bus stops and initial bus services. Idempotent — skips if data exists."""

    # Skip if already seeded
    if db.query(BusStop).count() > 0:
        return

    # ── Insert bus stops ──────────────────────────────────────────────────
    stop_objects = []
    for stop_data in CHENNAI_STOPS:
        stop = BusStop(**stop_data)
        db.add(stop)
        stop_objects.append(stop)

    db.flush()  # Ensure IDs are assigned

    # ── Generate 10 bus services with varied routes ─────────────────────
    # Each bus gets a random source and destination (must be different)
    used_bus_numbers = random.sample(BUS_NUMBERS, k=10)

    for bus_number in used_bus_numbers:
        # Pick a random pair of distinct stops as source and destination
        src_stop, dst_stop = random.sample(stop_objects, 2)

        # Build the route between source and destination
        # Current stop starts at the source
        service = BusService(
            bus_number=bus_number,
            source_id=src_stop.id,
            destination_id=dst_stop.id,
            current_stop_id=src_stop.id,
            status=BusStatus.RUNNING,
        )
        db.add(service)

    db.commit()
    # ── Seed default station module ────────────────────────────────────
    if db.query(StationModule).count() == 0:
        db.add(StationModule(name="Default Station", assigned_stop_id=stop_objects[0].id))
        db.commit()
        print(f"[OK] Seeded default station module (assigned to {stop_objects[0].name}).")

    print(f"[OK] Seeded {len(stop_objects)} bus stops and 10 bus services.")

    # ── Seed system users (admin + driver) ────────────────────────────
    if db.query(SystemUser).count() == 0:
        db.add(SystemUser(username="admin", password="admin123", name="Administrator", role=UserRole.ADMIN))
        db.add(SystemUser(username="driver", password="driver123", name="Driver", role=UserRole.DRIVER))
        db.commit()
        print("[OK] Seeded default admin and driver accounts.")
