TARGET = OneFileGBEMU

all: run

$(TARGET): main.c
	gcc -Isrc/ -Isrc/Include -Lsrc/lib -o OneFileGBEMU main.c apu.c -lmingw32 -lSDL2main -lSDL2

run: $(TARGET)
	./$(TARGET)


