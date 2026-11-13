import os
import secrets
from datetime import datetime, timezone
from fastapi import Header
from pydantic import Field
from fastapi import Response
import paho.mqtt.publish as publish

from dotenv import load_dotenv
from fastapi import FastAPI, HTTPException
from pydantic import BaseModel
from pymongo import MongoClient
from pymongo.errors import DuplicateKeyError

load_dotenv()

app = FastAPI(title="Cold-Room Monitor")
client = MongoClient(os.environ["MONGODB_URI"])
db = client["ecse3038_mock_test2"]
devices = db["devices"]
devices.create_index("mac", unique=True)


class Device(BaseModel):
    mac: str
    room: str


@app.post("/devices", status_code=201)
def register_device(device: Device):
    key = secrets.token_urlsafe(16)

    try:
        devices.insert_one({
            "mac": device.mac,
            "room": device.room,
            "key": key,
        })
    except DuplicateKeyError:
        raise HTTPException(status_code=409, detail="MAC already registered")

    return {"mac": device.mac, "room": device.room, "key": key}


@app.get("/devices")
def get_devices():
    return list(devices.find({}, {"_id": 0, "mac": 1, "room": 1}))

class Reading(BaseModel):
    mac: str
    temp: float = Field(ge=-40, le=80)


@app.post("/readings", status_code=201)
def create_reading(reading: Reading, x_api_key: str | None = Header(default=None)):
    device = devices.find_one({"key": x_api_key}) if x_api_key else None

    if device is None:
        raise HTTPException(status_code=401, detail="Missing or invalid API key")

    if device["mac"] != reading.mac:
        raise HTTPException(status_code=403, detail="Key belongs to another MAC")

    timestamp = datetime.now(timezone.utc)
    db["readings"].insert_one({
        "mac": reading.mac,
        "temp": reading.temp,
        "time": timestamp,
    })

    return {"mac": reading.mac, "temp": reading.temp, "time": timestamp}
@app.get("/readings")
def get_readings(mac: str | None = None, since: datetime | None = None):
    query = {}

    if mac is not None:
        query["mac"] = mac

    if since is not None:
        query["time"] = {"$gte": since}

    return list(db["readings"].find(query, {"_id": 0}).sort("time", -1))
class AlarmLimit(BaseModel):
    max_temp: float = Field(allow_inf_nan=False)


@app.put("/alarm/{mac}")
def set_alarm(mac: str, alarm: AlarmLimit, response: Response):
    if devices.find_one({"mac": mac}) is None:
        raise HTTPException(status_code=404, detail="Unknown MAC")

    result = db["alarms"].update_one(
        {"_id": mac},
        {"$set": {"max_temp": alarm.max_temp}},
        upsert=True,
    )

    topic = f"ecse3038/{os.environ['MQTT_NAME']}/alarm/max"
    publish.single(
        topic,
        payload=str(alarm.max_temp),
        hostname="135.232.203.144",
        port=1883,
        qos=1,
        retain=True,
    )

    response.status_code = 201 if result.upserted_id is not None else 200
    return {"mac": mac, "max_temp": alarm.max_temp}