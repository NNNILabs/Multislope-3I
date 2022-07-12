cd ./build/
make
cd ..
name=$(find ./build/ -name "*.uf2")
echo "found: $name"
RPI="/media/$USER/RPI-RP2/"
echo "waiting for RPI..."
timeout=0
while [ ! -d "$RPI" ]; do
    sleep 1
    timeout=$((timeout+1))
    if [ $timeout -gt 10 ]; then
        echo "RPI not found"
        exit 1
    fi
done
echo "found $RPI"
cp $name /media/$USER/RPI-RP2/
