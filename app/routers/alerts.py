from datetime import datetime
from fastapi import APIRouter, Depends, HTTPException
from pydantic import BaseModel
from sqlalchemy.orm import Session
from app.database import get_db
from app.models import HealthAlert, User, Trip, TripStatus
from app.schemas import AlertCreateRequest, AlertOut

router = APIRouter(prefix="/api/alerts", tags=["Health Alerts"])

# ── In-memory live heartbeat store (keyed by RFID) ──────────────────────────
live_heartbeats: dict[str, dict] = {}


class HeartbeatIn(BaseModel):
    rfid: str
    bpm: int


def _bpm_status(bpm: int) -> str:
    if bpm < 40 or bpm > 150:
        return "critical"
    if bpm < 60 or bpm > 100:
        return "warning"
    return "normal"


@router.post("/", status_code=201)
def create_alert(payload: AlertCreateRequest, db: Session = Depends(get_db)):
    """Log a health alert from the hardware when abnormal heart rate is detected."""
    user = db.query(User).filter(User.rfid_number == payload.rfid).first()
    if not user:
        raise HTTPException(status_code=404, detail="User not found")

    # Find active trip if any
    active_trip = (
        db.query(Trip)
        .filter(
            Trip.user_id == user.id,
            Trip.status.in_([TripStatus.WAITING, TripStatus.BOARDED]),
        )
        .order_by(Trip.created_at.desc())
        .first()
    )

    alert = HealthAlert(
        user_id=user.id,
        trip_id=active_trip.id if active_trip else None,
        bpm=payload.bpm,
        sms_sent=payload.sms_sent,
    )
    db.add(alert)
    db.commit()
    db.refresh(alert)

    return {
        "id": alert.id,
        "user": user.name,
        "bpm": alert.bpm,
        "sms_sent": alert.sms_sent,
        "trip_id": alert.trip_id,
    }


@router.get("/")
def get_all_alerts(db: Session = Depends(get_db)):
    """Get all health alerts for the dashboard."""
    alerts = (
        db.query(HealthAlert)
        .order_by(HealthAlert.created_at.desc())
        .limit(50)
        .all()
    )
    results = []
    for a in alerts:
        user = db.query(User).filter(User.id == a.user_id).first()
        results.append({
            "id": a.id,
            "user_name": user.name if user else "Unknown",
            "rfid": user.rfid_number if user else "",
            "bpm": a.bpm,
            "sms_sent": a.sms_sent,
            "trip_id": a.trip_id,
            "created_at": str(a.created_at),
        })
    return results

# ── Live heartbeat POST (Arduino sends every reading) ────────────────────────

@router.post("/heartbeat")
def post_heartbeat(payload: HeartbeatIn, db: Session = Depends(get_db)):
    """Store the latest BPM for a passenger in memory (no DB write)."""
    user = db.query(User).filter(User.rfid_number == payload.rfid).first()
    user_name = user.name if user else "Unknown"

    # Find active trip to get assigned bus
    trip = None
    if user:
        trip = (
            db.query(Trip)
            .filter(
                Trip.user_id == user.id,
                Trip.status.in_([TripStatus.WAITING, TripStatus.BOARDED]),
            )
            .order_by(Trip.created_at.desc())
            .first()
        )

    live_heartbeats[payload.rfid] = {
        "rfid": payload.rfid,
        "user_name": user_name,
        "user_id": user.id if user else None,
        "trip_id": trip.id if trip else None,
        "bus_number": trip.assigned_bus_number if trip else None,
        "bpm": payload.bpm,
        "status": _bpm_status(payload.bpm),
        "updated_at": datetime.utcnow().isoformat(),
    }
    return {"ok": True, "bpm": payload.bpm, "status": _bpm_status(payload.bpm)}


# ── Live heartbeat GET (driver portal polls this) ────────────────────────────

@router.get("/heartbeat")
def get_live_heartbeats():
    """Return all live heartbeat readings for the driver portal."""
    return list(live_heartbeats.values())
