# ==============================================================================
# purpose:
# build and run focused host-side tests for play activity content identities.
#
# key behavior:
# - avoids onion's unrelated sdl and gtest test dependencies.
# - uses the host 7z executable for archive fixtures.
# - keeps all generated objects inside this test directory.
# - builds against onion's bundled sqlite source.
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

test_sources := \
	test_identity.c \
	test_identity_raw.c \
	test_identity_zip.c \
	test_identity_cue.c \
	test_identity_m3u.c \
	test_identity_schema.c \
	test_identity_merge.c \
	test_identity_history.c \
	test_identity_assets.c \
	test_support.c

play_activity_sources := \
	playActivityIdentity.c \
	playActivitySchema.c \
	playActivityHistory.c \
	playActivityAssets.c

common_sources := \
	file.c \
	str.c \
	log.c

test_objects := \
	$(addprefix $(build_directory)/test/,$(test_sources:.c=.o))

play_activity_objects := \
	$(addprefix $(build_directory)/src/playActivity/,$(play_activity_sources:.c=.o))

common_objects := \
	$(addprefix $(build_directory)/src/common/utils/,$(common_sources:.c=.o))

cjson_object := $(build_directory)/include/cjson/cJSON.o
sqlite_object := $(build_directory)/include/sqlite3/sqlite3.o

objects := \
	$(test_objects) \
	$(play_activity_objects) \
	$(common_objects) \
	$(cjson_object) \
	$(sqlite_object)

.PHONY: all run clean

all: $(target)

$(target): $(objects)
	$(compiler) $(objects) -o $@

$(build_directory)/test/%.o: %.c
	@mkdir -p "$(dir $@)"
	$(compiler) $(common_flags) -c "$<" -o "$@"

$(build_directory)/src/playActivity/%.o: ../../src/playActivity/%.c
	@mkdir -p "$(dir $@)"
	$(compiler) $(common_flags) -c "$<" -o "$@"

$(build_directory)/src/common/utils/%.o: ../../src/common/utils/%.c
	@mkdir -p "$(dir $@)"
	$(compiler) $(common_flags) -c "$<" -o "$@"

$(cjson_object): ../../include/cjson/cJSON.c
	@mkdir -p "$(dir $@)"
	$(compiler) $(common_flags) -c "$<" -o "$@"

# sqlite is bundled third-party code; keep project warning flags off it.
$(sqlite_object): ../../include/sqlite3/sqlite3.c
	@mkdir -p "$(dir $@)"
	$(compiler) \
		-std=gnu18 \
		-D_DEFAULT_SOURCE \
		-Wno-discarded-qualifiers \
		-c "$<" \
		-o "$@"

run: $(target)
	./$(target)

clean:
	rm -rf "$(build_directory)" "$(target)"
