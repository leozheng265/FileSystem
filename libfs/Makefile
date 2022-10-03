# REF: Makefile discussion slides, Makefile_v3.0 from discussion.
# Target library
lib := libfs.a
objs := fs.o disk.o

all: $(lib)

CC := gcc
FLAGS := -MMD # -Wall -Werror -Wextra

all: $(lib)
deps := $(patsubst %.o, %.d, $(objs))
-include $(deps)

$(lib): $(objs)
	@echo "Constructing Library $@..."
	@ar rcs $(lib) $^
	@echo "Library creation success."

%.o: %.c
	@echo "Compiling $@..."
	@$(CC) $(FLAGS) -c -o $@ $<
	@echo "Done."

clean:
	@echo "Removing all generated files..."
	@rm -f $(lib) $(objs) $(deps)
	@echo "Nice and Tidy."
