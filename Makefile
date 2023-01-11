ifndef PORT
PORT=/dev/ttyUSB0
endif
BOARD=esp32:esp32:nodemcu-32s

ifeq ("$(wildcard network_conn.h)","")
  $(error network_conn.h does not exist.  Please read the README)
endif

ifeq ("$(wildcard mac_to_name.h)","")
  $(error mac_to_name.h does not exist.  Please read the README)
endif

SRC = $(wildcard *.ino *.h)

PROJECT = $(notdir $(CURDIR))

TARGET = $(PROJECT).ino.bin

$(TARGET): $(SRC)
	@rm -rf tmp
	@mkdir -p tmp
	@TMPDIR=$(PWD)/tmp arduino-cli compile --fqbn=$(BOARD) --output-dir=$(PWD) --build-property build.partitions=min_spiffs --build-property upload.maximum_size=1966080
	@rm -rf tmp

recompile: $(TARGET)

netupload: $(TARGET)
ifdef host
	curl -F "image=@$(TARGET)" ${host}:8266/update
else
	@echo Need host=target to be set - eg make $@ host=testesp
endif

upload:
	@mkdir -p tmp
	@TMPDIR=$(PWD)/tmp arduino-cli upload --fqbn=$(BOARD) -p $(PORT) --input-dir=$(PWD)
	@rm -rf tmp

serial:
	@kermit -l $(PORT) -b 115200 -c

clean:
	rm -rf *.elf tmp *.bin *.map debug.cfg debug_custom.json esp32.svd

reset:
	~/.local/bin/esptool.py --port $(PORT) chip_id
