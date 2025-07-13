TARGET = OneFileGBEMU

all: run

$(TARGET): insp.c
	gcc -Isrc/ -Isrc/Include -Lsrc/lib -o OneFileGBEMU insp.c apu.c -lmingw32 -lSDL2main -lSDL2

run: $(TARGET)
	./$(TARGET)


