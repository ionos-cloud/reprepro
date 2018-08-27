reprepro with multiple versions support
=======================================

This git repository hosts a branch for reprepro which adds multiple versions
support to it. See [Debian bug #570623](https://bugs.debian.org/570623) for
details and updates.

The upstream repository can be found on https://salsa.debian.org/brlink/reprepro

Release Notes
=============

The multiple-versions patch set adds following features:

* Add shunit2 based tests (Closes: [#857302](https://bugs.debian.org/857302))
* Support multiple versions. (Closes: [#570623](https://bugs.debian.org/570623))
* Add the commands move, movesrc, movematched, movefilter
* Add Limit and Archive option

Behavior changes
----------------

The multiple-versions reprepro keeps all package versions in the
archive. Set "Limit: 1" to keep only one version per package in the
archive to restore the previous behavior.

Database layout changes
-----------------------

The database layout changes from the upstream release to the
multiple versions patch set. The difference is as following:

### upstream

* packages.db maps "package name" to "control file" without duplicates
* no packagenames.db

### multiple versions

* packages.db maps "package name|version" to "control file" without
duplicates
* packagenames.db maps "package name" to "package name|version"
allowing duplicates and duplicates sorted by dpkg --compare-versions
descending

The first time the database is opened by reprepro with multiple versions
support, the database will be upgraded from the upstream layout to the multiple
versions layout. *Warning*: There is no way back (but could be done with a
simple Python script)!
