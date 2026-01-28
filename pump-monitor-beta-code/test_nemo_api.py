#!/usr/bin/env python3
# ═══════════════════════════════════════════════════════════════════════════════
#  Direct NEMO API Test Script
#  Tests if NEMO API assigns timestamps automatically when created_date is missing
#  This script bypasses the collector and sends directly to NEMO API
# ═══════════════════════════════════════════════════════════════════════════════

from dotenv import load_dotenv
import os
import requests
import json
from datetime import datetime

# Load environment variables
load_dotenv()

# Get NEMO credentials from .env
TOKEN = os.getenv("NEMO_TOKEN")
NEMO_URL = os.getenv("NEMO_URL")

if not TOKEN or not NEMO_URL:
    print("❌ Error: Missing NEMO_TOKEN or NEMO_URL in .env file")
    print("   Create a .env file with:")
    print("   NEMO_TOKEN=your_token_here")
    print("   NEMO_URL=https://your-nemo-api.com/api/endpoint")
    exit(1)

# Prepare headers
HEADERS = {"Authorization": f"Token {TOKEN}"}

def test_with_timestamp():
    """Test 1: Send data WITH timestamp"""
    print("\n" + "="*70)
    print("TEST 1: Sending data WITH timestamp")
    print("="*70)
    
    payload = {
        "created_date": datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ"),
        "value": 22.5,
        "sensor": 27
    }
    
    print(f"📤 Sending to: {NEMO_URL}")
    print(f"📤 Payload: {json.dumps(payload, indent=2)}")
    
    try:
        response = requests.post(NEMO_URL, json=payload, headers=HEADERS, timeout=10)
        print(f"\n📥 Response Status: {response.status_code}")
        print(f"📥 Response Body: {response.text}")
        
        if response.status_code == 200 or response.status_code == 201:
            print("✅ SUCCESS: Data accepted by API")
            return True
        else:
            print(f"⚠️  API returned status {response.status_code}")
            return False
    except requests.exceptions.RequestException as e:
        print(f"❌ Error: {e}")
        return False

def test_without_timestamp():
    """Test 2: Send data WITHOUT timestamp (testing if API assigns it)"""
    print("\n" + "="*70)
    print("TEST 2: Sending data WITHOUT timestamp")
    print("="*70)
    
    payload = {
        "value": 33.7,
        "sensor": 27
        # Note: created_date is intentionally missing
    }
    
    print(f"📤 Sending to: {NEMO_URL}")
    print(f"📤 Payload: {json.dumps(payload, indent=2)}")
    print("   ⚠️  created_date is intentionally omitted")
    
    try:
        response = requests.post(NEMO_URL, json=payload, headers=HEADERS, timeout=10)
        print(f"\n📥 Response Status: {response.status_code}")
        print(f"📥 Response Body: {response.text}")
        
        if response.status_code == 200 or response.status_code == 201:
            print("✅ SUCCESS: Data accepted by API (timestamp likely assigned automatically)")
            return True
        else:
            print(f"⚠️  API returned status {response.status_code}")
            if response.status_code == 400:
                print("   This suggests the API requires created_date")
            return False
    except requests.exceptions.RequestException as e:
        print(f"❌ Error: {e}")
        return False

def test_with_empty_string_timestamp():
    """Test 3: Send data with empty string timestamp"""
    print("\n" + "="*70)
    print("TEST 3: Sending data with empty string timestamp")
    print("="*70)
    
    payload = {
        "created_date": "",
        "value": 44.9,
        "sensor": 27
    }
    
    print(f"📤 Sending to: {NEMO_URL}")
    print(f"📤 Payload: {json.dumps(payload, indent=2)}")
    
    try:
        response = requests.post(NEMO_URL, json=payload, headers=HEADERS, timeout=10)
        print(f"\n📥 Response Status: {response.status_code}")
        print(f"📥 Response Body: {response.text}")
        
        if response.status_code == 200 or response.status_code == 201:
            print("✅ SUCCESS: Data accepted by API")
            return True
        else:
            print(f"⚠️  API returned status {response.status_code}")
            return False
    except requests.exceptions.RequestException as e:
        print(f"❌ Error: {e}")
        return False

if __name__ == "__main__":
    print("\n" + "="*70)
    print("🧪 Direct NEMO API Test Script")
    print("="*70)
    print(f"📍 API URL: {NEMO_URL}")
    print(f"🔑 Token: {TOKEN[:10]}..." if len(TOKEN) > 10 else f"🔑 Token: {TOKEN}")
    print("\nTesting timestamp assignment behavior...\n")
    
    # Run tests
    test1_result = test_with_timestamp()
    test2_result = test_without_timestamp()
    test3_result = test_with_empty_string_timestamp()
    
    # Summary
    print("\n" + "="*70)
    print("TEST SUMMARY")
    print("="*70)
    print(f"Test 1 (with timestamp):         {'✅ PASS' if test1_result else '❌ FAIL'}")
    print(f"Test 2 (without timestamp):      {'✅ PASS' if test2_result else '❌ FAIL'}")
    print(f"Test 3 (empty string timestamp): {'✅ PASS' if test3_result else '❌ FAIL'}")
    print("\n💡 Interpretation:")
    if test2_result:
        print("   ✅ The API accepts data without timestamps!")
        print("   ✅ Timestamps are likely assigned automatically by the API")
        print("   ✅ Your ESP32 does NOT need to send timestamps")
    else:
        print("   ⚠️  The API requires timestamps")
        print("   ⚠️  Your ESP32 will need to send timestamps")
    print("\n📝 Check your NEMO API dashboard/logs to see what timestamps were assigned")

