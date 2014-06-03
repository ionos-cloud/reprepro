#!/usr/bin/python3
# Copyright (C) 2013 Bernhard R. Link
#
# This example script is free software; you can redistribute it
# and/or modify it under the terms of the GNU General Public License
# version 2 as published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02111-1301  USA

# Those can be set here or in conf/outsftphook.conf:
servername = None
username = None
targetdir = ""

import sys, os, subprocess, select, sftp

class Round(sftp.Enum,
	DONE = -2,
	INDIRECT = -1,
	POOLFILES = 0,
	DISTFILES = 1,
	DELETES = 2,
):
	pass

errors = 0
def printe(s):
	global errors
	print(s, file=sys.stderr)
	errors += 1

# renaming file, assuming all directories exist...
def renamefile(dst, src, donefunc):
	a = yield [sftp.REMOVE(targetdir + dst), sftp.RENAME(targetdir + src, targetdir + dst, [sftp.SSH_FXF_RENAME.OVERWRITE])]
	while True:
		l = []
		if not isinstance(a, sftp.STATUS):
			raise SftpUnexpectedAnswer(a, "expecting STATUS")
		if isinstance(a.forr, sftp.REMOVE):
			if a.status != sftp.SSH_FX.OK and a.status != sftp.SSH_FX.NO_SUCH_FILE:
				printe("%s failed: %s" % (a.forr, a))
		elif isinstance(a.forr, sftp.RENAME):
			if a.status != sftp.SSH_FX.OK:
				printe("%s failed: %s" % (a.forr, a))
			else:
				l = donefunc(dst)
		else:
			raise SftpUnexpectedAnswer(a, a.forr)
		a.forr.done()
		a = yield l

# create symlink, assuming all directories exist...
def symlinkfile(dst, src, donefunc):
	a = yield [sftp.REMOVE(targetdir + dst), sftp.SYMLINK(targetdir + dst, targetdir + src)]
	while True:
		l = []
		if not isinstance(a, sftp.STATUS):
			raise SftpUnexpectedAnswer(a, "expecting STATUS")
		if isinstance(a.forr, sftp.REMOVE):
			if a.status != sftp.SSH_FX.OK and a.status != sftp.SSH_FX.NO_SUCH_FILE:
				printe("%s failed: %s" % (a.forr, a))
		elif isinstance(a.forr, sftp.SYMLINK):
			if a.status != sftp.SSH_FX.OK:
				printe("%s failed: %s" % (a.forr, a))
			else:
				l = donefunc(dst, message="symlink done")
		else:
			raise SftpUnexpectedAnswer(a, a.forr)
		a.forr.done()
		a = yield l

def deletefile(dst, donefunc):
	a = yield [sftp.REMOVE(targetdir + dst)]
	if not isinstance(a, sftp.STATUS):
		raise SftpUnexpectedAnswer(a, "expecting STATUS")
	if a.status == sftp.SSH_FX.OK:
		l = donefunc(dst, message="deleted")
	elif a.status == sftp.SSH_FX.NO_SUCH_FILE:
		l = donefunc(dst, message="already deleted")
	else:
		printe("%s failed: %s" % (a.forr, a))
		l = []
	a.forr.done()
	a = yield l
	raise SftpUnexpectedAnswer(a, a.forr)

