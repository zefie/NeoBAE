ifeq ($(USE_SDL3),1)
	BAE_API			:= SDL3
else
	BAE_API			:= SDL2
endif

include inc/Makefile.common

ARCH		:= -static
CC		:= gcc
CXX		:= g++
LD		:= $(CC)
AR      	:= ar
STRIP		:= strip

OPTI            := -O2 -fPIC

ifeq ($(DEBUG),1)
    OPTI := -g -O0 -ggdb3 -fPIC
endif

CFLAGS  	:= $(ARCH) $(OPTI) $(INC_PATH) -D_THREAD_SAFE -Wno-unused-value

ifneq ($(BAE_FLAGS),)
	CFLAGS	+= $(BAE_FLAGS)
endif

include inc/Makefile.versioning
CFLAGS		+= -D_VERSION=\""$(VERSION)"\"


LDFLAGS		:= $(ARCH) $(OPTI)
ifneq ($(DEBUG),1)
	LDFLAGS += -s
endif

LIBS	= 	-lpthread -lm -ldl


UNAME_S:=$(shell uname -s 2>/dev/null)
ifeq ($(USE_SDL3),1)
# System SDL for Linux (dynamic) if pkg-config available
ifneq (,$(findstring Linux,$(UNAME_S)))
	SDL3_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null)
	SDL3_LIBS   := $(shell pkg-config --libs sdl3 2>/dev/null)
	ifneq ($(SDL3_LIBS),)
		CFLAGS += $(SDL3_CFLAGS)
		# Prepend pkg-config libs; they already have -lSDL3 and any needed deps
		LIBS := $(SDL3_LIBS) $(filter-out -lSDL3,$(LIBS))
		# If you kept -static in ARCH but want dynamic SDL, strip it here:
		ifneq (,$(findstring -static,$(ARCH)))
			ARCH := $(filter-out -static,$(ARCH))
			LDFLAGS := $(filter-out -static,$(LDFLAGS))
		endif
	else
		# Fallback minimal guess
		CFLAGS += -I/usr/include/SDL2
		LIBS := -lSDL3 $(LIBS)
	endif
else
	# Non-Linux (e.g., BSD) fallback
	SDL3_CFLAGS := $(shell pkg-config --cflags sdl3 2>/dev/null)
	SDL3_LIBS   := $(shell pkg-config --libs sdl3 2>/dev/null)
	ifneq ($(SDL3_LIBS),)
		CFLAGS += $(SDL3_CFLAGS)
		LIBS := $(SDL3_LIBS) $(LIBS)
	else
		CFLAGS += -I/usr/include/SDL3
		LIBS := -lSDL3 $(LIBS)
	endif
endif
else
# System SDL for Linux (dynamic) if pkg-config available
ifneq (,$(findstring Linux,$(UNAME_S)))
	SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
	SDL2_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)
	ifneq ($(SDL2_LIBS),)
		CFLAGS += $(SDL2_CFLAGS)
		# Prepend pkg-config libs; they already have -lSDL2 and any needed deps
		LIBS := $(SDL2_LIBS) $(filter-out -lSDL2,$(LIBS))
		# If you kept -static in ARCH but want dynamic SDL, strip it here:
		ifneq (,$(findstring -static,$(ARCH)))
			ARCH := $(filter-out -static,$(ARCH))
			LDFLAGS := $(filter-out -static,$(LDFLAGS))
		endif
	else
		# Fallback minimal guess
		CFLAGS += -I/usr/include/SDL2
		LIBS := -lSDL2 $(LIBS)
	endif
else
	# Non-Linux (e.g., BSD) fallback
	SDL2_CFLAGS := $(shell pkg-config --cflags sdl2 2>/dev/null)
	SDL2_LIBS   := $(shell pkg-config --libs sdl2 2>/dev/null)
	ifneq ($(SDL2_LIBS),)
		CFLAGS += $(SDL2_CFLAGS)
		LIBS := $(SDL2_LIBS) $(LIBS)
	else
		CFLAGS += -I/usr/include/SDL2
		LIBS := -lSDL2 $(LIBS)
	endif
endif
endif

CXXFLAGS 	:= $(CFLAGS)
# Use C++ linker if we have any C++ sources
ifneq ($(filter %.cpp,$(SRC) $(SRC_BIN)),)
    LD := $(CXX)
    LIBS += -lstdc++ -lsupc++
endif

all: $(TARGET_LIB).a ${TARGET_LIB}.so $(TARGET_BIN)

$(TARGET_LIB).a: $(OBJ)
	@mkdir -p $(TARGET_OUT)
	$(AR) rcs $(TARGET_OUT)$(TARGET_LIB).a $(OBJ)

$(TARGET_LIB).so: ${OBJ}
	@mkdir -p $(TARGET_OUT)
	${LD} -shared $(LDFLAGS) ${OBJ} $(LIBS) -o $(TARGET_OUT)${TARGET_LIB}.so

$(TARGET_BIN): ${OBJ_BIN}
	@mkdir -p $(TARGET_OUT)
	${LD} -o $(TARGET_OUT)${TARGET_BIN} ${LDFLAGS} ${OBJ_BIN} ${LIBS}

pack: ${TARGET_BIN}
	cat $(TARGET_OUT)${TARGET_BIN} | gzip -9c > $(TARGET_OUT)playbae_linux32_static.gz


# Generate rules for all unique source files
ALL_SOURCES := $(sort $(SRC) $(SRC_BIN))
$(foreach src,$(ALL_SOURCES),$(eval $(call make_obj_rule,$(src))))

clean:
	@rm -rf $(TARGET_OUT)
	@rm -rf $(OBJ_DIR)
	@rm -rf $(BUILD_DIR)
	@rm -rf $(TEST_OUT_DIR)
	@echo Cleaned!

include inc/Makefile.tests
