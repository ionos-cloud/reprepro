#!/usr/bin/python3
# Copyright (C) 2012 Bernhard R. Link
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02111-1301  USA

# This is an example outhook script.
# Actually it is part of the testsuite and does many things
# an actual outhook script would never do.
# But it checks so many aspects of how a outhook script is called
# that it should make quite clear what a outhookscript can expect.

import sys, os, subprocess, select, dbm

def poolfile(outdir, name):
	s = os.lstat(outdir + '/' + name)
	return "poolfile %d bytes" % s.st_size
def distfile(outdir, name):
	s = os.lstat(outdir + '/' + name)
	return "distfile %d bytes" % s.st_size
def distsymlink(distdir, target):
	return "distsymlink -> %s/%s" % (distdir,target)
def collecteddistfile(outdir, name):
	if os.path.islink(outdir + '/' + name):
		l = os.readlink(outdir + '/' + name)
		d = os.path.dirname(name)
		while d and l[0:3] == '../':
			d = os.path.dirname(d)
			l = l[3:]
		if d:
			d = d + '/'
		return "distsymlink -> %s%s" % (d,l)
	else:
		return distfile(outdir, name)

def processfile(logfile, donefile, db):
	# print("Parsing '%s'" % logfile)
	lf = open(logfile, 'r', encoding='utf-8')
	newpoolfiles = []
	distributions = []
	deletepoolfiles = []
	mode = 'POOLNEW'
	# This parser is wasteful and unnecessarily complicated, but it's
	# purpose is mainly making sure the output of reprepro is
	# well-formed and no so much targeted at doing actual work.
	for l in lf:
		if l[-1] != '\n':
			raise CriticalError("Malformed file '%s' (not a text file)" % logfile)
		l = l[:-1]
		fields = l.split('\t')
		if fields[0] != 'POOLNEW':
			break
		if len(fields) != 2:
			raise CriticalError("Malformed file '%s': POOLNEW with more than one argument" % logfile)
		newpoolfiles.append(fields[1])
	else:
		fields = ['EOF']
	while fields[0] == 'BEGIN-DISTRIBUTION' or fields[0] == 'BEGIN-SNAPSHOT':
		beginmarker = fields[0]
		endmarker = 'END-' + beginmarker[6:]
		if len(fields) != 3 and len(fields) != 4:
			raise CriticalError("Malformed file '%s': wrong number of arguments for %s" % (logfile,beginmarker))
		distname = fields[1]
		distdir = fields[2]
		distfiles = []
		distsymlinks = []
		distdeletes = []
		for l in lf:
			if l[-1] != '\n':
				raise CriticalError("Malformed file '%s' (not a text file)" % logfile)
			l = l[:-1]
			fields = l.split('\t')
			if fields[0] == endmarker:
				if len(fields) != 3 and len(fields) != 4:
					raise CriticalError("Malformed file '%s': wrong number of arguments for %s" % (logfile, endmarker))
				if fields[1] != distname or fields[2] != distdir:
					raise CriticalError("Malformed file '%s': %s not matching previous %s" % (logfile, endmarker, beginmarker))
				break
			elif fields[0] == 'DISTKEEP':
				continue
			elif not fields[0] in ['DISTFILE', 'DISTSYMLINK', 'DISTDELETE']:
				raise CriticalError("Malformed file '%s': Unexpected '%s'" % (logfile, fields[0]))
			if len(fields) < 3:
				raise CriticalError("Malformed file '%s': wrong number of arguments for %s" % (logfile, fields[0]))
			if fields[1] != distdir:
				raise CriticalError("Malformed file '%s': wrong distdir '%s' in '%s'" %(logfile, fields[1], fields[0]))
			if fields[0] == 'DISTFILE':
				if len(fields) != 4:
					raise CriticalError("Malformed file '%s': wrong number of arguments for %s" % (logfile, fields[0]))
				distfiles.append((fields[2], fields[3]))
			elif fields[0] == 'DISTDELETE':
				if len(fields) != 3:
					raise CriticalError("Malformed file '%s': wrong number of arguments for %s" % (logfile, fields[0]))
				distdeletes.append(fields[2])
			elif fields[0] == 'DISTSYMLINK':
				if len(fields) != 4:
					raise CriticalError("Malformed file '%s': wrong number of arguments for %s" % (logfile, fields[0]))
				distsymlinks.append((fields[2], fields[3]))
		else:
			raise CriticalError("Malformed file '%s': unexpected end of file (%s missing)" % (logfile, endmarker))
		distributions.append((distname, distdir, distfiles, distsymlinks, distdeletes))
		l = next(lf, 'EOF\n')
		if l[-1] != '\n':
			raise CriticalError("Malformed file '%s' (not a text file)" % logfile)
		l = l[:-1]
		fields = l.split('\t')
	while fields[0] == 'POOLDELETE':
		if len(fields) != 2:
			raise CriticalError("Malformed file '%s': wrong number of arguments for POOLDELETE" % logfile)
		deletepoolfiles.append(fields[1])
		l = next(lf, 'EOF\n')
		if l[-1] != '\n':
			raise CriticalError("Malformed file '%s' (not a text file)" % logfile)
		l = l[:-1]
		fields = l.split('\t')
	if fields[0] != 'EOF' or next(lf, None) != None:
		raise CriticalError("Malformed file '%s': Unexpected command '%s'" % (logfile, fields[0]))
	# print("Processing '%s'" % logfile)
	# Checked input to death, no actualy do something
	outdir = os.environ['REPREPRO_OUT_DIR']
	for p in newpoolfiles:
		bp = bytes(p, encoding="utf-8")
		if bp in db:
			raise Exception("duplicate pool file %s" % p)
		db[bp] = poolfile(outdir, p)
	for distname, distdir, distfiles, distsymlinks, distdeletes in distributions:
		for name, orig in distfiles:
			db[distdir + '/' + name] = distfile(outdir, orig)
		for name, target in distsymlinks:
			db[distdir + '/' + name] = distsymlink(distdir, target)
		for name in distdeletes:
			del db[distdir + '/' + name]
	for p in deletepoolfiles:
		bp = bytes(p, encoding="utf-8")
		if not bp in db:
			raise Exception("deleting non-existant pool file %s" % p)
		del db[bp]

