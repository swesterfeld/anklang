# This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0

# == MAKE setup ==
all:		# Default Rule
MAKEFLAGS      += -r
SHELL         ::= /bin/bash -o pipefail
PARALLEL_MAKE   = $(filter JOBSERVER, $(subst -j, JOBSERVER , $(MFLAGS)))
S ::= # Variable containing 1 space
S +=

# == Version ==
version_full    != misc/version.sh
version_short  ::= $(word 1, $(version_full))
version_hash   ::= $(word 2, $(version_full))
version_date   ::= $(wordlist 3, 999, $(version_full))
version_bits   ::= $(subst _, , $(subst -, , $(subst ., , $(version_short))))
version_major  ::= $(word 1, $(version_bits))
version_minor  ::= $(word 2, $(version_bits))
version_micro  ::= $(word 3, $(version_bits))
version_to_month = $(shell echo "$(version_date)" | sed -r -e 's/^([2-9][0-9][0-9][0-9])-([0-9][0-9])-.*/m\2 \1/' \
			-e 's/m01/January/ ; s/m02/February/ ; s/m03/March/ ; s/m04/April/ ; s/m05/May/ ; s/m06/June/' \
			-e 's/m07/July/ ; s/m08/August/ ; s/m09/September/ ; s/m10/October/ ; s/m11/November/ ; s/m12/December/')
version-info:
	@echo version_full: $(version_full)
	@echo version_short: $(version_short)
	@echo version_hash: $(version_hash)
	@echo version_date: $(version_date)
	@echo version_major: $(version_major)
	@echo version_minor: $(version_minor)
	@echo version_micro: $(version_micro)
	@echo version_to_month: "$(version_to_month)"
ifeq ($(version_micro),)	# do we have any version?
$(error Missing version information, run: misc/version.sh)
endif

# == User Defaults ==
# see also 'make default' rule
-include config-defaults.mk

# == builddir ==
# Allow O= and builddir= on the command line
ifeq ("$(origin O)", "command line")
builddir	::= $O
builddir.origin ::= command line
endif
ifeq ('$(builddir)', '')
builddir	::= out
endif
# Provide $> as builddir shorthand, used in almost every rule
>		  = $(builddir)
# if 'realpath --relative-to' is missing, os.path.realpath could be used as fallback
build2srcdir	 != realpath --relative-to $(builddir) .

# == Mode ==
# determine build mode
MODE.origin ::= $(origin MODE) # before overriding, remember if MODE came from command line
override MODE !=  case "$(MODE)" in \
		    p*|pr*|pro*|prod*|produ*|produc*|product*)		MODE=production ;; \
		    producti*|productio*|production)			MODE=production ;; \
		    r*|re*|rel*|rele*|relea*|releas*|release)		MODE=production ;; \
		    dev*|deve*|devel*|develo*|develop*|developm*)	MODE=devel ;; \
		    developme*|developmen*|development)			MODE=devel ;; \
		    d*|de*|deb*|debu*|debug|dbg)			MODE=debug ;; \
		    u*|ub*|ubs*|ubsa*|ubsan)				MODE=ubsan ;; \
		    a*|as*|asa*|asan)					MODE=asan ;; \
		    t*|ts*|tsa*|tsan)					MODE=tsan ;; \
		    l*|ls*|lsa*|lsan)					MODE=lsan ;; \
		    q*|qu*|qui*|quic*|quick)				MODE=quick ;; \
		    *)							MODE=production ;; \
		  esac ; echo "$$MODE"
.config.defaults += MODE
$(info $S  MODE     $(MODE))
.config.defaults += INSN

# == Dirctories ==
prefix		 ?= /usr/local
bindir		 ?= $(prefix)/bin
sharedir 	 ?= $(prefix)/share
mandir		 ?= $(sharedir)/man
docdir		 ?= $(sharedir)/doc
libdir		 ?= $(prefix)/lib
pkgprefix	 ?= $(libdir)
pkgdir		 ?= $(pkgprefix)/anklang-$(version_major)-$(version_minor)
pkgsharedir	 ?= $(pkgdir)/share
.config.defaults += prefix bindir sharedir mandir docdir libdir pkgprefix pkgdir
pkgdocdir	 ?= $(pkgdir)/doc
ifneq (1,$(words [$(prefix)])) # guard against `prefix := / / ` etc which will confuse `rm -rf`
$(error Variable `prefix` must expand to a single directory name, current value: `prefix:=$(prefix)`)
endif

