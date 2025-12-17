#!/bin/bash
# Run the collector using the venv Python

cd "$(dirname "$0")"
source python-venv/bin/activate
python3 collector.py


