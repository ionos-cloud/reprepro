# -*- coding: utf-8 -*-
#
# Copyright 2013 Bernhard R. Link <brlink@debian.org>
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# SOFTWARE IN THE PUBLIC INTEREST, INC. BE LIABLE FOR ANY CLAIM, DAMAGES OR
# OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
# ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.
#

"""
This is a sftp module to be used in reprepro's outsftphook example.
Like the sftp binary it calls ssh to do the connection in a secure
way and then speaks the sftp subsystem language over that connection.
"""


import os, subprocess, select

class EnumInternException(Exception):
	def __init__(self, v):
		super().__init__(v)
		self.value = v

class _EnumType(type):
	"""
	Metaclass for Enum. Allows one to set values as parameters.
	"""
	def __new__(cls, name, bases, namespace, **values):
		return type.__new__(cls, name, bases, namespace)
	def __init__(self, name, bases, namespace, **values):
		super().__init__(name, bases, namespace)
		if bases:
			self._byvalue = dict()
			self._byname = dict()
		if values:
			for k,v in values.items():
				self._create_instance(k, v)

class Enum(metaclass=_EnumType):
	"""
	An enum is a class with a fixed set of instances.
	Each instance has a name and a integer value.
	If a new new instance is to be created, one of those
	fix instances is returned instead.
	"""
	@classmethod
	def _create_instance(cls, name, value):
		# create a new instance:
		result = super(Enum, cls).__new__(cls)
		if isinstance(name, str):
			result.name = name
		else:
			result.name = name[0]
		result.__name__ = result.name
		result.value = value
		cls._byvalue[value] = result
		if isinstance(name, str):
			cls._byname[name] = result
			setattr(cls, name, result)
		else:
			for n in name:
				cls._byname[n] = result
				setattr(cls, n, result)
		return result
	def __new__(cls, l):
		try:
			if isinstance(l, cls):
				return l
			elif isinstance(l, int):
				return cls._byvalue[l]
			elif isinstance(l, str):
				return cls._byname[l]
			else:
				raise EnumInternException(repr(l))
		except KeyError:
			raise EnumInternException(repr(l))
	def __init__(self, l):
		pass
	def __int__(self):
		return self.value
	def __str__(self):
		return self.name
	def __repr__(self):
		return "%s.%s.%s" % (type(self).__module__, type(self).__name__, self.name)

class _BitmaskType(type):
	"""
	Metaclass for Bitmask types. Allows one to set values as parameters.
	"""
	@classmethod
	def __prepare__(cls, name, bases, **values):
		namespace = type.__prepare__(cls, name, bases)
		if values:
			flagcls = _EnumType.__new__(type, "flags of " + name, (Enum,), dict())
			flagcls._byvalue = dict()
			flagcls._byname = dict()
			namespace["_Values"] = flagcls
			for (k,v) in values.items():
				if isinstance(v, int):
					e = flagcls._create_instance(k, v)
					e.mask = v
				else:
					e = flagcls._create_instance(k, v[0])
					e.mask = v[1]
				namespace[k] = e
		return namespace
	def __new__(cls, name, bases, namespace, **values):
		return type.__new__(cls, name, bases, namespace)
	def __init__(self, name, bases, namespace, **values):
		return super().__init__(name, bases, namespace)

class Bitmask(set, metaclass=_BitmaskType):
	def __init__(self, l):
		if isinstance(l, int):
			super().__init__([i
				for (k,i)
				in self._Values._byvalue.items()
				if (l & i.mask) == k])
			if l != int(self):
				raise Exception("Unrepresentable number %d (got parsed as %s = %d)" %
				                (l, str(self), int(self)))
		elif isinstance(l, str):
			try:
				super().__init__([self._Values(i)
					for i
					in l.split("|")])
				# test for inconsistencies:
				type(self)(int(self))
			except EnumInternException as e:
				raise Exception("Invalid value '%s' in value '%s' for %s" %
				                (e.value, str(l), type(self).__name__))
		else:
			try:
				super().__init__([self._Values(i) for i in l])
				# test for inconsistencies:
				type(self)(int(self))
			except EnumInternException as e:
				raise Exception("Invalid value '%s' in value '%s' for %s" %
				                (e.value, str(l), type(self).__name__))
	def __int__(self):
		v = 0
		for i in self:
			v = v | int(i)
		return v
	def __str__(self):
		return "|".join([str(i) for i in self])

