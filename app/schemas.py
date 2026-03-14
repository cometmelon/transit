from pydantic import BaseModel
from typing import Optional


# ── Bus Stop ──────────────────────────────────────────────────────────────────

class BusStopOut(BaseModel):
    id: int
    name: str
    area: str
    latitude: float
    longitude: float

    model_config = {"from_attributes": True}


# ── Bus Service ───────────────────────────────────────────────────────────────

class BusServiceOut(BaseModel):
    id: int
    bus_number: str
    source: BusStopOut
    destination: BusStopOut
    current_stop: BusStopOut
    status: str

    model_config = {"from_attributes": True}


# ── User ──────────────────────────────────────────────────────────────────────

class UserCreateRequest(BaseModel):
    name: str
    rfid_number: str
    guardian_phone: Optional[str] = None


class SetBusStopRequest(BaseModel):
    stop_id: int


class SetDestinationRequest(BaseModel):
    destination_stop_id: int
    current_stop_id: int


class UserOut(BaseModel):
    id: int
    name: str
    rfid_number: str
    guardian_phone: Optional[str] = None
    destination_stop: Optional[BusStopOut] = None

    model_config = {"from_attributes": True}


# ── Station Module ────────────────────────────────────────────────────────────

class StationModuleOut(BaseModel):
    id: int
    name: str
    assigned_stop: BusStopOut

    model_config = {"from_attributes": True}


# ── Trip ──────────────────────────────────────────────────────────────────────

class TripCreateRequest(BaseModel):
    user_id: int
    from_stop_id: int
    to_stop_id: int
    assigned_bus_number: str


class TripVerifyRequest(BaseModel):
    current_stop_id: int


class TripOut(BaseModel):
    id: int
    user_id: int
    from_stop_id: int
    to_stop_id: int
    assigned_bus_number: str
    status: str
    from_stop: BusStopOut
    to_stop: BusStopOut

    model_config = {"from_attributes": True}


# -- Health Alert ---------------------------------------------------------------

class AlertCreateRequest(BaseModel):
    rfid: str
    bpm: int
    sms_sent: int = 0


class AlertOut(BaseModel):
    id: int
    user_id: int
    trip_id: Optional[int] = None
    bpm: int
    sms_sent: int
    created_at: str

    model_config = {"from_attributes": True}