def writefile(fname, filetocopy, donefunc):
	filename = targetdir + fname
	fd = open(filetocopy, 'rb')
	dirname = os.path.dirname(filename)
	if dirname:
		mode = yield [('waitingfor', sftp.Dirlock, dirname)]
	else:
		mode = "top-level"
	a = yield [('lock', sftp.Semaphore, 'openfile')]
	if a != "unlock":
		raise SftpUnexpectedAnswer(a, "waiting for unlock event")
	a = yield [sftp.OPEN(filename, "CREAT|WRITE")]
	if mode == "tryandtell" and isinstance(a, sftp.STATUS) and a.status == a.status.NO_SUCH_FILE:
		a.forr.done()
		a = yield [('missing', sftp.Dirlock, dirname),
		           ('release', sftp.Semaphore, 'openfile')]
		if a != "createnew":
			raise SftpUnexpectedAnswer(a, "waiting for %s" % dirname)
		mode = a
		a = yield [('lock', sftp.Semaphore, 'openfile')]
		if a != "unlock":
			raise SftpUnexpectedAnswer(a, "waiting for unlock event")
		a = yield [sftp.OPEN(filename, "CREAT|WRITE")]
	if not isinstance(a, sftp.HANDLE):
		a.forr.done()
		printe("Failed to create %s: %s" % (filename, a))
		return
		# raise SftpException("Failed to create %s: %s" % (filename, a))
	h = a.handle
	a.forr.done()
	if mode == "tryandtell":
		f = [('found', sftp.Dirlock, dirname), 'wantwrite']
	else:
		f = ['wantwrite']
	a = yield f
	if a != 'canwrite':
		raise SftpUnexpectedAnswer(a, "waiting for 'canwrite'")
	ofs = 0
	while True:
		b = fd.read(16376)
		if len(b) == 0:
			break
		a = yield [sftp.WRITE(h, ofs, b), 'wantwrite']
		ofs += len(b)
		b = None
		while a != 'canwrite':
			a.forr.done()
	fd.close()
	a = yield [sftp.CLOSE(h), ('release', sftp.Semaphore, 'openfile')]
	while True:
		if type(a.forr) == sftp.CLOSE:
			if a.status != sftp.SSH_FX.OK:
				printe("%s failed: %s" % (a.forr, a))
			l = donefunc(fname)
		else:
			if a.status != sftp.SSH_FX.OK:
				printe("%s failed: %s" % (a.forr, a))
			l = []
		a.forr.done()
		a = yield l

class CriticalError(Exception):
	pass
class ParseError(CriticalError):
	pass
class ParseErrorWrongCount(ParseError):
	def __init__(field):
		super().__init__("Wrong number of arguments for %s" % field)

class CollectedDistDir:
	def __init__(self, dir):
		self.done = False
		self.failed = False
		self.dir = dir
		self.files = dict()
		self.deletes = dict()
		self.symlinks = dict()
		self.transfered = 0
	def onedone(self, filename):
		assert(filename.endswith(".new"))
		filename = filename[:-4]
		assert (filename in self.files)
		self.transfered += 1
		self.files[filename].markpartial(filename, "asdotnew")
		return self.finalizeifready()
	def finalizeifready(self):
		assert (not self.done)
		if len(self.files) != self.transfered:
			assert (len(self.files) > self.transfered)
			return []
		# everything copied as .new as needed, let's start finalisation
		self.done = True
		l = []
		for m,e in self.files.items():
			l.append(sftp.TaskFromGenerator(renamefile(m, m + ".new", e.doneone)))
		for m,e in self.deletes.items():
			l.append(sftp.TaskFromGenerator(deletefile(m, e.doneone)))
		for m,(t,e) in self.symlinks.items():
			l.append(sftp.TaskFromGenerator(symlinkfile(m, t, e.doneone)))
		return l

class DistDir:
	def __init__(self, dir, onelog=True):
		self.dir = dir
		self.files = []
		self.deletes = []
		self.symlinks = []
	def queue(self, todo, distdirs, logfile):
		if not self.dir in distdirs:
			collection = CollectedDistDir(self.dir)
			distdirs[self.dir] = collection
		else:
			collection = distdirs[self.dir]
		for fn, fr in self.files:
			ffn = self.dir + "/" + fn
			if logfile.alreadydone.get(ffn, "") == "asdotnew":
				if logfile.enqueue(todo, ffn, Round.INDIRECT):
					collection.files[ffn] = logfile
					collection.transfered += 1
			else:
				if logfile.enqueue(todo, ffn,
						Round.DISTFILES, ffn + ".new",
						fr, collection.onedone):
					collection.files[ffn] = logfile
		for fn in self.deletes:
			ffn = self.dir + "/" + fn
			if logfile.enqueue(todo, ffn, Round.INDIRECT):
				collection.deletes[ffn] = logfile
		for fn, flt in self.symlinks:
			ffn = self.dir + "/" + fn
			if logfile.enqueue(todo, ffn, Round.INDIRECT):
				collection.symlinks[ffn] = (flt, logfile)