class SSH_FILEXFER(Bitmask, ATTR_SIZE = 0x00000001,
                            ATTR_UIDGID = 0x00000002,
                            ATTR_PERMISSIONS = 0x00000004,
                            ATTR_ACMODTIME = 0x00000008,
                            ATTR_EXTENDED = 0x80000000):
	pass

def ssh_data(b):
	return len(b).to_bytes(4, byteorder='big') + b
def ssh_string(s):
	b = str(s).encode(encoding='utf-8')
	return len(b).to_bytes(4, byteorder='big') + b
def ssh_u8(i):
	return int(i).to_bytes(1, byteorder='big')
def ssh_u32(i):
	return int(i).to_bytes(4, byteorder='big')
def ssh_u64(i):
	return int(i).to_bytes(8, byteorder='big')
def ssh_attrs(**opts):
	flags = SSH_FILEXFER(0)
	extended = []
	for key in opts:
		if key == 'size':
			flags.add(SSH_FILEXFER.ATTR_SIZE)
		elif key == 'uid' or key == 'gid':
			flags.add(SSH_FILEXFER.ATTR_UIDGID)
		elif key == 'permissions':
			flags.add(SSH_FILEXFER.ATTR_PERMISSIONS)
		elif key == 'atime' or key == 'mtime':
			flags.add(SSH_FILEXFER.ATTR_ACMODTIME)
		elif '@' in key:
			extended.add(opts[key])
		else:
			raise SftpException("Unsupported file attribute type %s" % repr(key))
	if extended:
		flags.add(SSH_FILEXFER.ATTR_EXTENDED)
	b = ssh_u32(int(flags))
	if SSH_FILEXFER.ATTR_SIZE in flags:
		b = b + ssh_u64(opts['size'])
	if SSH_FILEXFER.ATTR_UIDGID in flags:
		b = b + ssh_u32(opts['uid'])
		b = b + ssh_u32(opts['gid'])
	if SSH_FILEXFER.ATTR_PERMISSIONS in flags:
		b = b + ssh_u32(opts['permissions'])
	if SSH_FILEXFER.ATTR_ACMODTIME in flags:
		b = b + ssh_u32(opts['atime'])
		b = b + ssh_u32(opts['mtime'])
	if SSH_FILEXFER.ATTR_EXTENDED in flags:
		b = b + ssh_u32(len(extended))
		for key in extended:
			b = b + ssh_string(key)
			b = b + ssh_data(opts[key])
	return b

def ssh_getu32(m):
	v = int.from_bytes(m[:4], byteorder='big')
	return v, m[4:]
def ssh_getstring(m):
	l = int.from_bytes(m[:4], byteorder='big')
	return (m[4:4+l].decode(encoding='utf-8'), m[4+l:])
def ssh_getdata(m):
	l = int.from_bytes(m[:4], byteorder='big')
	return (m[4:4+l], m[4+l:])
def ssh_getattrs(m):
	attrs = dict()
	flags, m = ssh_getu32(m)
	flags = SSH_FILEXFER(flags)
	if SSH_FILEXFER.ATTR_SIZE in flags:
		attrs['size'], m = ssh_getu64(m)
	if SSH_FILEXFER.ATTR_UIDGID in flags:
		attrs['uid'], m = ssh_getu32(m)
		attrs['gid'], m = ssh_getu32(m)
	if SSH_FILEXFER.ATTR_PERMISSIONS in flags:
		attrs['permissions'], m = ssh_getu32(m)
	if SSH_FILEXFER.ATTR_ACMODTIME in flags:
		attrs['atime'], m = ssh_getu32(m)
		attrs['mtime'], m = ssh_getu32(m)
	if SSH_FILEXFER.ATTR_EXTENDED in flags:
		count, m = ssh_getu32(m)
		while count > 0:
			count -= 1
			key, m = ssh_getstring(m)
			attrs[key], m = ssh_getdata(m)
	return (attrs, m)