def collectfiles(dir, name):
	for l in os.listdir(dir + '/' + name):
		n = name + '/' + l
		if os.path.isdir(dir + '/' + n):
			for x in collectfiles(dir, n):
				yield x
		else:
			yield n

def collectpool(outdir):
	if os.path.isdir(outdir + '/pool'):
		return ["%s: %s" % (filename, poolfile(outdir, filename)) for filename in collectfiles(outdir, 'pool')]
	else:
		return []

def collectdists(outdir):
	if os.path.isdir(outdir + '/dists'):
		return ["%s: %s" % (filename, collecteddistfile(outdir, filename)) for filename in collectfiles(outdir, 'dists')]
	else:
		return []

def showdiff(i1, i2):
	clean = True
	l1 = next(i1, None)
	l2 = next(i2, None)
	while l1 or l2:
		if l1 == l2:
			l1 = next(i1, None)
			l2 = next(i2, None)
		elif l1 != None and (l2 == None or l1 < l2):
			print("+ %s" % l1)
			clean = False
			l1 = next(i1, None)
		elif l2 != None and (l1 == None or l1 > l2):
			print("- %s" % l2)
			clean = False
			l2 = next(i2, None)
		else:
			raise("unexpected")
	return clean

def check(db):
	outdir = os.environ['REPREPRO_OUT_DIR']
	actualfiles = collectpool(outdir)
	actualfiles.extend(collectdists(outdir))

	expectedfiles = []
	for k in db.keys():
		expectedfiles.append("%s: %s" % (k.decode(encoding='utf-8'), db[k].decode(encoding='utf-8')))
	expectedfiles.sort()
	actualfiles.sort()
	if not showdiff(iter(expectedfiles), iter(actualfiles)):
		raise CriticalError("outdir does not match expected state")

class CriticalError(Exception):
	pass

def main(args):
	if len(args) <= 0:
		raise CriticalError("No .outlog files given at command line!")

	if len(args) == 1 and args[0] == '--print':
		db = dbm.open(os.environ['REPREPRO_OUT_DB'], 'r')
		for k in sort(db.keys()):
			print("%s: %s" % (k, db[k]))
		return
	if len(args) == 1 and args[0] == '--check':
		db = dbm.open(os.environ['REPREPRO_OUT_DB'], 'r')
		check(db)
		return

	for f in args:
		if len(f) < 8 or f[-7:] != ".outlog":
			raise CriticalError("command line argument '%s' does not look like a .outlog file!" % f)

	db = dbm.open(os.environ['REPREPRO_OUT_DB'], 'c')

	for f in args:
		donefile = f[:-7] + ".outlogdone"
		if os.path.exists(donefile):
			print("Ignoring '%s' as '%s' already exists!" % (f,donefile), file=sys.stderr)
			continue
		processfile(f, donefile, db)

try:
	main(sys.argv[1:])
except CriticalError as e:
	print(str(e), file=sys.stderr)
	raise SystemExit(1)
