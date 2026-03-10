TOOLCHAIN_PREFIX ?=
CC = $(TOOLCHAIN_PREFIX)gcc
CXX = $(TOOLCHAIN_PREFIX)g++
CXXFLAGS = -g -Wall -DU8G2_WITH_EXTERNAL_FONT
CFLAGS = -g -Wall -DU8G2_WITH_EXTERNAL_FONT
LIBSDL = -lSDL2main -lSDL2
LDFLAGS = -pthread
# 分别处理 C 和 C++ 源文件
C_SRC = $(shell ls ./u8g2/*.c) 
CPP_SRC = main.cpp parser/parser.cpp
C_OBJ = $(C_SRC:.c=.o)
CPP_OBJ = $(CPP_SRC:.cpp=.o)
OBJ = $(C_OBJ) $(CPP_OBJ)

DEVICE_TYPE ?= sdl2
ifeq ($(DEVICE_TYPE),sdl2)
	C_SRC +=  $(shell ls ./u8g2/port/sdl/*.c )
	CXXFLAGS += -DSDL2
	LDFLAGS += $(LIBSDL)
else ifeq ($(DEVICE_TYPE),luckfox)
	C_SRC +=  $(shell ls ./u8g2/port/linux/*.c )
	CXXFLAGS += -DLUCKFOX
endif
# 使用 C++ 编译器链接
u8g2_sdl: $(OBJ) 
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(OBJ) -o u8g2_sdl $(LDFLAGS)

# 分别定义编译规则
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:	
	-rm $(OBJ) u8g2_sdl