class SftpException(Exception):
	pass

class SftpStrangeException(SftpException):
	"""Unparseable stuff from server"""
	pass

class SftpUnexpectedAnswerException(SftpStrangeException):
	def __init__(self, answer, request):
		super().__init__("Unexpected answer '%s' to request '%s'" %
		                 (str(answer), str(request)))

class SftpTooManyRequestsException(SftpException):
	def __init__(self):
		super().__init__("Too many concurrent requests (out of request ids)")

class SftpInternalException(SftpException):
	"""a programming or programmer mistake"""
	pass

class Request:
	def __init__(self, **args):
		self.data = args
		pass
	def __int__(self):
		return self.requestid
	def __str__(self):
		return type(self).__name__ + "(" + " ".join(["%s=%s" % (key, repr(val))
			for (key, val) in self.data.items()]) + ")"
	@classmethod
	def bin(cls, conn, req, *payload):
		s = 5
		for b in payload:
			s = s + len(b)
		# print("Sending packet of type %d and size %d" % (cls.typeid, s))
		r = ssh_u32(s) + ssh_u8(cls.typeid) + ssh_u32(int(req))
		for b in payload:
			r = r + b
		return r
	def send(self, conn):
		conn.requests[int(self)] = self
		self.conn = conn
		conn.send(self.bin(conn, self, **self.data))
	def done(self):
		if self.requestid != None:
			del self.conn.requests[self.requestid]
			self.requestid = None

class NameRequest(Request):
	"""Base class for requests with a single name as argument"""
	def __init__(self, name):
		super().__init__(name = name)
	@classmethod
	def bin(cls, conn, req, name):
		return super().bin(conn, req, ssh_string(name))

class HandleRequest(Request):
	"""Base class for requests with a single name as argument"""
	def __init__(self, handle):
		super().__init__(handle = handle)
	@classmethod
	def bin(cls, conn, req, handle):
		return super().bin(conn, req, ssh_data(handle))

class NameAttrRequest(Request):
	"""Base class for requests with a name and attributes as argument"""
	def __init__(self, name, **attrs):
		super().__init__(name = name, attrs = attrs)
	@classmethod
	def bin(cls, conn, req, name, attrs):
		return super().bin(conn, req,
			ssh_string(name),
			ssh_attrs(**attrs))

class INIT(Request):
	typeid = 1
	@classmethod
	def bin(cls, conn, version):
		# INIT has no request id but instead sends a protocol version
		return super().bin(conn, int(version))

class SSH_FXF(Bitmask, READ =    0x00000001,
                       WRITE =   0x00000002,
                       APPEND =  0x00000004,
                       CREAT =   0x00000008,
                       TRUNC =   0x00000010,
                       EXCL =    0x00000020):
	pass

class OPEN(Request):
	typeid = 3
	def __init__(self, name, flags, **attributes):
		super().__init__(name = name, flags = SSH_FXF(flags), attrs = attributes)
	@classmethod
	def bin(cls, conn, req, name, flags, attrs):
		return super().bin(conn, req,
			ssh_string(name),
			ssh_u32(flags),
			ssh_attrs(**attrs))

class CLOSE(HandleRequest):
	typeid = 4

class READ(Request):
	typeid = 5
	def __init__(self, handle, start, length):
		super().__init__(handle = handle, start = start, length = int(length))
	@classmethod
	def bin(cls, conn, req, handle, start, length):
		return super().bin(conn, req, ssh_data(handle), ssh_u64(start), ssh_u32(length))

class WRITE(Request):
	typeid = 6
	def __init__(self, handle, start, data):
		super().__init__(handle = handle, start = start, data = bytes(data))
	@classmethod
	def bin(cls, conn, req, handle, start, data):
		return super().bin(conn, req, ssh_data(handle), ssh_u64(start), ssh_data(data))

class LSTAT(NameRequest):
	typeid = 7

class FSTAT(HandleRequest):
	typeid = 8

class SETSTAT(NameAttrRequest):
	typeid = 9

class FSETSTAT(Request):
	typeid = 10
	def __init__(self, handle, **attrs):
		super().__init__(handle = handle, attrs = attrs)
	@classmethod
	def bin(cls, conn, req, name, attrs):
		return super().bin(conn, req,
			ssh_data(handle),
			ssh_attrs(**attrs))

