from fastapi import APIRouter, Depends, HTTPException
from sqlalchemy.orm import Session
from pydantic import BaseModel
from app.database import get_db
from app.models import SystemUser

router = APIRouter(prefix="/api/auth", tags=["Auth"])


class LoginRequest(BaseModel):
    username: str
    password: str


@router.post("/login")
def login(req: LoginRequest, db: Session = Depends(get_db)):
    """Authenticate admin or driver and return their role."""
    user = db.query(SystemUser).filter(SystemUser.username == req.username).first()

    if not user or user.password != req.password:
        raise HTTPException(status_code=401, detail="Invalid username or password")

    return {
        "success": True,
        "role": user.role.value,
        "name": user.name,
    }
