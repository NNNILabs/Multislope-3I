call "environment.bat"
if not exist "build" ( 
    mkdir "build"
)
cd build
cmake -G "NMake Makefiles" ..
nmake