class OPENDIR(NameRequest):
	typeid = 11

class READDIR(HandleRequest):
	typeid = 12

class REMOVE(NameRequest):
	typeid = 13

class MKDIR(NameAttrRequest):
	typeid = 14

class RMDIR(NameRequest):
	typeid = 15

class REALPATH(NameRequest):
	typeid = 16

class STAT(NameRequest):
	typeid = 17

class SSH_FXF_RENAME(Bitmask, OVERWRITE = 0x00000001,
                              ATOMIC    = 0x00000002,
                              NATIVE    = 0x00000004):
	pass

class RENAME(Request):
	typeid = 18
	def __init__(self, src, dst, flags):
		if not isinstance(flags, SSH_FXF_RENAME):
			flags = SSH_FXF_RENAME(flags)
		super().__init__(src = src, dst = dst, flags = flags)
	@classmethod
	def bin(cls, conn, req, src, dst, flags):
		# TODO: Version 3 has no flags (though they do not seem to harm)
		return super().bin(conn, req, ssh_string(src),
			ssh_string(dst), ssh_u32(flags))

class READLINK(NameRequest):
	typeid = 19

class SYMLINK(Request):
	typeid = 20
	def __init__(self, name, dest):
		super().__init__(name = name, dest = dest)
	@classmethod
	def bin(cls, conn, req, name, dest):
		# TODO: this is openssh and not the standard (they differ)
		return super().bin(conn, req, ssh_string(dest),
			ssh_string(name))

class EXTENDED(Request):
	typeid = 200
	# TODO?

################ Answers ################

class Answer:
	def __int__(self):
		return self.id
	# Fallbacks, can be removed once all are done:
	def __init__(self, m):
		self.data =  m
	def __str__(self):
		return "%s %s" % (type(self).__name__, repr(self.data))

class VERSION(Answer):
	id = 2

class SSH_FX(Enum,
	OK =                   0,
	EOF =                  1,
	NO_SUCH_FILE =         2,
	PERMISSION_DENIED =    3,
	FAILURE =              4,
	BAD_MESSAGE =          5,
	NO_CONNECTION =        6,
	CONNECTION_LOST =      7,
	OP_UNSUPPORTED =       8,
	INVALID_HANDLE =       9,
	NO_SUCH_PATH =        10,
	FILE_ALREADY_EXISTS = 11,
	WRITE_PROTECT =       12,
	NO_MEDIA =            13
):
	pass

class STATUS(Answer):
	id = 101
	def __init__(self, m):
		s, m = ssh_getu32(m)
		self.status = SSH_FX(s)
		self.message, m = ssh_getstring(m)
		self.lang, m = ssh_getstring(m)
	def __str__(self):
		return "STATUS %s: %s[%s]" % (
			str(self.status),
			self.message,
			self.lang)

class HANDLE(Answer):
	id = 102
	def __init__(self, m):
		self.handle, m = ssh_getdata(m)
	def __str__(self):
		return "HANDLE %s" % repr(self.handle)

class DATA(Answer):
	id = 103
	def __init__(self, m):
		self.data, m = ssh_getdata(m)
	def __str__(self):
		return "DATA %s" % repr(self.data)

class NAME(Answer):
	id = 104
	def __init__(self, m):
		count, m = ssh_getu32(m)
		self.names = []
		while count > 0:
			count -= 1
			filename, m = ssh_getstring(m)
			longname, m = ssh_getstring(m)
			attrs, m = ssh_getattrs(m)
			self.append((filename, longname, attrs))

	def __str__(self):
		return "NAME" + "".join(("%s:%s:%s" % (repr(fn), repr(ln), str(attrs))
		                         for (fn,ln,attrs) in self.names))

class ATTRS(Answer):
	id = 105
	def __init__(self, m):
		self.attrs, m = ssh_getattrs(m)

	def __str__(self):
		return "ATTRS %s" % str(self.attrs)

class EXTENDED_REPLY(Answer):
	id = 201
	# TODO?

################ Tasks ################

