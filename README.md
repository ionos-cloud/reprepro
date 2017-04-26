reprepro with multiple versions support
=======================================

This git repository host a branch for reprepro which adds multiple versions
support to it. See [Debian bug #570623](https://bugs.debian.org/570623) for
details and updates.

Release Notes
=============

The multiple-versions patch set adds following features:

* Add shunit2 based tests (Closes: [#857302](https://bugs.debian.org/857302))
* Support multiple versions. (Closes: [#570623](https://bugs.debian.org/570623))
* Add listdistros command (Closes: [#857303](https://bugs.debian.org/857303))
* Add the commands move, movesrc, movematched, movefilter
* Add Limit and Archive option

Behavior changes
----------------

The multiple-versions reprepro keeps all package versions in the
archive. Set "Limit: 1" to keep only one version per package in the
archive to restore the previous behavior.

Database layout changes
-----------------------

The database layout changes from the upstream 5.1.1 release to the
multiple versions patch set. The difference is as following:

### 5.1.1

* packages.db maps "package name" to "control file" without duplicates
* no packagenames.db

### 5.1.1 + multiple versions

* packages.db maps "package name|version" to "control file" without
duplicates
* packagenames.db maps "package name" to "package name|version"
allowing duplicates and duplicates sorted by dpkg --compare-versions
descending

The multiple-versions reprepro supports migrating from 5.1.1 to
5.1.1 + multiple versions. Warning: There is no way back (but could be
done with a simple Python script)!
