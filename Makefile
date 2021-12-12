INCLUDE := include
SOURCE  := source
BUILD   := build
DEBUG   := build/debug

PROG   = mcd
CSRC   = $(shell find $(SOURCE) -type f -name '*.c')
CXXSRC = $(shell find $(SOURCE) -type f -name '*.cpp')
OBJS   = $(CSRC:$(SOURCE)/%.c=$(BUILD)/%.o) $(CXXSRC:$(SOURCE)/%.cpp=$(BUILD)/%.o)
D_OBJS = $(CSRC:$(SOURCE)/%.c=$(DEBUG)/%.o) $(CXXSRC:$(SOURCE)/%.cpp=$(DEBUG)/%.o)

CFLAGS   =
CXXFLAGS =
CPPFLAGS = -c -I$(INCLUDE) -Wall -Wextra
LDFLAGS  =
LDLIBS   = -lstdc++ -lpthread

all: $(BUILD)
	@$(MAKE) $(BUILD)/$(PROG) --no-print-directory
	@ln -sf $(BUILD)/$(PROG) $(PROG)

debug: $(DEBUG)
	@$(MAKE) $(DEBUG)/$(PROG) --no-print-directory
	@ln -sf $(DEBUG)/$(PROG) $(PROG)

clean:
	$(RM) $(PROG) $(OBJS) $(D_OBJS) $(BUILD)/$(PROG) $(DEBUG)/$(PROG)
	@/bin/echo -e '\e[1;32mClean...\e[0m'

install:
	@if [ ! $$UID -eq 0 ]; then echo "Must be run as root."; exit 1; fi
	-systemctl stop mc-daemon
	@if [ ! -f /etc/mc-daemon.conf ]; then cp mc-daemon.conf /etc/; fi
	$(RM) /usr/local/bin/mc-daemon /etc/systemd/system/mc-daemon.service
	cp mcd /usr/local/bin/mc-daemon
	cp mc-daemon.service /etc/systemd/system/
	systemctl enable mc-daemon
	systemctl daemon-reload

uninstall:
	@if [ ! $$UID -eq 0 ]; then echo "Must be run as root."; exit 1; fi
	-systemctl stop mc-daemon
	-systemctl disable mc-daemon
	$(RM) /usr/local/bin/mc-daemon /etc/systemd/system/mc-daemon.service
	@echo "If you no longer want it, you may now delete /etc/mc-daemon.conf"

.PHONY: all clean debug install uninstall

$(BUILD)/$(PROG): $(OBJS)
	$(CC) $(LDLIBS) $^ -o $@

$(DEBUG)/$(PROG): CPPFLAGS += -g -DDEBUG
$(DEBUG)/$(PROG): $(D_OBJS)
	$(CC) $(LDLIBS) $^ -o $@

$(BUILD)/%.o $(DEBUG)/%.o: $(SOURCE)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) $< -o $@

$(BUILD)/%.o $(DEBUG)/%.o: $(SOURCE)/%.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $< -o $@

$(BUILD) $(DEBUG):
	mkdir -p $@