class Task:
	"""A task is everything that sends requests,
	   receives answers, uses collectors or is
	   awakened by collectors.
	"""
	def start(self, connection):
		self.connection = connection
	def enqueueRequest(self, request):
		request.task = self
		self.connection.enqueueRequest(request)
	def sftpanswer(self, a):
		raise SftpInternalException("unimplemented sftpanswer called")
	def writeready(self):
		raise SftpInternalException("unimplemented writeready called")
	def parentinfo(self, command):
		raise SftpInternalException("unimplemented parentinfo called")

class TaskFromGenerator(Task):
	"""A wrapper around a python corotine (generator)"""
	def __init__(self, gen):
		super().__init__()
		self.gen = gen
	def start(self, connection):
		super().start(connection)
		self.enqueue(next(self.gen))
	def parentinfo(self, command):
		self.enqueue(self.gen.send(command))
	def sftpanswer(self, answer):
		self.enqueue(self.gen.send(answer))
	def writeready(self):
		self.enqueue(self.gen.send('canwrite'))
	def __str__(self):
		return "Task(by %s)" % self.gen
	def enqueue(self, joblist):
		if len(joblist) == 0:
			return
		for job in joblist:
			if isinstance(job, Request):
				self.enqueueRequest(job)
			elif job == 'wantwrite':
				self.connection.enqueueTask(self)
			elif (isinstance(job, tuple) and len(job) == 2 and
			     isinstance(job[0], Task)):
				if DebugMode.LOCKS in self.debug:
					print("parentinfo", job,
					       **self.debugopts)
				job[0].parentinfo(job[1])
			elif (isinstance(job, tuple) and len(job) >= 2 and
			     issubclass(job[1], Collector)):
				self.connection.collect(self, *job)
			elif isinstance(job, Task):
				self.connection.start(job)
			else:
				raise SftpInternalException("strange result from generator")


class Collector(Task):
	""" Collectors collect information from Tasks and send them
	    triggers at requested events (parent directory created,
	    another file can be processed, ...)
	"""
	def childinfo(self, who, command):
		raise SftpInternalException("unimplemented parentinfo called")

class DebugMode(Bitmask, **{
	'COOKED_IN':    1,
	'COOKED_OUT':   2,
	'RAW_IN_STAT':  4,
	'RAW_OUT_STAT': 8,
	'RAW_IN':      16,
	'RAW_OUT':     32,
	'ENQUEUE':     64,
	'LOCKS':      128,
}):
	pass

