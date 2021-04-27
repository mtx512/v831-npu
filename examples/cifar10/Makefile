PRJ_ROOT_DIR := $(shell pwd)

TARGET_NAME := nna_cifar10

SRC_HW := $(PRJ_ROOT_DIR)/../../hw
SRC_UTILS := $(PRJ_ROOT_DIR)/../../utils

#Set Architecture
ARCH := arm

# Build with Allwinner SDK gcc 5.2.1 as MAIX-II kenel/OS is built with that
#CC := arm-linux-gnueabihf-gcc
CC := arm-openwrt-linux-gcc

DEPFILES +=

OBJFILES +=

TARGET = $(TARGET_NAME)

CC += -fPIC -mcpu=cortex-a7 -mtune=cortex-a7 -Wall -Wno-unused-but-set-variable -fpermissive

SOURCES := $(wildcard $(SRC_HW)/*.cpp $(SRC_UTILS)/*.cpp)
OBJFILES := $(patsubst %.cpp,%.o, $(SOURCES))


#Macros
DEFINES +=

#Include directories
INCLUDES_SRC += -I$(PRJ_ROOT_DIR)/include -I$(SRC_HW)/include -I$(SRC_UTILS)/include

#Libraries
LIBRARIES +=

#Object files
OBJFILES += $(patsubst %.cpp,%.o,$(wildcard *.cpp)) $(patsubst %.cpp,%.o,$(wildcard src/*.cpp))

#Dpendency files
DEPFILES += $(patsubst %.cpp,%.d,$(wildcard *.cpp)) $(patsubst %.cpp,%.d,$(wildcard src/*.cpp))


#Compile object files
%.o: %.cpp
	$(CC) $(DEBUGFLAGS_C++-Compiler) $(INCLUDES_SRC) $(DEFINES) -c  -o $@ $<

all: $(OBJFILES)
	$(CC) -o $(TARGET) $(OBJFILES) $(LIBRARIES)

#Clean files
clean:
	rm -f $(OBJFILES) rm -f $(DEPFILES) rm -f $(TARGET) rm -rf $(TARGET_VER)
