from sqlalchemy import Column, Integer, String, Float, ForeignKey, Enum as SAEnum, DateTime
from sqlalchemy.orm import relationship
from app.database import Base
import enum
from datetime import datetime


class BusStatus(str, enum.Enum):
    RUNNING = "running"
    COMPLETED = "completed"


class BusStop(Base):
    __tablename__ = "bus_stops"

    id = Column(Integer, primary_key=True, index=True)
    name = Column(String, unique=True, nullable=False)
    area = Column(String, nullable=False)
    latitude = Column(Float, nullable=False)
    longitude = Column(Float, nullable=False)

    def __repr__(self):
        return f"<BusStop(id={self.id}, name='{self.name}')>"


class BusService(Base):
    __tablename__ = "bus_services"

    id = Column(Integer, primary_key=True, index=True)
    bus_number = Column(String, nullable=False)
    source_id = Column(Integer, ForeignKey("bus_stops.id"), nullable=False)
    destination_id = Column(Integer, ForeignKey("bus_stops.id"), nullable=False)
    current_stop_id = Column(Integer, ForeignKey("bus_stops.id"), nullable=False)
    status = Column(SAEnum(BusStatus), default=BusStatus.RUNNING, nullable=False)

    source = relationship("BusStop", foreign_keys=[source_id])
    destination = relationship("BusStop", foreign_keys=[destination_id])
    current_stop = relationship("BusStop", foreign_keys=[current_stop_id])

    def __repr__(self):
        return f"<BusService(id={self.id}, bus='{self.bus_number}', status='{self.status}')>"


class User(Base):
    __tablename__ = "users"

    id = Column(Integer, primary_key=True, index=True)
    name = Column(String, nullable=False)
    rfid_number = Column(String, unique=True, nullable=False)
    destination_stop_id = Column(Integer, ForeignKey("bus_stops.id"), nullable=True)
    guardian_phone = Column(String, nullable=True)

    destination_stop = relationship("BusStop", foreign_keys=[destination_stop_id])

    def __repr__(self):
        return f"<User(id={self.id}, name='{self.name}', rfid='{self.rfid_number}')"


class StationModule(Base):
    __tablename__ = "station_modules"

    id = Column(Integer, primary_key=True, index=True)
    name = Column(String, nullable=False, default="Default Station")
    assigned_stop_id = Column(Integer, ForeignKey("bus_stops.id"), nullable=False)

    assigned_stop = relationship("BusStop", foreign_keys=[assigned_stop_id])

    def __repr__(self):
        return f"<StationModule(id={self.id}, name='{self.name}', stop={self.assigned_stop_id})>"


class TripStatus(str, enum.Enum):
    WAITING = "waiting"
    BOARDED = "boarded"
    COMPLETED = "completed"


class Trip(Base):
    __tablename__ = "trips"

    id = Column(Integer, primary_key=True, index=True)
    user_id = Column(Integer, ForeignKey("users.id"), nullable=False)
    from_stop_id = Column(Integer, ForeignKey("bus_stops.id"), nullable=False)
    to_stop_id = Column(Integer, ForeignKey("bus_stops.id"), nullable=False)
    assigned_bus_number = Column(String, nullable=False)
    status = Column(SAEnum(TripStatus), default=TripStatus.WAITING, nullable=False)
    created_at = Column(DateTime, default=datetime.utcnow, nullable=False)

    user = relationship("User", foreign_keys=[user_id])
    from_stop = relationship("BusStop", foreign_keys=[from_stop_id])
    to_stop = relationship("BusStop", foreign_keys=[to_stop_id])

    def __repr__(self):
        return f"<Trip(id={self.id}, user={self.user_id}, status='{self.status}')>"


class HealthAlert(Base):
    __tablename__ = "health_alerts"

    id = Column(Integer, primary_key=True, index=True)
    user_id = Column(Integer, ForeignKey("users.id"), nullable=False)
    trip_id = Column(Integer, ForeignKey("trips.id"), nullable=True)
    bpm = Column(Integer, nullable=False)
    sms_sent = Column(Integer, default=0)  # 0=no, 1=yes
    created_at = Column(DateTime, default=datetime.utcnow, nullable=False)

    user = relationship("User", foreign_keys=[user_id])
    trip = relationship("Trip", foreign_keys=[trip_id])

    def __repr__(self):
        return f"<HealthAlert(id={self.id}, user={self.user_id}, bpm={self.bpm})>"


class UserRole(str, enum.Enum):
    ADMIN = "admin"
    DRIVER = "driver"


class SystemUser(Base):
    __tablename__ = "system_users"

    id = Column(Integer, primary_key=True, index=True)
    username = Column(String, unique=True, nullable=False)
    password = Column(String, nullable=False)
    name = Column(String, nullable=False)
    role = Column(SAEnum(UserRole), nullable=False)

    def __repr__(self):
        return f"<SystemUser(id={self.id}, username='{self.username}', role='{self.role}')>"
