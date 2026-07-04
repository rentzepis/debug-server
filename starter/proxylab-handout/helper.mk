SUBMIT_MESSAGE =
SUBMIT_MESSAGE += "I affirm that I have complied with this course's academic"
SUBMIT_MESSAGE += "integrity policy as defined at"
SUBMIT_MESSAGE += "https://www.cs.cmu.edu/~213/academicintegrity.html [y/N]: "

# Set course/lab variables
include .labname.mk

#####################################################################
# Rules to check code formatting
#####################################################################

CLANG_FORMAT ?= clang-format

.PHONY: format
format: $(FORMAT_FILES)
	$(CLANG_FORMAT) -style=file -i $(FORMAT_FILES)

.PHONY: check-format
check-format: .format-checked

.format-checked: $(FORMAT_FILES)
	CLANG_FORMAT=$(CLANG_FORMAT) ./check-format $^
	@touch .format-checked

#####################################################################
# Rules to verify executability of scripts
#####################################################################
# This is to catch and provide immediate help for the common mistake
# of unpacking the handout tarball on a Windows box, thus stripping
# all the executable bits, and then uploading all the files to a
# cluster machine one by one.

HANDOUT_SCRIPTS += check-format

.PHONY: check-scripts
check-scripts:
	@for script in $(HANDOUT_SCRIPTS); do                           \
	  if [ ! -x "$$script" ]; then                                  \
	    scripts_nox="$$scripts_nox$$script ";                       \
	  fi;                                                           \
	done;                                                           \
	if [ -n "$$scripts_nox" ]; then                                 \
	  if [ -d .git ]; then                                          \
	    thiscmd="these commands";                                   \
	  else                                                          \
	    thiscmd="this command";                                     \
	  fi;                                                           \
	  printf '%s\n'                                                 \
	    "*** error: scripts without execute bit: $$scripts_nox"     \
	    "*** To fix this error, run $$thiscmd:"                     \
	    "  chmod +x $$scripts_nox";                                 \
	  if [ "$$thiscmd" = "these commands" ]; then                   \
	    printf '%s\n'                                               \
	      "  git commit -m 'Restore execute bit' -- $$scripts_nox"  \
	      "  git push";                                             \
	  fi;                                                           \
	  exit 1;                                                       \
	fi

all: check-scripts
format: check-scripts
