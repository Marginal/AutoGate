@if exist AutoGate.zip del AutoGate.zip
@if exist AutoGate_dev.zip del AutoGate_dev.zip

zip -9 -j AutoGate.zip plugins/*.txt plugins/*.xpl 
zip -9 AutoGate_dev.zip AutoGate.html "Safedock Pilot Instructions.html" imgs/*.png objs/AutoGate*.blend objs/*.obj objs/*.png
