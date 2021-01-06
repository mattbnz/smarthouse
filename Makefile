SHELL :=/bin/bash
VERSION_A := "$$(git rev-parse --short HEAD)"
VERSION_B := "$$(git rev-parse --short HEAD^1)"
DATE := "$$(date +%Y%m%d%H%M)"
NEW := "0.0~git.$(DATE).$(VERSION_A)"
PKG := "$$(dpkg-parsechangelog -S Version)"
PKGGIT := "$$(dpkg-parsechangelog -S Version | cut -f4 -d.)"

changes:
	@if ! git diff --exit-code --quiet; then echo >&2 "uncommitted changes"; false; fi

needsupdate:
	@if [ $(PKGGIT) == $(VERSION_A) ]; then echo >&2 "up to date"; false; fi
	@if [ $(PKGGIT) == $(VERSION_B) ]; then echo >&2 "up to date"; false; fi

bumpversion: changes needsupdate
	@dch -b -v "$(NEW)" "Update from git"
	@dch -D internal --force-distribution -r ""
	git commit -m "package update" debian/changelog

push-deb:
	fakeroot dpkg-buildpackage -uc -us
	dput home ../smarthouse_$(PKG)_amd64.changes

deb: bumpversion push-deb
