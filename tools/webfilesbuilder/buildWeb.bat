echo off
:build
node gulp.js
Title "AVATTO-GW90-Ti webfiles builder"
echo Press any key for build again or close window
pause
goto build
