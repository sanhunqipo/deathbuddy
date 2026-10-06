CC := gcc
RM := rm -rf
CFLAGS := -O2 -Wall
LDFLAGS := `pkg-config --libs --cflags cairo x11` -lXi

all: clean build

build: clean
	@$(CC) deathbuddy.c $(CFLAGS) -o deathbuddy $(LDFLAGS)

clean:
	@$(RM) deathbuddy