# == Target Collections ==
ALL_TARGETS	::=
ALL_TESTS	::=
CHECK_TARGETS	::=
CLEANFILES	::=
CLEANDIRS	::=

# == Defaults ==
INCLUDES	::= -I.
DEFS		::=

# == Compiler Setup ==
CXXSTD		::= -std=gnu++20 -pthread -pipe
CSTD		::= -std=gnu11 -pthread -pipe
EXTRA_DEFS	::= # target private defs, lesser precedence than CXXFLAGS
EXTRA_INCLUDES	::= # target private defs, lesser precedence than CXXFLAGS
EXTRA_FLAGS	::= # target private flags, precedence over CXXFLAGS
CLANG_TIDY	 ?= clang-tidy

# == Utilities & Checks ==
include misc/config-utils.mk
include misc/config-uname.mk
include misc/config-checks.mk
include misc/config-external.mk

NPM_INSTALL ?= $(XNPM) install
.config.defaults += CC CFLAGS CXX CLANG_TIDY CXXFLAGS LDFLAGS LDLIBS NPM_INSTALL

# == ls-tree.lst ==
# Requires either git or a pre-packaged `ls-tree.lst` file
$>/ls-tree.lst: ; $(MAKE) $>/ls-tree.d
# Note, GITCOMMITDEPS needs misc/config-utils.mk to be included
# Read ls-tree.lst into $(LS_TREE_LST)
LS_TREE_LST ::= # added to by ls-tree.d
$>/ls-tree.d: $(GITCOMMITDEPS)						| $>/
	$(QGEN)
	$Q if test -r .git ; then					\
		git ls-tree -r --name-only HEAD				\
		| grep -v '^external/' > $>/ls-tree.lst ;		\
	   else								\
		$(CP) ./ls-tree.lst $>/ls-tree.lst ;			\
	   fi
	$Q ( echo 'LS_TREE_LST += $$(strip '\\ 				\
	     && sed 's/$$/ \\/' $>/ls-tree.lst && echo ')' ) > $@
-include $>/ls-tree.d

# == .submodule-stamp ==
.submodule-stamp: $(GITCOMMITDEPS)
	$(QGEN)
	$Q test ! -e .git || git submodule update --init --recursive
	$Q touch $@
Makefile.mk: .submodule-stamp

# == enduser targets ==
all: FORCE
check: FORCE
check-audio: FORCE
install: FORCE
uninstall: FORCE
installcheck: FORCE
lint: FORCE

# == subdirs ==
include devices/Makefile.mk
include ase/Makefile.mk
include images/knobs/Makefile.mk
include ui/Makefile.mk
include electron/Makefile.mk
include misc/Makefile.mk
include doc/style/Makefile.mk
include doc/Makefile.mk

# == run ==
run: FORCE all
	$>/electron/anklang

# == clean rules ==
clean: FORCE
	@test -z "$(strip $(CLEANFILES))" || (set -x; rm -f $(CLEANFILES) )
	@test -z "$(strip $(CLEANDIRS))" || (set -x; rm -fr $(CLEANDIRS) )
CLEANDIRS += poxy/ html/ assets/

