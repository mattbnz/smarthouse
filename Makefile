SHELL :=/bin/bash
VERSION_A := "0.0~git.$$(git rev-parse --short HEAD)"
VERSION_B := "0.0~git.$$(git rev-parse --short HEAD^1)"
PKG = "$$(dpkg-parsechangelog -S Version)"

changes:
	@if ! git diff --exit-code --quiet; then echo >&2 "uncommitted changes"; false; fi

needsupdate:
	@if [ $(PKG) == $(VERSION_A) ]; then echo >&2 "up to date"; false; fi
	@if [ $(PKG) == $(VERSION_B) ]; then echo >&2 "up to date"; false; fi

bumpversion: changes needsupdate
	@dch -b -v "$(VERSION_A)" "Update from git"
	@dch -D internal --force-distribution -r ""
	git commit -m "package update" debian/changelog

push-deb:
	fakeroot dpkg-buildpackage -uc -us
	dput home ../smarthouse_$(PKG)_amd64.changes

deb: bumpversion push-deb
