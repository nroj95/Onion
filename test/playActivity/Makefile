# ==============================================================================
# purpose:
# build and run focused host-side tests for play activity content identities.
#
# key behavior:
# - avoids onion's unrelated sdl and gtest test dependencies.
# - uses the host 7z executable for archive fixtures.
# - builds outside the production source directories.
# ==============================================================================

target := playactivity-identity-test
build_directory := build

compiler := gcc

common_flags := \
	-std=gnu18 \
	-Wall \
	-Wextra \
	-D_DEFAULT_SOURCE \
	-DPLATFORM_LINUX \
	-DSEVEN_ZIP_PATH=\"/usr/bin/7z\" \
	-I../../include \
	-I../../src/common \
	-I../../src/playActivity

sources := \
	test_identity.c \
	test_identity_raw.c \
	test_identity_zip.c \
	test_identity_cue.c \
	test_identity_m3u.c \
	test_identity_schema.c \
	test_identity_merge.c \
	test_support.c \
	../../src/playActivity/playActivityIdentity.c \
	../../src/playActivity/playActivitySchema.c \
	../../src/common/utils/file.c \
	../../src/common/utils/str.c \
	../../src/common/utils/log.c

objects := $(addprefix $(build_directory)/,$(sources:.c=.o))

.PHONY: all run clean

all: $(target)

sqlite_library := /usr/lib/x86_64-linux-gnu/libsqlite3.so.0

$(target): $(objects)
	$(compiler) $(objects) $(sqlite_library) -o $@

$(build_directory)/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(compiler) $(common_flags) -c "$<" -o "$@"

run: $(target)
	./$(target)

clean:
	rm -rf "$(build_directory)" "$(target)"