# == help rules ==
help: FORCE
	@echo 'Make targets:'
	@: #   12345678911234567892123456789312345678941234567895123456789612345678971234567898
	@echo '  all             - Build all targets, uses config-defaults.mk if present.'
	@echo '  clean           - Remove build directory, but keeps config-defaults.mk.'
	@echo '  install         - Install binaries and data files under $$(prefix)'
	@echo '  uninstall       - Uninstall binaries, aliases and data files'
	@echo '  installcheck    - Run checks on the installed project files.'
	@echo '  default         - Create config-defaults.mk with variables set via the MAKE'
	@echo '                    command line. Inspect the file for a list of variables to'
	@echo '                    be customized. Deleting it will undo any customizations.'
	@echo '  check           - Run selfttests and unit tests'
	@echo '  x11test         - Replay all JSON recordings from x11test/ in Electron.'
	@echo '  x11test-v       - Run "x11test" in virtual XServer (headless).'
	@echo '  check-audio     - Validate Anklang rendering against reference files'
	@echo '  check-bench     - Run the benchmark tests'
	@echo '  check-loading   - Check all distributed Anklang files load properly'
	@echo '  check-suite     - Run the unit test suite'
	@echo '  serve           - Start SoundEngine, serve and auto-rebuild ui/ sources'
	@echo '  viewdocs        - Build and browser the manual'
	@echo '  run             - Start Anklang without installation'
	@echo 'Invocation:'
	@echo '  make V=1        - Enable verbose output from MAKE and subcommands'
	@echo '  make O=DIR      - Create all output files in DIR, see also config-defaults.mk'
	@echo '                    for related variables like CXXFLAGS'
	@echo '  make DESTDIR=/  - Absolute path prepended to all install/uninstall locations'
	@echo "  make MODE=...   - Run 'quick' build or make 'production' mode binaries."
	@echo '                    Other modes: debug, devel, asan, lsan, tsan, ubsan'

# == 'default' settings ==
# Allow value defaults to be adjusted via: make default builddir=... CXX=...
default: FORCE
	$(QECHO) WRITE config-defaults.mk
	$Q echo -e '# make $@\n'					> $@.tmp
	$Q echo 'builddir = $(builddir)'				>>$@.tmp
	$Q : $(foreach VAR, $(.config.defaults), &&					\
	  if $(if $(filter command, $(origin $(VAR)) $($(VAR).origin)),			\
		true, false) ; then							\
	    echo '$(VAR) = $(value $(VAR))'				>>$@.tmp ;	\
	  elif ! grep -sEm1 '^\s*$(VAR)\s*:?[:!?]?=' config-defaults.mk	>>$@.tmp ; then	\
	    echo '# $(VAR) = $(value $(VAR))'				>>$@.tmp ;	\
	  fi )
	$Q mv $@.tmp config-defaults.mk

# == output directory rules ==
# rule to create output directories from order only dependencies, trailing slash required
$>/%/:
	$Q mkdir -p $@
.PRECIOUS: $>/%/ # prevent MAKE's 'rm ...' for automatically created dirs

# == FORCE rules ==
# Use FORCE to mark phony targets via a dependency
.PHONY:	FORCE

# == PACKAGE_CONFIG ==
define PACKAGE_VERSIONS
  "version": "$(version_short)",
  "revdate": "$(version_date)",
  "mode": "$(MODE)"
endef

define PACKAGE_CONFIG
  "config": {
    "srcdir": "$(abspath .)",
    "outdir": "$(abspath $>)",
    "pkgdir": "$(abspath $(pkgdir))",
    $(strip $(PACKAGE_VERSIONS)) }
endef

# == package.json ==
$>/package.json: misc/package.json.in $(GITCOMMITDEPS)			| $>/
	$(QGEN)
	$Q : $(file > $@.config,$(PACKAGE_CONFIG),)
	$Q sed -e '1r '$@.config $< > $@.tmp
	$Q rm $@.config
	$Q mv $@.tmp $@
CLEANDIRS += $>/node_modules/

# == npm.done ==
$>/node_modules/.npm.done: $(if $(NPMBLOCK),, $>/package.json)	| $>/
	$(QGEN)
	$Q rm -f -r $>/node_modules/
	@: # Install all node_modules and anonymize build path
	$Q cd $>/ \
	  && { POFFLINE= && test ! -d node_modules/ || POFFLINE=--prefer-offline ; } \
	  && $(NPM_INSTALL) $$POFFLINE \
	  && find . -name package.json -print0 | xargs -0 sed -r "\|$$PWD|s|^(\s*(\"_where\":\s*)?)\"$$PWD|\1\"/...|" -i
	@: # Fix bun installation, see: https://github.com/oven-sh/bun/pull/5077
	$Q test ! -d $>/node_modules/sharp/ -o -d $>/node_modules/sharp/build/Release/ || (cd $>/node_modules/sharp/ && $(NPM_INSTALL))
	$Q test -d $>/node_modules/electron/dist/ || (cd $>/node_modules/electron/ && $(NPM_INSTALL))
	$Q: # add newer nodejs API to browserify-url.js, needed for e.g. postcss error messages
	$Q touch $@
