@echo off
echo ========================================
echo AVATTO-GW90-Ti UI Debug Server
echo ========================================
echo.
echo Starting HTTP server on port 8000...
echo.
echo Open your browser and visit:
echo http://localhost:8000/debug-ui/
echo.
echo Press Ctrl+C to stop the server
echo ========================================
echo.

cd ..
python -m http.server 8000