class LogFile:
	def parselogline(self, fields):
		if fields[0] == 'POOLNEW':
			if len(fields) != 2:
				raise ParseErrorWrongCount(fields[0])
			self.newpoolfiles.append(fields[1])
		elif fields[0] == 'POOLDELETE':
			if len(fields) != 2:
				raise ParseErrorWrongCount(fields[0])
			self.deletepoolfiles.append(fields[1])
		elif fields[0].startswith('BEGIN-'):
			pass
		elif fields[0].startswith('END-'):
			pass
		elif fields[0].startswith('DIST'):
			command = fields[0][4:]
			if command not in ['KEEP', 'FILE', 'DELETE', 'SYMLINK']:
				raise ParseError("Unknown command %s" % command)
			if not fields[1] in self.dists:
				d = self.dists[fields[1]] = DistDir(fields[1])
			else:
				d = self.dists[fields[1]]
			if command == 'FILE':
				if len(fields) != 4:
					raise ParseErrorWrongCount(fields[0])
				d.files.append((fields[2], fields[3]))
			elif command == 'DELETE':
				if len(fields) != 3:
					raise ParseErrorWrongCount(fields[0])
				d.deletes.append(fields[2])
			elif command == 'SYMLINK':
				if len(fields) != 4:
					raise ParseErrorWrongCount(fields[0])
				d.symlinks.append((fields[2], fields[3]))
		elif fields[0] == "DONE":
			self.alreadydone[fields[2]] = fields[1]
		else:
			raise ParseError("Unknown command %s" % fields[0])
	def __init__(self, logfile, donefile):
		self.alreadydone = dict()
		self.logfile = logfile
		self.donefile = donefile
		try:
			lf = open(logfile, 'r', encoding='utf-8')
		except Exception as e:
			raise CriticalError("Cannot open %s: %s" % (repr(logfile), e))
		self.newpoolfiles = []
		self.dists = {}
		self.deletepoolfiles = []
		self.todocount = 0
		for l in lf:
			if l[-1] != '\n':
				raise ParseError("not a text file")
			self.parselogline(l[:-1].split('\t'))
		lf.close()
	def queue(self, todo, distdirs):
		self.todo = set()
		for f in self.deletepoolfiles:
			self.enqueue(todo, f, Round.DELETES, f, None, self.doneone)
		for f in self.newpoolfiles:
			self.enqueue(todo, f, Round.POOLFILES, f, options.outdir + "/" + f, self.doneone)
		for d in self.dists.values():
			d.queue(todo, distdirs, self)
		if not self.todocount:
			# nothing to do left, mark as done:
			os.rename(self.logfile, self.donefile)
			del self.todo
		return self.todocount > 0
	def enqueue(self, dic, elem, *something):
		if elem in self.alreadydone and self.alreadydone[elem] != "asdotnew":
			if not elem in dic:
				dic[elem] = (Round.DONE,)
			return False
		elif not elem in dic:
			self.todo.add(elem)
			self.todocount += 1
			dic[elem] = something
			return True
		else:
			self.markpartial(elem, "obsoleted")
			return False
	def markpartial(self, filename, message="done"):
		if options.verbose:
			print("%s: %s" % (message, repr(filename)))
		f = open(self.logfile, "a", encoding="utf-8")
		print("DONE\t%s\t%s" % (message, filename), file=f)
		f.close()
	def doneone(self, filename, message="done"):
		assert (filename in self.todo)
		self.todo.discard(filename)
		assert (self.todocount > 0)
		self.todocount -= 1
		self.markpartial(filename, message=message)
		if self.todocount == 0:
			os.rename(self.logfile, self.donefile)
		return []


def doround(s, r, todo):
	for p,v in todo.items():
		assert (isinstance(v[0], Round))
		if v[0] != r:
			continue
		round, filename, source, donefunc = v
		if round != r:
			continue
		if source is None:
			s.start(sftp.TaskFromGenerator(deletefile(filename, donefunc)))
		else:
			s.start(sftp.TaskFromGenerator(writefile(filename, source, donefunc)))
	s.dispatch()


class Options:
	def __init__(self):
		self.verbose = None
		self.pending = False
		self.autoretry = None
		self.ignorepending = False
		self.forceorder = False
		self.confdir = None
		self.basedir = None
		self.outdir = None
		self.logdir = None
		self.debugsftp = None

options = Options()

