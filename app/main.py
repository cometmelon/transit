import asyncio
from contextlib import asynccontextmanager
from pathlib import Path
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.staticfiles import StaticFiles
from fastapi.responses import RedirectResponse
from app.database import engine, SessionLocal, Base
from app.models import BusStop, BusService, User, Trip, HealthAlert, SystemUser, StationModule  # noqa: F401 - ensure models are registered
from app.seed import seed_database
from app.simulator import run_simulator
from app.routers import stops, buses, users, trips, simulate, alerts, auth, station


@asynccontextmanager
async def lifespan(app: FastAPI):
    """Startup: create tables, seed data, start simulator. Shutdown: cancel simulator."""

    # ── Startup ───────────────────────────────────────────────────────────
    Base.metadata.create_all(bind=engine)
    print("[OK] Database tables created.")

    db = SessionLocal()
    try:
        seed_database(db)
    finally:
        db.close()

    # Launch the background bus simulator
    simulator_task = asyncio.create_task(run_simulator())

    yield

    # ── Shutdown ──────────────────────────────────────────────────────────
    simulator_task.cancel()
    try:
        await simulator_task
    except asyncio.CancelledError:
        print("[STOP] Bus simulator stopped.")


app = FastAPI(
    title="Chennai Transit API",
    description="Public bus transit backend for Chennai with dynamic simulation",
    version="1.0.0",
    lifespan=lifespan,
)

# ── CORS ──────────────────────────────────────────────────────────────────────
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# ── Routers ───────────────────────────────────────────────────────────────────
app.include_router(stops.router)
app.include_router(buses.router)
app.include_router(users.router)
app.include_router(trips.router)
app.include_router(simulate.router)
app.include_router(alerts.router)
app.include_router(auth.router)
app.include_router(station.router)

# ── Frontend static files ─────────────────────────────────────────────────────
FRONTEND_DIR = Path(__file__).resolve().parent.parent / "frontend"
app.mount("/app", StaticFiles(directory=str(FRONTEND_DIR), html=True), name="frontend")


@app.get("/", tags=["Health"])
def health_check():
    return RedirectResponse(url="/app/login.html")