NODE_PATH ::= $(abspath $>/node_modules/)
export NODE_PATH

# == uninstall ==
uninstall:
	$Q $(RMDIR_P) '$(DESTDIR)$(pkgdir)/' ; true

# == x11test ==
x11test/files.json := $(wildcard x11test/*.json)
x11test x11test-v: $(x11test/files.json) $(lib/AnklangSynthEngine)
	$(QGEN)
	$Q rm -f -r $>/x11test/
	$Q mkdir -p $>/x11test/
	$Q $(CP) $(x11test/files.json) $>/x11test/
	$Q cd $>/x11test/ \
	&& { test "$@" == x11test-v && H=-v || H= ; } \
	&& for json in *.json ; do \
		echo "$$json" \
		&& $(abspath x11test/replay.sh) $$H $$json || exit $$? \
	 ; done
CLEANDIRS += $>/x11test/
.PHONY: x11test x11test-v

# == check rules ==
# Macro to generate test runs as 'check' dependencies
define CHECK_ALL_TESTS_TEST
CHECK_TARGETS += $$(dir $1)check-$$(notdir $1)
$$(dir $1)check-$$(notdir $1): $1
	$$(QECHO) RUN… $$@
	$$Q $1
endef
$(foreach TEST, $(ALL_TESTS), $(eval $(call CHECK_ALL_TESTS_TEST, $(TEST))))
CHECK_TARGETS += check-audio check-bench
check: $(CHECK_TARGETS) lint
$(CHECK_TARGETS): FORCE
check-bench: FORCE

# == installcheck ==
installcheck-buildtest:
	$(QGEN)
	$Q cd $> $(file > $>/conftest_buildtest.cc, $(conftest_buildtest.c)) \
	&& test -r conftest_buildtest.cc \
		; X=$$? ; echo -n "Create  Anklang sample program: " ; test 0 == $$X && echo OK || { echo FAIL; exit $$X ; }
	$Q cd $> \
	&& $(CCACHE) $(CXX) $(CXXSTD) -Werror \
		`PKG_CONFIG_PATH="$(DESTDIR)$(pkgdir)/lib/pkgconfig:$(libdir)/pkgconfig:$$PKG_CONFIG_PATH" $(PKG_CONFIG) --cflags anklang` \
		-c conftest_buildtest.cc \
		; X=$$? ; echo -n "Compile Anklang sample program: " ; test 0 == $$X && echo OK || { echo FAIL; exit $$X ; }
	$Q cd $> \
	&& $(CCACHE) $(CXX) $(CXXSTD) -Werror conftest_buildtest.o -o conftest_buildtest $(LDMODEFLAGS) \
		`PKG_CONFIG_PATH="$(DESTDIR)$(pkgdir)/lib/pkgconfig:$(libdir)/pkgconfig:$$PKG_CONFIG_PATH" $(PKG_CONFIG) --libs anklang` \
		; X=$$? ; echo -n "Link    Anklang sample program: " ; test 0 == $$X && echo OK || { echo FAIL; exit $$X ; }
	$Q cd $> \
	&& LD_LIBRARY_PATH="$(DESTDIR)$(pkgdir)/lib:$$LD_LIBRARY_PATH" ./conftest_buildtest \
		; X=$$? ; echo -n "Execute Anklang sample program: " ; test 0 == $$X && echo OK || { echo FAIL; exit $$X ; }
	$Q cd $> \
	&& rm -f conftest_buildtest.cc conftest_buildtest.o conftest_buildtest
.PHONY: installcheck-buildtest
installcheck: installcheck-buildtest
# conftest_buildtest.c
define conftest_buildtest.c
#include <engine/engine.hh>
extern "C"
int main (int argc, char *argv[])
{
  Anklang::start_server (&argc, argv);
  return 0;
}
endef

# == dist ==
extradist ::= ChangeLog doc/copyright TAGS ls-tree.lst # doc/README
dist_exclude := $(strip			\
	external/rapidjson/bin		\
	external/rapidjson/doc		\
	external/websocketpp/test	\
	external/clap/artwork		\
	external/minizip-ng/test	\
	external/minizip-ng/lib		\
)
dist: $(extradist:%=$>/%)
	@$(eval distname := anklang-$(version_short))
	$(QECHO) MAKE $(distname).tar.zst
	$Q git describe --dirty | grep -qve -dirty || echo -e "#\n# $@: WARNING: working tree is dirty\n#"
	$Q rm -rf $>/dist/$(distname)/ && mkdir -p $>/dist/$(distname)/ assets/
	$Q $(CP) $>/ChangeLog assets/ChangeLog-$(version_short).txt
	$Q git archive -o assets/$(distname).tar --prefix=$(distname)/ HEAD
	$Q git submodule foreach \
	  'git archive --prefix="$(distname)/$${displaypath}/" -o tmp~.tar HEAD && tar Af "$(abspath assets/$(distname).tar)" tmp~.tar && rm tmp~.tar'
	$Q tar f assets/$(distname).tar --delete $(dist_exclude:%=$(distname)/%)
	$Q cd $>/ && $(CP) --parents $(extradist) $(abspath $>/dist/$(distname))
	$Q tar f assets/$(distname).tar --delete $(distname)/NEWS.md \
	&& misc/mknews.sh				>  $>/dist/$(distname)/NEWS.md \
	&& cat NEWS.md					>> $>/dist/$(distname)/NEWS.md
	$Q tar xf assets/$(distname).tar -C $>/dist/ $(distname)/misc/version.sh	# fetch archived version.sh
	$Q tar f assets/$(distname).tar --delete $(distname)/misc/version.sh	# delete, make room for replacement
	$Q sed "s/^BAKED_DESCRIBE=.*/BAKED_DESCRIBE='$(version_short)'/" \
		-i $>/dist/$(distname)/misc/version.sh	# support lightweight tags and fix git-2.25.1 which cannot describe:match
	$Q cd $>/dist/ && tar uhf $(abspath assets/$(distname).tar) $(distname)	# update (replace) files in tarball
	$Q rm -rf assets/$(distname).tar.zst && zstd --ultra -22 --rm assets/$(distname).tar && ls -lh assets/$(distname).tar.zst
	$Q echo "Archive ready: assets/$(distname).tar.zst" | sed '1h; 1s/./=/g; 1p; 1x; $$p; $$x'
.PHONY: dist

# == ChangeLog ==
$>/ChangeLog: $(GITCOMMITDEPS) Makefile.mk			| $>/
	$(QGEN)
	$Q git log --abbrev=13 --date=short --first-parent HEAD	\
		--pretty='^^%ad  %an 	# %h%n%n%B%n'		 > $@.tmp	# Generate ChangeLog with ^^-prefixed records
	$Q sed 's/^/	/; s/^	^^// ; s/[[:space:]]\+$$// '    -i $@.tmp	# Tab-indent commit bodies, kill trailing whitespaces
	$Q sed '/^\s*$$/{ N; /^\s*\n\s*$$/D }'			-i $@.tmp	# Compress multiple newlines
	$Q mv $@.tmp $@
	$Q test -s $@ || { mv $@ $@.empty ; ls -al --full-time $@.empty ; exit 1 ; }

# == TAGS ==
# ctags --print-language `git ls-tree -r --name-only HEAD`
$>/TAGS: $>/ls-tree.lst Makefile.mk
	$(QGEN)
	$Q ctags -e -o $@ -L $>/ls-tree.lst
ALL_TARGETS += $>/TAGS

# == compile_commands.json ==
compile_commands.json: Makefile.mk
	$(QGEN)
	$Q rm -f $>/*/*.o $>/*/*/*.o
	bear -- $(MAKE) CC=clang CXX=clang++ -j

# == all rules ==
all: $(ALL_TARGETS) $(ALL_TESTS)

# == grep-reminders ==
$>/.grep-reminders: $(wildcard $(LS_TREE_LST))
	$Q test -r .git && git -P grep -nE '(/[*/]+[*/ ]*|[#*]+ *)?(FI[X]ME).*' || true
	$Q touch $@
all: $>/.grep-reminders

# Clean non-directories in $>/
CLEANFILES += $(filter-out $(patsubst %/,%,$(wildcard $>/*/)), $(wildcard $>/* $>/.[^.]* $>/..?* ))
