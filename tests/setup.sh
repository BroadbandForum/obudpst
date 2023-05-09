#!/bin/bash

python3 -m venv ./

source ./bin/activate

pip install -r requirements.txt

docker compose build
