from fastapi import APIRouter, Depends
from sqlalchemy.orm import Session
from app.database import get_db
from app.models import BusStop
from app.schemas import BusStopOut

router = APIRouter(prefix="/api/stops", tags=["Bus Stops"])


@router.get("/", response_model=list[BusStopOut])
def get_all_stops(db: Session = Depends(get_db)):
    """Get all available bus stops."""
    return db.query(BusStop).all()
