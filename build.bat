@mkdir build
@mkdir build\sfx
@mkdir build\css
@mkdir build\js
@mkdir build\img
@mkdir build\levels

call emcc cpp\lib.cpp -s MODULARIZE=1 -s EXPORTED_RUNTIME_METHODS="['ccall','cwrap']" -s INITIAL_MEMORY=256MB -o js\lib-wasm.js -s USE_PTHREADS=1 -s PTHREAD_POOL_SIZE=4 --std=c++11 -O3 -s EXPORT_NAME=plib

@xcopy /s/e /y css build\css
@xcopy /s/e /y html\* build\
@xcopy /s/e /y img build\img
@xcopy /s/e /y js build\js
@xcopy /s/e /y sfx build\sfx
@xcopy /s/e /y levels build\levels