BUILDTYPE ?= Release

all: test

build/Makefile:
	deps/run_gyp pixelmatch.gyp --depth=. -Goutput_dir=. --generator-output=./build -f make

xcode:
	deps/run_gyp pixelmatch.gyp --depth=. -Goutput_dir=. --generator-output=./build -f xcode
	open build/pixelmatch.xcodeproj

test: build/Makefile
	BUILDTYPE=Debug make -C build test
	build/Debug/test

clean:
	-rm -rf build
