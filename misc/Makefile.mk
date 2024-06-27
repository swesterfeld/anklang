# This Source Code Form is licensed MPL-2.0: http://mozilla.org/MPL/2.0
include $(wildcard $>/misc/*.d)
misc/cleandirs ::= $(wildcard $>/misc/)
CLEANDIRS       += $(misc/cleandirs)
ALL_TARGETS     += misc/all
misc/all:

# == clean-misc ==
clean-misc:
	rm -rf $(misc/cleandirs)
.PHONY: clean-misc

# == lint-cppcheck ==
CPPCHECK ?= cppcheck
CPPCHECK_CCENABLE := warning,style,performance,portability
lint-cppcheck: $>/ls-tree.lst misc/Makefile.mk		| $>/misc/cppcheck/
	$Q egrep "^(ase|devices|jsonipc|ui)/.*\.(cc|hh)$$" < $<		> $>/misc/$@.lst
	$Q $(CPPCHECK) --enable=$(CPPCHECK_CCENABLE) $(CPPCHECK_DEFS) $$(cat $>/misc/$@.lst) $(wildcard $>/ase/*.cc)
CPPCHECK_DEFS := -D__SIZEOF_LONG__=8 -D__SIZEOF_WCHAR_T__=4 -D__linux__ -U_SC_NPROCESSORS_ONLN -U_WIN32 -U__clang__
.PHONY: lint-cppcheck

# == lint-unused ==
lint-unused: $>/ls-tree.lst misc/Makefile.mk		| $>/misc/cppcheck/
	$Q egrep "^(ase|devices|jsonipc|ui)/.*\.(cc|hh)$$" < $<		> $>/misc/$@.lst
	$Q $(CPPCHECK) --enable=unusedFunction,$(CPPCHECK_CCENABLE) $(CPPCHECK_DEFS) $$(cat $>/misc/$@.lst) $(wildcard $>/ase/*.cc) \
	|& grep --color=auto -E '(\b(un)?use|\bnever\b|\b(un)?reach)\w*'
.PHONY: lint-unused

# == clang-tidy ==
CLANG_TIDY_FILES = $(filter %.c %.cc %.C %.cpp %.cxx, $(LS_TREE_LST))
CLANG_TIDY_LOGS  = $(patsubst %, $>/clang-tidy/%.log, $(CLANG_TIDY_FILES))
clang-tidy clang-tidy-check: $(CLANG_TIDY_LOGS)
	$(QGEN)
	$Q OK=true \
	&& for log in $(CLANG_TIDY_LOGS) ; do \
		grep -Eq ': (error|warning):' $$log && OK=false ; \
		test 1 -ge `wc -l < $$log` || \
		  sed "s|^$$PWD/||" < $$log | misc/colorize.sh >&2 ; \
	   done \
	&& test $@ != clang-tidy-check || $$OK
$>/clang-tidy/%.log: % $(GITCOMMITDEPS)					| $>/clang-tidy/
	$(QECHO) CLANG-TIDY $@
	$Q mkdir -p $(dir $@) && rm -f $>/clang-tidy/$<.*
	$Q set +o pipefail \
	&& CTIDY_DEFS=( $(ASE_EXTERNAL_INCLUDES) $(CLANG_TIDY_DEFS) $($<.CTIDY_DEFS) -march=x86-64-v2 ) \
	&& [[ $< = @(*.[hc]) ]] || CTIDY_DEFS+=( -std=gnu++20 ) \
	&& (set -x ; $(CLANG_TIDY) --export-fixes=$>/clang-tidy/$<.yaml $< $($<.CTIDY_FLAGS) -- "$${CTIDY_DEFS[@]}" ) >$@~ 2>&1 || :
	$Q mv $@~ $@
CLANG_TIDY_DEFS := -I. -I$> -isystem external/ -isystem $>/external/ -DASE_COMPILATION $(ASEDEPS_CFLAGS) $(GTK2_CFLAGS)
# File specific LINT_FLAGS, example:		ase/jsonapi.cc.LINT_FLAGS ::= --checks=-clang-analyzer-core.NullDereference
jsonipc/testjsonipc.cc.CTIDY_DEFS ::= -D__JSONIPC_NULL_REFERENCE_THROWS__
.PHONY: clang-tid clang-tidy-clean
CLEANDIRS += $>/clang-tidy/

# == scan-build ==
scan-build:								| $>/misc/scan-build/
	$(QGEN)
	$Q rm -rf $>/misc/scan-tmp/ && mkdir -p $>/misc/scan-tmp/
	$Q echo "  CHECK   " "for CXX to resemble clang++"
	$Q $(CXX) --version | grep '\bclang\b'
	scan-build -o $>/misc/scan-build/ --use-cc "$(CC)" --use-c++ "$(CXX)" $(MAKE) CCACHE= -j`nproc`
	$Q shopt -s nullglob ; \
	      for r in $>/misc/scan-build/20??-??-??-*/report-*.html ; do \
		D=$$(sed -nr '/<!-- BUGDESC/ { s/^<!-- \w+ (.+) -->/\1/	   ; p }' $$r) && \
		F=$$(sed -nr '/<!-- BUGFILE/ { s/^<!-- \w+ ([^ ]+) -->/\1/ ; p }' $$r) && \
		L=$$(sed -nr '/<!-- BUGLINE/ { s/^<!-- \w+ ([^ ]+) -->/\1/ ; p }' $$r) && \
		echo "$$F:$$L: $$D" | sed "s,^`pwd`/,," ; \
	      done > $>/misc/scan-build/scan-build.log
	misc/blame-lines -b $>/misc/scan-build/scan-build.log
.PHONY: scan-build
# Note, 'make scan-build' requires 'make default CC=clang CXX=clang++' to generate any reports.

# == branch-check ==
# Check for various lint results in files touched by the current branch
BRANCH_CHECK_UPSTREAM ::= trunk
branch-check:
	$(QGEN)
	$Q script $>/$(@F).log </dev/null -e -c ' : \
		&& $(MAKE) -j`nproc` all \
		&& $(MAKE) lint \
		&& $(MAKE) -j`nproc` clang-tidy \
	   '
	$Q git diff --name-only $$(git merge-base $(BRANCH_CHECK_UPSTREAM) HEAD) > $>/$(@F).files && sed 's/$$/:/' -i $>/$(@F).files
	$Q grep -qFf $>/$(@F).files $>/$(@F).log || exit 0 \
	&& M="$@: $$(git describe --all --always HEAD): Problems found in files touched since '$(BRANCH_CHECK_UPSTREAM)'" \
	&& echo && echo "$$M" | sed -e '1{ h; s/./=/g; p;x; p;x; }'
	$Q (set -x && grep -m 24 --color=auto -Ff $>/$(@F).files $>/$(@F).log) || exit 0 && exit $(BRANCH_CHECK_EXIT)
BRANCH_CHECK_EXIT ?= 0
.PHONY: branch-check

# == misc/anklang.desktop ==
# https://specifications.freedesktop.org/desktop-entry-spec/desktop-entry-spec-latest.html
$>/misc/anklang.desktop: misc/anklang.desktop		| $>/misc/
	@$(QGEN)
	$Q sed 's|\$$(pkgdir)|$(pkgdir)|g' $< > $@.tmp
	$Q mv $@.tmp $@
misc/all: $>/misc/anklang.desktop

# == installation ==
misc/svgdir ::= $(sharedir)/icons/hicolor/scalable/apps
misc/install:
	@$(QECHO) INSTALL '$(DESTDIR)$(pkgsharedir)/.'
	$Q rm -f -r $(DESTDIR)$(pkgsharedir)/applications/ $(DESTDIR)$(pkgsharedir)/mime/packages/
	$Q $(INSTALL) -d $(DESTDIR)$(pkgsharedir)/mime/packages/ && cp -p misc/anklang-mime.xml $(DESTDIR)$(pkgsharedir)/mime/packages/anklang.xml
	$Q $(INSTALL) -d $(DESTDIR)$(sharedir)/mime/packages/ && ln -fs -r $(DESTDIR)$(pkgsharedir)/mime/packages/anklang.xml $(DESTDIR)$(sharedir)/mime/packages/anklang.xml
	$Q $(INSTALL) -d $(DESTDIR)$(pkgsharedir)/applications/ && cp -p $>/misc/anklang.desktop $(DESTDIR)$(pkgsharedir)/applications/
	$Q $(INSTALL) -d $(DESTDIR)$(sharedir)/applications/ && ln -fs -r $(DESTDIR)$(pkgsharedir)/applications/anklang.desktop $(DESTDIR)$(sharedir)/applications/anklang.desktop
	$Q $(INSTALL) -d $(DESTDIR)$(misc/svgdir)/ && ln -fs -r $(DESTDIR)$(pkgdir)/ui/assets/favicon.svg $(DESTDIR)$(misc/svgdir)/anklang.svg

.PHONY: misc/install
install: misc/install
misc/uninstall: FORCE
	@$(QECHO) REMOVE '$(DESTDIR)$(pkgsharedir)/.'
	$Q rm -f -r $(DESTDIR)$(pkgsharedir)/applications/ $(DESTDIR)$(pkgsharedir)/mime/packages/
	$Q rm -f $(DESTDIR)$(sharedir)/mime/packages/anklang.xml
	$Q rm -f $(DESTDIR)$(sharedir)/applications/anklang.desktop
	$Q rm -f $(DESTDIR)$(misc/svgdir)/anklang.svg
.PHONY: misc/uninstall
uninstall: misc/uninstall

# == Check Copyright Notices ==
check-copyright: misc/mkcopyright.py doc/copyright.ini $>/ls-tree.lst
	$(QGEN)
	$Q misc/mkcopyright.py -b -u -e -c doc/copyright.ini $$(cat $>/ls-tree.lst)
CHECK_TARGETS += $(if $(HAVE_GIT), check-copyright)

# == appimagetools/appimage-runtime-zstd ==
$>/appimagetools/appimage-runtime-zstd:			| $>/appimagetools/
	$(QECHO) FETCH linuxdeploy, appimage-runtime-zstd
	$Q mkdir -p .dlcache/
	$Q test -e .dlcache/linuxdeploy-x86_64.AppImage \
	|| ( curl -sfSL https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage -o .dlcache/linuxdeploy-x86_64.AppImage.tmp \
	     && chmod +x .dlcache/linuxdeploy-x86_64.AppImage.tmp \
	     && mv .dlcache/linuxdeploy-x86_64.AppImage.tmp .dlcache/linuxdeploy-x86_64.AppImage )
	$Q test -e .dlcache/appimage-runtime-zstd \
	|| ( curl -sfSL https://github.com/tim-janik/appimage-runtime/releases/download/21.6.0/appimage-runtime-zstd -o .dlcache/appimage-runtime-zstd.tmp \
	     && mv .dlcache/appimage-runtime-zstd.tmp .dlcache/appimage-runtime-zstd )
	$Q $(CP) .dlcache/linuxdeploy-x86_64.AppImage .dlcache/appimage-runtime-zstd $(@D)
CLEANDIRS += $>/appimagetools/

# == mkassets ==
# Let misc/mkassets.sh do the work, just pre-cache needed downloads
mkassets: $>/appimagetools/appimage-runtime-zstd
	+$Q exec misc/mkassets.sh
.PHONY: mkassets
CLEANDIRS += $>/mkdeb/