class Connection:
	def next_request_id(self):
		i = self.requestid_try_next
		while i in self.requests:
			i = (i + 1) % 0x100000000
			if i == self.requestid_try_next:
				raise SftpTooManyRequestsException()
		self.requestid_try_next = (i + 1) % 0x100000000
		return i
	def __init__(self, servername, sshcommand="ssh", username=None, ssh_options=[], debug=0, debugopts=dict(), maxopenfiles=10):
		self.debug = DebugMode(debug)
		self.debugopts = debugopts
		self.requests = dict()
		self.collectors = dict()
		self.queue = list()
		self.wantwrite = list()
		self.requestid_try_next = 17
		self.semaphores = {'openfile': maxopenfiles}

		commandline = [sshcommand]
		if ssh_options:
			commandline.extend(ssh_options)
		# those defaults are after the user-supplied ones so they can be overridden.
		# (earlier ones win with ssh).
		commandline.extend(["-oProtocol 2", # "-oLogLevel DEBUG",
		                    "-oForwardX11 no", "-oForwardAgent no",
		                    "-oPermitLocalCommand no",
		                    "-oClearAllForwardings yes"])
		if username:
			commandline.extend(["-l", username])
		commandline.extend(["-s", "--", servername, "sftp"])
		self.connection = subprocess.Popen(commandline,
		                                   close_fds = True,
		                                   stdin = subprocess.PIPE,
		                                   stdout = subprocess.PIPE,
		                                   bufsize = 0)
		self.poll = select.poll()
		self.poll.register(self.connection.stdout, select.POLLIN)
		self.inbuffer = bytes()
		self.send(INIT.bin(self, 3))
		t,b = self.getpacket()
		if t != VERSION.id:
			raise SftpUnexpectedAnswerException(b, "INIT")
		# TODO: parse answer data (including available extensions)
	def close(self):
		self.connection.send_signal(15)
	def getmoreinput(self, minlen):
		while len(self.inbuffer) < minlen:
			o = self.connection.stdout.read(minlen - len(self.inbuffer))
			if o == None:
				continue
			if len(o) == 0:
				raise SftpStrangeException("unexpected EOF")
			self.inbuffer = self.inbuffer + o
	def getpacket(self):
		self.getmoreinput(5)
		s = int.from_bytes(self.inbuffer[:4], byteorder='big')
		if s < 1:
			raise SftpStrangeException("Strange size field in Paket from server!")
		t = self.inbuffer[4]
		if DebugMode.RAW_IN_STAT in self.debug:
			print("receiving packet of length %d and type %d " %
			      (s, t), **self.debugopts)
		s = s - 1
		self.inbuffer = self.inbuffer[5:]
		self.getmoreinput(s)
		d = self.inbuffer[:s]
		self.inbuffer = self.inbuffer[s:]
		if DebugMode.RAW_IN in self.debug:
			print("received packet(type %d):" % t, repr(d),
			      **self.debugopts)
		return (t, d)
	def send(self, b):
		if not isinstance(b, bytes):
			raise SftpInternalException("send not given byte sequence")
		if DebugMode.RAW_OUT_STAT in self.debug:
			print("sending packet of %d bytes" % len(b),
			      **self.debugopts)
		if DebugMode.RAW_OUT in self.debug:
			print("sending packet:", repr(b),
			      **self.debugopts)
		self.connection.stdin.write(b)
	def enqueueRequest(self, job):
		if DebugMode.ENQUEUE in self.debug:
			print("enqueue", job,
			       **self.debugopts)
		if len(self.queue) == 0 and len(self.wantwrite) == 0:
			self.poll.register(self.connection.stdin,
					   select.POLLOUT)
		job.requestid = self.next_request_id()
		self.queue.append(job)
	def enqueueTask(self, task):
		if DebugMode.ENQUEUE in self.debug:
			print("enqueue", task, **self.debugopts)
		if len(self.queue) == 0 and len(self.wantwrite) == 0:
			self.poll.register(self.connection.stdin,
					   select.POLLOUT)
		self.wantwrite.append(task)
	def collect(self, who, command, collectortype, *collectorargs):
		if DebugMode.LOCKS in self.debug:
			print("collector", command, collectortype.__name__,
				*collectorargs, **self.debugopts)
		"""Tell the (possibly to be generated) """
		collectorid = (collectortype, collectorargs)
		if not collectorid in self.collectors:
			l = collectortype(*collectorargs)
			self.collectors[collectorid] = l
			l.start(self)
		else:
			l = self.collectors[collectorid]
		l.childinfo(who, command)
	def start(self, task):
		task.start(self)
	def dispatchanswer(self, answer):
		task = answer.forr.task
		try:
			task.sftpanswer(answer)
		except StopIteration:
			orphanreqs = [ r
				for r in self.requests.values()
				if r.task == task ]
			for r in orphanreqs:
				r.done()
	def readdata(self):
		t,m = self.getpacket()
		for answer in Answer.__subclasses__():
			if t == answer.id:
				break
		else:
			raise SftpUnexpectedAnswerException("Unknown answer type %d" % t, "")
		id, m = ssh_getu32(m)
		a = answer(m)
		if DebugMode.COOKED_IN in self.debug:
			print("got answer for request %d: %s" %
			      (id, str(a)), **self.debugopts)
		if not id in self.requests:
			raise SftpUnexpectedAnswerException(a, "unknown-id-%d" % id)
		else:
			a.forr = self.requests[id]
			self.dispatchanswer(a)
	def senddata(self):
		if len(self.queue) == 0:
			while len(self.wantwrite) > 0:
				w = self.wantwrite.pop(0)
				if len(self.wantwrite) == 0 and len(self.queue) == 0:
					self.poll.unregister(self.connection.stdin)
				w.writeready()
		if len(self.queue) > 0:
			request = self.queue.pop(0)
			if len(self.queue) == 0 and len(self.wantwrite) == 0:
				self.poll.unregister(self.connection.stdin)
			if DebugMode.COOKED_OUT in self.debug:
				print("sending request %d: %s" %
				      (request.requestid, str(request)),
				      **self.debugopts)
			request.send(self)
	def dispatch(self):
		while self.requests or self.queue:
			for (fd, event) in self.poll.poll():
				if event == select.POLLIN:
					self.readdata()
				elif event == select.POLLHUP:
					raise SftpStrangeException(
					      "Server disconnected unexpectedly"
					      " or ssh client process terminated")
				elif event == select.POLLOUT:
					self.senddata()
				else:
					raise SftpException("Unexpected event %d from poll" % event)

