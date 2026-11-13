# ECSE3038 Mock Test 2 — Cold-Room Monitor

A simulated ESP32 reads a DHT22 every 10 seconds and sends the temperature to a FastAPI server. The server stores readings in MongoDB Atlas and publishes the alarm limit over MQTT. The LED turns on when the temperature is above the limit.

## Run the API on the course VM

1. From the repository root, run `cd api`.
2. Run `python3 -m venv venv`, then `source venv/bin/activate`.
3. Run `python -m pip install -r requirements.txt`.
4. Create `api/.env` with `MONGODB_URI` set to your Atlas connection string and `MQTT_NAME=alliyah`.
5. Run `sudo systemctl stop iot-api` to free port 8000.
6. Run `uvicorn app:app --host 127.0.0.1 --port 8000`. Open `/docs` at the VM's public address.
7. After testing, press Ctrl+C and run `sudo systemctl start iot-api` to restore the tutorial service.

## Run the board

1. Open `board` in VS Code with PlatformIO and Wokwi installed.
2. Create `board/include/secrets.h` containing `#define DEVICE_KEY ""` for the first run.
3. Check that `API_URL` in `src/main.cpp` points to your running mock API's `/readings` endpoint.
4. Build with PlatformIO and start Wokwi. Copy the board MAC from the serial output.
5. For a new board, register its MAC and room through `POST /devices` in `/docs`. Put the returned key between the quotes in `secrets.h`. For an already registered board, use its saved key.
6. Rebuild and restart Wokwi. A successful reading prints `POST /readings: 201`.
7. Send `{"max_temp": 8}` to `PUT /alarm/{mac}` using the board MAC. Raise the simulated temperature above 8°C to turn on the LED. Subscribe to `ecse3038/alliyah/#` on the class broker to see the alarm limit, LED state, and board status.