def parseoptions(args):
	while args and args[0].startswith("--"):
		arg = args.pop(0)
		if arg == "--verbose" or arg == "-v":
			options.verbose = True
		elif arg.startswith("--debug-sftp="):
			options.debugsftp = int(arg[13:])
		elif arg == "--pending":
			options.pending = True
		elif arg == "--ignore-pending":
			options.ignorepending = True
		elif arg == "--force-order":
			options.forceorder = True
		elif arg == "--basedir=":
			options.basedir = arg[:10]
		elif arg == "--basedir":
			options.basedir = args.pop(0)
		elif arg == "--outdir=":
			options.outdir = arg[:9]
		elif arg == "--outdir":
			options.outdir = args.pop(0)
		elif arg == "--logdir=":
			options.logdir = arg[:9]
		elif arg == "--logdir":
			options.logdir = args.pop(0)
		elif arg == "--help":
			print("""outsftphook.py: an reprepro outhook example using sftp
This hook sends changed files over sftp to a remote host. It is usually put into
conf/options as outhook, but may also be called manually.
Options:
	--verbose        tell what you did
	--basedir DIR    sets the following to default values
	--outdir DIR     directory to find pool/ and dist/ directories in
	--logdir DIR     directory to check for unprocessed outlog files
	--pending        process pending files instead of arguments
	--autoretry      reprocess older pending files, too
	--ignore-pending ignore pending files
	--force-order    do not bail out if the given files are not ordered
	--debug-sftp=N   debug sftp.py (or your remote sftp server)
""")
			raise SystemExit(0)
		else:
			raise CriticalError("Unexpected command line option %s" %repr(arg))
	if options.pending and options.ignorepending:
		raise CriticalError("Cannot do both --pending and --ignore-pending")
	if options.autoretry and options.forceorder:
		raise CriticalError("Cannot do both --pending and --force-order")
	if options.autoretry and options.ignorepending:
		raise CriticalError("Cannot do both --autoretry and --ignore-pending")
	# we need confdir, logdir and outdir, if they are given, all is done
	if options.logdir is not None and options.outdir is not None and options.confdir is not None:
		return
	# otherwise it gets more complicated...
	preconfdir = options.confdir
	if preconfdir is None:
		preconfdir = os.environ.get("REPREPRO_CONFIG_DIR", None)
	if preconfdir is None:
		if options.basedir is not None:
			preconfdir = options.basedir + "/conf"
		elif "REPREPRO_BASE_DIR" in os.environ:
			preconfdir = os.environ["REPREPRO_BASE_DIR"] + "/conf"
		else:
			raise CriticalError("If not called by reprepro, please either give (--logdir and --outdir) or --basedir!")
	optionsfile = preconfdir + "/options"
	if os.path.exists(optionsfile):
		f = open(optionsfile, "r")
		for line in f:
			line = line.strip()
			if len(line) == 0 or line[0] == '#' or line[0] == ';':
				continue
			line = line.split()
			if line[0] == "basedir" and options.basedir is None:
				options.basedir = line[1]
			elif line[0] == "confdir" and options.confdir is None:
				options.confdir = line[1]
			elif line[0] == "logdir" and options.logdir is None:
				options.logdir = line[1]
			elif line[0] == "outdir" and options.outdir is None:
				options.outdir = line[1]
		f.close()
	if options.basedir is None:
		options.basedir = os.environ.get("REPREPRO_BASE_DIR", None)
	if options.outdir is None:
		if options.basedir is None:
			raise CriticalError("Need --basedir if not called by reprepro")
		options.outdir = options.basedir
	if options.logdir is None:
		if options.basedir is None:
			raise CriticalError("Need --basedir if not called by reprepro")
		options.logdir = options.basedir + "/logs"
	if options.confdir is None:
		if "REPREPRO_CONFIG_DIR" in os.environ:
			options.confdir = os.environ["REPREPRO_CONFIG_DIR"]
		else:
			if options.basedir is None:
				raise CriticalError("Need --basedir if not called by reprepro")
			options.confdir = options.basedir + "/conf"

