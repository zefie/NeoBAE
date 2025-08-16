BAE_BUILT_IN_PATCHES	:= 1
BAE_API			:= SDL2
DEBUG			:= 0
include inc/Makefile.common

ARCH		:= -m32 -static
CC		:= gcc
CXX		:= g++
LD		:= $(CC)
AR      	:= ar
STRIP		:= strip

OPTI            := -O2 -fPIC
CFLAGS  	:= $(ARCH) $(OPTI) $(INC_PATH) -D_THREAD_SAFE -Wno-unused-value

ifneq ($(BAE_FLAGS),)
	CFLAGS	+= $(BAE_FLAGS)
endif

include inc/Makefile.versioning
CFLAGS		+= -D_VERSION=\""$(VERSION)"\"

CXXFLAGS 	:= $(CFLAGS)
LDFLAGS		:= $(ARCH) $(OPTI) -s

LIBS	= 	-lpthread -lm -ldl

# ------------------------------------------------------------
# Vendored SDL2 static build support
# Usage:
#   1) Build SDL2 yourself (static) for 32-bit:
#        ./configure --disable-shared --enable-static --prefix=$PWD/build-linux32 CFLAGS="-m32" LDFLAGS="-m32"
#        make && make install
#      (Resulting tree has include/ and lib/ under build-linux32)
#   2) Invoke make with VENDORED_SDL2 pointing at that prefix:
#        make BAE_API=SDL2 VENDORED_SDL2=../SDL2-2.32.8/build-linux32
#   3) This block adds the proper -I and static lib paths and removes system SDL2.
# If VENDORED_SDL2 is not set, you must provide system SDL2 via pkg-config or
# manually append -I and -lSDL2 yourself before this include.
# ------------------------------------------------------------
UNAME_S:=$(shell uname -s 2>/dev/null)
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

# Rules for compiling source files to object files
$(OBJ_DIR)%.o : %.cpp
	@echo @compile $<
	@mkdir -p $(OBJ_DIR)
	@${CXX} -c ${CXXFLAGS} $< -o $@

$(OBJ_DIR)%.o : %.c
	@echo @compile $<
	@mkdir -p $(OBJ_DIR)
	@${CC} -c ${CFLAGS} $< -o $@

clean:
	@rm -rdf $(TARGET_OUT)
	@rm -rdf $(OBJ_DIR)
	@rm -rdf $(BUILD_DIR)
	@rm -rdf $(TEST_OUT_DIR)
	@echo Cleaned!

include inc/Makefile.tests
