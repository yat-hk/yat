ROOT := ../..
ENGINE := $(ROOT)/engine
CXX ?= c++
CC ?= cc
# -MMD -MP: without header dependencies, editing yat/engine.h rebuilds only the
# .cpp files you touched and silently links them against translation units that
# still hold the OLD sizeof(Engine) — an ODR violation that crashes or, worse,
# renders quietly wrong goldens.
CXXFLAGS := -std=c++17 -O2 -Wall -MMD -MP -I$(ENGINE)/include -I$(ENGINE)/third_party -I$(ENGINE)/third_party/qrcodegen -I.
CFLAGS := -std=c11 -O2 -Wall -MMD -MP -I$(ENGINE)/third_party/qrcodegen

SRCS := main.cpp $(ENGINE)/src/path.cpp $(ENGINE)/src/text.cpp $(ENGINE)/src/extract.cpp $(ENGINE)/src/render.cpp $(ENGINE)/src/rss.cpp $(ENGINE)/src/csv.cpp $(ENGINE)/src/icons.cpp $(ENGINE)/src/fonts.cpp $(ENGINE)/src/fonts_data_16.cpp $(ENGINE)/src/fonts_data_24.cpp $(ENGINE)/src/fonts_data_32.cpp $(ENGINE)/src/fonts_data_48.cpp $(ENGINE)/src/fonts_data_96.cpp $(ENGINE)/src/fonts_data_128.cpp
C_SRCS := $(ENGINE)/third_party/qrcodegen/qrcodegen.c
OBJS := $(SRCS:.cpp=.o) $(C_SRCS:.c=.o)
DEPS := $(OBJS:.o=.d)

yat-preview: $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

-include $(DEPS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Fleet audit: every pack rendered with its fixtures into one directory, so an
# engine/font/chrome change can be eyeballed across the whole fleet at once
# (the goldens in run-tests.sh only pin a handful of pages). Pass through extra
# flags with ARGS, e.g. `make render-all ARGS=--strict` to fail on any render
# warning, or `ARGS="--only hko"` for one pack family.
render-all: yat-preview
	python3 render_all.py $(ARGS)

clean:
	rm -f yat-preview $(OBJS) $(DEPS)
	rm -rf out

.PHONY: clean render-all