def main(args):
	global errors, servername, username, targetdir
	if "REPREPRO_OUT_DIR" in os.environ or "REPREPRO_LOG_DIR" in os.environ:
		# assume being called by reprepro if one of those variable
		# is set, so they all should be set:
		options.outdir = os.environ["REPREPRO_OUT_DIR"]
		options.logdir = os.environ["REPREPRO_LOG_DIR"]
		options.confdir = os.environ["REPREPRO_CONFIG_DIR"]
	else:
		parseoptions(args)
	assert (options.outdir and (options.ignorepending or options.logdir) and options.confdir)
	conffilename = options.confdir + "/outsftphook.conf"
	if os.path.exists(conffilename):
		conffile = open(conffilename, "r")
		for line in conffile:
			line = line.strip().split(None, 1)
			if len(line) == 0 or line[0].startswith("#"):
				continue
			if line[0] == "servername":
				servername = line[1]
			elif line[0] == "username":
				username = line[1]
			elif line[0] == "targetdir":
				targetdir = line[1]
			elif line[0] == "debug":
				if options.debugsftp is None:
					try:
						options.debugsftp = int(line[1])
					except Exception:
						raise CriticalError(("Cannot parse %s: " +
						      "unparseable number %s") %
							    (repr(conffilename), repr(line[1])))
			elif line[0] == "verbose":
				if line[1].lower() in {'yes', 'on', '1', 'true'}:
					if options.verbose is None:
						options.verbose = True
				elif line[1].lower() in {'no', 'off', '0', 'false'}:
					if options.verbose is None:
						options.verbose = False
				else:
					raise CriticalError(("Cannot parse %s: " +
					      "unparseable truth value %s") %
						    (repr(conffilename), repr(line[1])))
			elif line[0] == "autoretry":
				if line[1].lower() in {'yes', 'on', '1', 'true'}:
					if options.autoretry is None:
						options.autoretry = True
				elif line[1].lower() in {'no', 'off', '0', 'false'}:
					if options.autoretry is None:
						options.autoretry = False
				else:
					raise CriticalError(("Cannot parse %s: " +
					      "unparseable truth value %s") %
						    (repr(conffilename), repr(line[1])))
			else:
				raise CriticalError("Cannot parse %s: unknown option %s" %
						    (repr(conffilename), repr(line[0])))
		conffile.close()
	if options.debugsftp is None:
		options.debugsftp = 0
	if targetdir and not targetdir.endswith("/"):
		targetdir = targetdir + "/"
	if not servername:
		raise CriticalError("No servername configured!")
	if not username:
		raise CriticalError("No username configured!")

	if len(args) <= 0:
		if not options.pending:
			raise CriticalError("No .outlog files given at command line!")
	else:
		if options.pending:
			raise CriticalError("--pending might not be combined with arguments!")
	if options.ignorepending:
		pendinglogs = set()
	else:
		pendinglogs = set(name for name in os.listdir(options.logdir)
		                       if name.endswith(".outlog"))
	maxbasename = None
	for f in args:
		if len(f) < 8 or f[-7:] != ".outlog":
			raise CriticalError("command line argument '%s' does not look like a .outlog file!" % f)
		bn = os.path.basename(f)
		pendinglogs.discard(bn)
		if maxbasename:
			if maxbasename < bn:
				maxbasename = bn
			elif not options.forceorder:
				raise CriticalError("The arguments are not in order (%s <= %s). Applying in this order might not be safe. (use --force-order to proceed in this order anyway)" % (bn, maxbasename))
		else:
			maxbasename = bn
	if options.pending:
		pendinglogs = sorted(pendinglogs)
	else:
		pendinglogs = sorted(filter(lambda bn: bn < maxbasename, pendinglogs))
		if pendinglogs and not options.autoretry:
			raise CriticalError("Unprocessed earlier outlogs found: %s\nYou need to process them first (or use --autoretry or autoretry true in outsftphook.conf to automatically process them)" % repr(pendinglogs))
		if pendinglogs and len(args) > 1:
			raise CriticalError("autoretry does not work with multiple log files given (yet).")
	args = list(map(lambda bn: options.logdir + "/" + bn, pendinglogs)) + args
	outlogfiles = []
	for f in args:
		donefile = f[:-7] + ".done"
		if options.verbose:
			print("Parsing '%s'" % f)
		try:
			outlogfiles.append(LogFile(f, donefile))
		except ParseError as e:
			raise CriticalError("Error parsing %s: %s" %(f, str(e)))
	todo = {}
	distdirs = {}
	workpending = False
	for o in reversed(outlogfiles):
		workpending |= o.queue(todo, distdirs)
	if not workpending:
		if options.verbose:
			print("Nothing to do")
		raise SystemExit(0)
	s = sftp.Connection(servername=servername, username=username, debug=options.debugsftp)
	doround(s, Round.POOLFILES, todo)
	if errors:
		raise SystemExit(1)
	for d in distdirs.values():
		for t in d.finalizeifready():
			s.start(t)
	doround(s, Round.DISTFILES, todo)
	if errors:
		raise SystemExit(1)
	doround(s, Round.DELETES, todo)
	if errors:
		raise SystemExit(1)

try:
	main(sys.argv[1:])
except CriticalError as e:
	print(str(e), file=sys.stderr)
	raise SystemExit(1)
