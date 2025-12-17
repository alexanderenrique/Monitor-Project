# ═══════════════════════════════════════════════════════════════════════════════
#  NEMO Sensor Collector
#  Flask service that receives ESP32 sensor data and forwards it to the NEMO API.
#  Architecture: ESP32 → Collector (this script) → NEMO API (with Token Auth)
# ═══════════════════════════════════════════════════════════════════════════════

from flask import Flask, request, jsonify
from dotenv import load_dotenv
import os
import requests
from datetime import datetime

# ───────────────────────────────────────────────────────────────────────────────
# Flask App Setup
# ───────────────────────────────────────────────────────────────────────────────
app = Flask(__name__)

# Load environment variables from .env file
# (.env should be in the same directory as this file)
load_dotenv()

# Read NEMO credentials and API endpoint from environment
TOKEN = os.getenv("NEMO_TOKEN")
NEMO_URL = os.getenv("NEMO_URL")

# Validate environment configuration
if not TOKEN or not NEMO_URL:
    raise ValueError("❌ Missing NEMO_TOKEN or NEMO_URL in .env file")

# Prepare HTTP headers with authentication token
HEADERS = {"Authorization": f"Token {TOKEN}"}

# ───────────────────────────────────────────────────────────────────────────────
# Route: Receive data from ESP32 and forward to NEMO
# ───────────────────────────────────────────────────────────────────────────────
@app.route("/esp/receive_send", methods=["POST"])
def receive_send():
    """
    Receive JSON payload from ESP32 sensors and forward it to NEMO.
    The collector adds the timestamp when it receives the data.
    Expected JSON format from ESP32:
    {
        "value": 22.5,
        "sensor": 27
    }
    """
    try:
        data = request.get_json()
    except Exception:
        return jsonify({"error": "Invalid or missing JSON"}), 400

    # Extract fields (ESP32 doesn't send timestamp)
    sensor_value = data.get("value")
    sensor_id = data.get("sensor")

    # Validate required fields (only value and sensor are required from ESP32)
    if sensor_value is None or sensor_id is None:
        return jsonify({"error": "Missing required fields: 'value' and 'sensor' are required"}), 400

    # Generate timestamp when collector receives the data
    created_date = datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")
    
    print(f"📦 Received packet → Timestamp: {created_date}, Value: {sensor_value}, Sensor ID: {sensor_id}")

    # Construct payload for NEMO (with timestamp added by collector)
    nemo_payload = {
        "created_date": created_date,
        "value": sensor_value,
        "sensor": sensor_id
    }

    try:
        # Forward packet to NEMO API
        response = requests.post(NEMO_URL, json=nemo_payload, headers=HEADERS, timeout=10)
        if response.status_code == 200:
            print("✅ Packet successfully sent to NEMO")
            return jsonify({"message": "Data packet successfully sent to NEMO"}), 200
        else:
            print(f"⚠️  NEMO API returned {response.status_code}")
            return jsonify({"error": f"NEMO responded with {response.status_code}"}), response.status_code
    except requests.exceptions.RequestException as e:
        print(f"❌ Error sending to NEMO: {e}")
        return jsonify({"error": "Failed to reach NEMO API"}), 502

# ───────────────────────────────────────────────────────────────────────────────
# Main Entry Point
# ───────────────────────────────────────────────────────────────────────────────
if __name__ == "__main__":
    # Run the collector on all interfaces (port 8000)
    print("🚀 Starting NEMO Sensor Collector on port 8000...")
    app.run(host="0.0.0.0", port=8000)
