# LandScreen Ubuntu run notes

This project is a Qt Widgets ground station. It talks to the onboard side through TCP. Each message is one JSON line. It can build with Qt6 or Qt5.

## 1. Install dependencies

On Ubuntu 20.04, use ROS Noetic and the system Qt5 packages:

```bash
sudo apt update
sudo apt install build-essential cmake qtbase5-dev qtbase5-dev-tools python3
```

On Ubuntu 22.04/24.04, you can use Qt6:

```bash
sudo apt update
sudo apt install build-essential cmake qt6-base-dev
```

If you install Qt 6 with the Qt online installer, pass the Qt path to CMake.

## 2. Build

```bash
cd LandScreen-master
mkdir -p build
cd build
cmake ..
cmake --build . -j
```

If Qt is installed in `~/Qt/6.9.0/gcc_64`:

```bash
cmake .. -DCMAKE_PREFIX_PATH=$HOME/Qt/6.9.0/gcc_64
cmake --build . -j
```

## 3. Test with the local fake server

Terminal 1:

```bash
cd LandScreen-master
python3 tools/fake_landscreen_server.py
```

Terminal 2:

```bash
cd LandScreen-master/build
LANDSCREEN_SERVER_IP=127.0.0.1 LANDSCREEN_SERVER_PORT=8001 ./planescreen
```

The fake server listens on `127.0.0.1:8001`. The original source default is still `192.168.10.3:8001`, but you can override it with `LANDSCREEN_SERVER_IP` and `LANDSCREEN_SERVER_PORT`.

## 4. Real onboard protocol

The onboard side should listen on TCP port `8001`, receive ground JSON lines, and send JSON lines back.

Ground station sends:

```json
{"f1x":2,"f1y":3,"f2x":-1,"f2y":-1,"f3x":-1,"f3y":-1,"launch":false}
```

Onboard side sends:

```json
{"planner":[{"x":0.5,"y":0.5},{"x":1.5,"y":1.0}],"tx":1.2,"ty":2.1,"tn":"elephant"}
```
