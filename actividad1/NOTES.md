# Add Arduino as component

mkdir -p components
cd components
git clone https://github.com/espressif/arduino-esp32.git arduino
cd arduino
git submodule update --init --recursive
cd ../..