PREFIX = /usr

prepare:
	mkdir -p build

build: prepare detetor recorder

detetor: prepare
	@echo "Building the detetor..."
	gcc detetor.c -o build/detetor \
    -I/usr/include/x11 \
    -I/usr/include/ffmpeg \
    -lX11 \
    -lavcodec -lavformat -lavutil -lswscale
	@echo "Build the detetor successfully"

recorder: prepare
	@echo "Building the recorder..."
	gcc recorder.c -o build/recorder \
    -I/usr/include/x11 \
    -I/usr/include/ffmpeg \
    -lX11 \
    -lavcodec -lavformat -lavutil -lswscale
	@echo "Build the recorder successfully"

install: build
	@echo "Installing the executables..."
	mkdir -pv ${DESTDIR}${PREFIX}/bin
	cp build/detetor ${DESTDIR}${PREFIX}/bin/
	cp build/recorder ${DESTDIR}${PREFIX}/bin/
	@echo "Installation completed successfully."

all:
	@echo "Building the project..."
	$(MAKE) prepare
	$(MAKE) build
	@echo "Build completed successfully."

clean:
	rm -rf build/*

.PHONY: prepare build detetor recorder all clean