class Dirlock(Collector):
	def __init__(self, name):
		super().__init__()
		self.name = name
		self.dirname = os.path.dirname(name)
		self.queue = []
	def start(self, connection):
		super().start(connection)
		if self.dirname and (self.name != self.dirname):
			self.mode = "wait-for-parent"
			self.connection.collect(self, 'waitingfor',
			                        Dirlock, self.dirname)
		else:
			self.tellparent = False
			self.mode = "wait-for-client"
			self.isnew = False
	def sftpanswer(self, a):
		assert(self.mode == "creating")
		if not isinstance(a, STATUS):
			raise SftpUnexpectedAnswer(a, a.forr)
		# Only one answer is expected:
		a.forr.done()
		if a.status == SSH_FX.OK:
			self.mode = "exists"
			self.isnew = True
			self.releaseallqueued()
		elif self.tellparent and a.status == SSH_FX.NO_SUCH_FILE:
			self.mode = "wait-for-parent"
			self.connection.collect(self, 'missing',
			                        Dirlock, self.dirname)
		else:
			raise SftpException("Cannot create directory %s: %s" % (self.name, a))
	def parentinfo(self, command):
		assert(self.mode == "wait-for-parent")
		if command == "createnew":
			self.tellparent = False
			self.isnew = True
			self.createdir()
			return
		if command != "tryandtell" and command != "ready":
			raise SftpInternalException(
			      "Unexpected parent info %s" %
			      command)
		self.tellparent = command == "tryandtell"
		if len(self.queue) > 0:
			self.mode = "testing"
			self.queue.pop(0).parentinfo("tryandtell")
		else:
			self.mode = "wait-for-client"
	def childinfo(self, who, command):
		if command == "waitingfor":
			if self.mode == "exists":
				if self.isnew:
					who.parentinfo("createnew")
				else:
					who.parentinfo("ready")
			elif self.mode == "wait-for-client":
				self.mode = "testing"
				who.parentinfo("tryandtell")
			else:
				self.queue.append(who)
		elif command == "found":
			assert(self.mode == "testing")
			self.mode = "exists"
			self.isnew = False
			self.releaseallqueued()
		elif command == "missing":
			self.queue.append(who)
			self.mode = "creating"
			self.createdir()
		else:
			raise SftpInternalException(
			      "Unexpected child information: %s" %
			      command)
	def createdir(self):
		self.mode = "creating"
		self.enqueueRequest(MKDIR(self.name))
	def releaseallqueued(self):
		if self.tellparent:
			self.connection.collect(self, 'found',
			                        Dirlock, self.dirname)
			self.tellparent = False
		if self.isnew:
			command = "createnew"
		else:
			command = "ready"
		# This assumes out mode cannot change any more:
		while self.queue:
			self.queue.pop(0).parentinfo(command)

class Semaphore(Collector):
	def __init__(self, name):
		super().__init__()
		self.name = name
		self.queue = []
		self.allowed = 10
	def start(self, connection):
		self.allowed = connection.semaphores[self.name]
	def childinfo(self, who, command):
		if command == "lock":
			if self.allowed > 0:
				self.allowed -= 1
				who.parentinfo("unlock")
			else:
				self.queue.append(who)
		elif command == "release":
			if self.allowed == 0 and self.queue:
				self.queue.pop(0).parentinfo("unlock")
			else:
				self.allowed += 1
		else:
			raise SftpInternalException("Semaphore.childinfo called with invalid command")
