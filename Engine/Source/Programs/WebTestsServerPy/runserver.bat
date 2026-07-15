@echo off

call createenv.bat

python -m daphne -e tcp:port=8000:interface=0.0.0.0 -e tcp:port=8001:interface=0.0.0.0 webtests.asgi:application
