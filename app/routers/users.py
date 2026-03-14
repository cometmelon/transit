from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session, joinedload
from app.database import get_db
from app.models import User, BusStop
from app.schemas import (
    UserOut,
    UserCreateRequest,
    SetDestinationRequest,
    BusStopOut,
)

router = APIRouter(prefix="/api/users", tags=["Users"])


# ── List all users ────────────────────────────────────────────────────────────

@router.get("/", response_model=list[UserOut])
def list_users(db: Session = Depends(get_db)):
    """Return every registered user."""
    return (
        db.query(User)
        .options(joinedload(User.destination_stop))
        .all()
    )


# ── Endpoint 6: Create user (name + RFID) ────────────────────────────────────

@router.post("/", response_model=UserOut, status_code=201)
def create_user(payload: UserCreateRequest, db: Session = Depends(get_db)):
    """Register a new user at the station with name and RFID number."""
    # Check for duplicate RFID
    existing = db.query(User).filter(User.rfid_number == payload.rfid_number).first()
    if existing:
        raise HTTPException(status_code=400, detail="RFID number already registered")

    user = User(name=payload.name, rfid_number=payload.rfid_number, guardian_phone=payload.guardian_phone)
    db.add(user)
    db.commit()
    db.refresh(user)
    return user


# ── Endpoint 6b: Fetch user data at the station ──────────────────────────────

@router.get("/{user_id}", response_model=UserOut)
def get_user(user_id: int, db: Session = Depends(get_db)):
    """Fetch user data at the station by user ID."""
    user = (
        db.query(User)
        .options(joinedload(User.destination_stop))
        .filter(User.id == user_id)
        .first()
    )
    if not user:
        raise HTTPException(status_code=404, detail="User not found")
    return user


# ── Endpoint: Lookup user by RFID number ──────────────────────────────────────

@router.get("/rfid/{rfid_number}", response_model=UserOut)
def get_user_by_rfid(rfid_number: str, db: Session = Depends(get_db)):
    """Look up a user by their RFID tag number (used by station EM-18 reader)."""
    user = (
        db.query(User)
        .options(joinedload(User.destination_stop))
        .filter(User.rfid_number == rfid_number)
        .first()
    )
    if not user:
        raise HTTPException(status_code=404, detail="User not found for this RFID")
    return user


# ── Endpoint 5: Set destination bus stop ──────────────────────────────────────

@router.post("/{user_id}/destination", response_model=UserOut)
def set_destination(
    user_id: int, payload: SetDestinationRequest, db: Session = Depends(get_db)
):
    """Set the user's destination bus stop along with their current bus stop."""
    user = db.query(User).filter(User.id == user_id).first()
    if not user:
        raise HTTPException(status_code=404, detail="User not found")

    # Validate destination stop
    dest_stop = db.query(BusStop).filter(BusStop.id == payload.destination_stop_id).first()
    if not dest_stop:
        raise HTTPException(status_code=404, detail="Destination bus stop not found")

    # Validate current stop
    current_stop = db.query(BusStop).filter(BusStop.id == payload.current_stop_id).first()
    if not current_stop:
        raise HTTPException(status_code=404, detail="Current bus stop not found")

    user.destination_stop_id = dest_stop.id
    db.commit()
    db.refresh(user)

    return (
        db.query(User)
        .options(joinedload(User.destination_stop))
        .filter(User.id == user_id)
        .first()
    )

