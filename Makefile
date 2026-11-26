# SPDX-FileCopyrightText: 2026 RiftWii contributors
# SPDX-License-Identifier: GPL-3.0-or-later

include $(DEVKITPPC)/wii_rules

PYTHON ?= python3
CORE := build/core
ASSETS := build/assets
OUT := $(if $(filter 1,$(TEST)),build/test,build)
APP := $(OUT)/apps/rift-store
BEARSSL := $(CORE)/build-bearssl/libbearssl.a
CORE_C := $(CORE)/wii/online/tlsroots.c
CORE_CPP := $(addprefix $(CORE)/,wii/core/nanduse.cpp wii/online/entropy.cpp wii/online/tls.cpp \
	wii/online/httpmem.cpp wii/online/netsock.cpp wii/online/netconfig.cpp src/net/appcatalog.cpp \
	src/net/http.cpp src/net/dnsquery.cpp src/data/sha256.cpp src/data/textlines.cpp src/fs/zipread.cpp \
	src/art/pngdecode.cpp src/art/themepack.cpp src/art/qrcode.cpp)
SOURCES := source/main.c source/draw.c source/sound.c source/system_exit.c source/dolboot.c \
	source/remote_queue.c source/dolboot_trampoline.S source/live_backend.cpp source/store_data.cpp \
	source/catalogue_parser.cpp $(CORE_C) $(CORE_CPP)
OBJECTS := $(foreach file,$(SOURCES),$(OUT)/obj/$(file).o)
FLAGS := $(MACHDEP) -O2 -Wall -Wextra -Werror -ffunction-sections -fdata-sections \
	-I$(LIBOGC_INC) -I$(DEVKITPRO)/portlibs/ppc/include -I$(ASSETS) -Isource \
	-I$(CORE)/include -I$(CORE)/wii/core -I$(CORE)/wii/online -I$(CORE)/vendor-bearssl/inc \
	-DRIFTWII_VERSION=\"2611-252\" $(if $(filter 1,$(TEST)),-DRIFT_STORE_TEST)
LINK := -Wl,--wrap=wiiuse_init_cmd_queue,--gc-sections
LIBS := $(BEARSSL) -L$(LIBOGC_LIB) -L$(DEVKITPRO)/portlibs/ppc/lib \
	-lz -lfat -lwiiuse -lbte $(LIBOGC_LIB)/libmad.a -lasnd -logc -lm

.PHONY: all clean memory
all: $(APP)/boot.dol $(APP)/meta.xml $(APP)/icon.png $(APP)/LICENSE-ASND.txt

memory: $(OUT)/rift-store.elf
	$(DEVKITPPC)/bin/powerpc-eabi-size $<

$(CORE)/revision: tools/fetch_core.py
	$(PYTHON) tools/fetch_core.py $(CORE)

$(BEARSSL): $(CORE)/revision
	$(MAKE) --no-print-directory -C $(CORE) -f Makefile.bearssl

$(CORE_C) $(CORE_CPP): $(CORE)/revision
	@test -f $@

$(ASSETS)/store_assets.h $(ASSETS)/icon.png &: tools/make_assets.py tools/brand_art.py tools/category_art.py tools/pointer_art.py assets/glyphs.txt assets/mark.png assets/rounded.ttf
	$(PYTHON) tools/make_assets.py $(ASSETS)

$(ASSETS)/sound_assets.h: tools/make_sounds.py
	@mkdir -p $(ASSETS)
	$(PYTHON) tools/make_sounds.py $@

$(OUT)/obj/%.c.o: %.c $(CORE)/revision $(ASSETS)/store_assets.h $(ASSETS)/sound_assets.h Makefile
	@mkdir -p $(@D)
	$(CC) $(FLAGS) -std=gnu11 -MMD -MP -c $< -o $@

$(OUT)/obj/%.cpp.o: %.cpp $(CORE)/revision Makefile
	@mkdir -p $(@D)
	$(CXX) $(FLAGS) -std=c++17 -fno-exceptions -fno-rtti -MMD -MP -c $< -o $@

$(OUT)/obj/%.S.o: %.S Makefile
	@mkdir -p $(@D)
	$(CC) $(MACHDEP) -c $< -o $@

$(OUT)/rift-store.elf: $(OBJECTS) $(BEARSSL) Makefile
	$(CXX) $(MACHDEP) $(LINK) $(OBJECTS) $(LIBS) -o $@

$(APP)/boot.dol: $(OUT)/rift-store.elf
	@mkdir -p $(APP)
	elf2dol $< $@

$(APP)/meta.xml: meta.xml
	@mkdir -p $(APP)
	cp $< $@

$(APP)/LICENSE-ASND.txt: LICENSE-ASND.txt
	@mkdir -p $(APP)
	cp $< $@

$(APP)/icon.png: $(ASSETS)/icon.png
	@mkdir -p $(APP)
	cp $< $@

clean:
	rm -f $(OBJECTS) $(OBJECTS:.o=.d) $(OUT)/rift-store.elf $(APP)/boot.dol $(APP)/meta.xml $(APP)/icon.png $(APP)/LICENSE-ASND.txt

-include $(OBJECTS:.o=.d)
