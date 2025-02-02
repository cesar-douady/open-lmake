# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

'''
	Handle local modules such that :
	-	we do not access dirs, only candidate files
	-	no caching as this is anti-lmake by nature
	-	accept to try modules in non-existent directories as they can be dynamically created
	System modules (not found in repository) and modules imported by system modules are processed as is.
	Besides that, the semantic is conserved except that :
	-	sys.path is split into sys.path for system and sys.local_path for modules in repository
	-	sys.local_path is augmented with the content of the environment variable PYTHON_LOCAL_PATH
	-	sys.import_map is augmented with the content of the environment variable PYTHON_IMPORT_MAP
	-	this split is performed at the time this module is executed, but both sys.path and sys.local_path
		may be updated afterwards, however the split semantic must be enforced (system modules in sys.path
		and repository modules in sys.local_path)
	-	sys.import_map contains a dict mapping module/package names to physical file/dir
		-	keys are names in dot notation (e.g. 'a.b')
		-	values are physical file/dir or path (e.g. 'products/a/tag/b')
	-	keys can be specialized, for example there may be an entry for 'a.b' and another for 'a.b.c'
	-	modules are seached with and without translation
		- e.g. the translated can contain computed modules and the untranslated can contain python modules
	-	for each search <original> mapped to <translated>, the following files are tried in order :
		-	<translated>.so
		-	<translated>.so.py
		-	<translated>.py
		-	<translated>/__init__.py
		-	idem with <original> instead of <translated>
		-	else, module is deemed to be a so called namespace package that need not exist on disk
	ModuleNotFoundError is reported if a namespace module is about to be returned as this is non-sens.
'''

# because this file may be exec'ed rather than imported (as the import machinery may not be reliable
# before execution), code is protected against multiple execution

import sys
import os
import site

import os.path as osp

from os import environ

def mkLocal(fileName) :
	if not fileName     : return ''
	if fileName[0]!='/' : return fileName
	# REPO_ROOT is typically set after import of this module as importer is meant to be imported very early.
	try             : root = environ['REPO_ROOT']
	except KeyError : root = os.getcwd()
	rootSlash = root+'/'
	if (fileName+'/').startswith(rootSlash) : return fileName[len(rootSlash):]
def isLocal(fileName) : return mkLocal(fileName) is not None

if 'importer_done' not in sys.__dict__ :
	sys.importer_done = True

	def fromSystemModule() :
		# determine caller of __import__
		# caller is the deepest non frozen frame other than this very module
		# as the Python import machinery is frozen
		frame = sys._getframe()
		here  = frame.f_code.co_filename
		while frame :
			fileName = frame.f_code.co_filename
			if fileName!=here and not re.match(r'^<frozen importlib\..*>$',fileName) :
				return not isLocal(fileName)
			frame = frame.f_back
		assert False,'spontaneous call to import'

	def splitPaths() :
		if not any(isLocal(f) for f in sys.path) : return # fast path
		sysPath  = sys.path
		sys.path = []
		for f in sysPath :
			lf = mkLocal(f)
			if lf is None                 : sys.path      .append(f)
			elif lf not in sys.local_path : sys.local_path.append(lf)

	if 'local_path' not in sys.__dict__ : sys.local_path = []
	localPath = environ.get('PYTHON_LOCAL_PATH')
	if localPath : # stupid split gives [''] instead of [] for an empty string
		for f in localPath.split(':') :
			sys.local_path.append(f)
	splitPaths()

	# add top level dir if not already present
	if 'import_map' not in sys.__dict__ : sys.import_map = {}
	importMap = environ.get('PYTHON_IMPORT_MAP')
	if importMap : # stupid split gives [''] instead of [] for an empty string
		for modMap in importMap.split(':') :
			name,file = modMap.split('=',1)
			sys.import_map[name] = file

	import re
	import builtins
	import importlib.machinery
	from   importlib.metadata import MetadataPathFinder

	origImport = builtins.__import__
	def __import__(name,globals=None,locals=None,fromlist=(),level=0) :
		def chkModule(m,p=False) :
			try   : l = m.__loader__ # if there is no loader, next condition does not make sense
			except: return
			if isinstance(l,NamespaceLoader) :
				raise ModuleNotFoundError('module %s not found\nglobal path : %s\nlocal path : %s'%(
					m
				,	sys.path
				,	sys.local_path
				))
		splitPaths()
		mod = origImport(name,globals,locals,fromlist,level)
		# catch return namespace modules as these are unusable in code and mask ModuleNotFoundError's
		# in case of attribute error, it means we do not recognize a namespace module, and we let go
		# be careful that for the last form, we must test all values, so we cannot put a global try/except
		if not fromlist : # form : import foo.bar.baz, mod is foo and we must verify baz is not a namespace module
			v = mod
			try :
				for n in name.split('.')[1:] : v = getattr(v,n)
				chkModule(v) # v is necessarily a module
			except AttributeError : pass
		elif fromlist[0]=='*' : # form : from foo.bar.baz import *, mod is baz and we must verify it is not a namespace module
			try                   : chkModule(mod)
			except AttributeError : pass
		else : # form : from foo.bar import baz,zee, mod is bar and we must verify baz nor zee are namespace modules
			for n in fromlist :
				try                   : chkModule(getattr(mod,n)) # if v does not exist, this is not really our problem
				except AttributeError : pass
		return mod
	builtins.__import__ = __import__

	# reimplement import_module to call __import__ if called from user code (dont touch system code, it is too complex)
	origImportModule = importlib.import_module
	def import_module(name,package=None) :
		if fromSystemModule() : return origImportModule(name,package)
		for lvl in range(len(name)) :
			if name[lvl]!='.' : break
		else : raise NameError('cannot import module whose name is only dots')
		if lvl :
			if not package : raise TypeError('cannot do relative import of %s without a package'%name)
			bits = package.rsplit('.',lvl-1)
			if len(bits)<lvl : raise ValueError('attempted relative import beyond top-level package')
			name = '%s.%s'%(bits[0],name[lvl:])
		mod = __import__(name)
		for n in name.split('.')[1:] : mod = getattr(mod,n)
		return mod
	importlib.import_module = import_module

	origAddSiteDir = site.addsitedir
	def addsitedir(sitedir,known_paths=None) :
		if isLocal(sitedir) : # avoid reading sitedir if local
			sys.path.append(sitedir)
			splitPaths()
		else : # fall back to normal processing if not local
			origAddSiteDir(sitedir,known_paths)
	site.addsitedir = addsitedir # replace with equivalent while preventing readir in repository

	class Loader :
		def create_module(self,spec): pass
	class PyLoader(Loader) :
		def __init__(self,name,srcFile,base=None) :
			self.name    = name
			self.srcFile = srcFile
			self.base    = base
		def exec_module(self,module) :
			src  = open(self.srcFile,'r').read()
			code = compile(src,self.srcFile,'exec',dont_inherit=True,optimize=-1)
			if self.base :
				try                   : module.__path__
				except AttributeError : module.__path__ = (self.base,)
			module.__file__ = self.srcFile
			exec(code,module.__dict__)
	class NamespaceLoader(Loader) :
		def __init__(self,search) :
			self.search = search
		def exec_module(self,module) :
			module.__path__ = self.search
			module.__file__ = None
	SoLoader = importlib.machinery.ExtensionFileLoader # inherit from machinery if something special is needed

	class Spec :
		submodule_search_locations = None
		cached                     = None
		def __init__(self,name,**kwds) :
			self.name = name
			for k,v in kwds.items() : setattr(self,k,v)
			if self.submodule_search_locations : self.parent = name
			else                               : self.parent = name.rsplit('.',1)[0]
			self.has_location = 'origin' in kwds

	class Importer(MetadataPathFinder):
		'''
			called before and after normal import processing
			- for modules imported from system modules, process normally, masking access to local directories
			- for modules within packages, try to find local modules *before* normal processing
			- for top level modules      , try to find local modules *after * normal processing
		'''
		def __init__(self,metaPath) :
			self.metaPath = metaPath
		def bases(self,name,path) :
			mapPath = sys.import_map.get(name,())
			if isinstance(mapPath,str) :
				yield mapPath
			else :
				for f in mapPath : yield f
			name = name.rsplit('.')
			nonLocal = False
			for f in path :
				if isLocal(f) : yield osp.join(f,name[-1])
				else          : nonLocal = True # use system procedures for system sources
			if nonLocal : yield False # do not look further locally if a system alternative is considered
			for f in sys.local_path :
				yield osp.join(f,*name)
		def findLocalSpec(self,name,path=()) :
			root  = environ.get('REPO_ROOT','')
			bases = []
			for base in self.bases(name,path) :
				if base in bases : continue
				if base is False : break
				for suffix,isPkg,loader in sys.import_policy :
					src = osp.join(root,base+suffix) # beware that cwd may not be REPO_ROOT, but in that case, REPO_ROOT should be set
					try    : open(src,'r')           # trigger dependency
					except : continue
					if isPkg :
						return Spec(name
						,	origin                     = src
						,	loader                     = loader(name,src,base)
						,	submodule_search_locations = (base,)
						)
					else :
						return Spec(name
						,	origin = src
						,	loader = loader(name,src)
						)
				bases.append(base)
			else :
				# defaults to namespace loader if nothing found
				return Spec(name
				,	origin                     = None
				,	loader                     = NamespaceLoader(bases)
				,	submodule_search_locations = bases
				)
		def find_spec(self,name,path,target) :
			spec = None
			if path and not fromSystemModule() :
				spec = self.findLocalSpec(name,path)
				if spec : return spec
			for importer in self.metaPath :
				try :
					spec = importer.find_spec(name,path,target)
					if spec : return spec
				except AttributeError :
					loader = importer.find_module(name,path)
					if loader : return importlib.util.spec_from_module(name,loader)
			if not path and not fromSystemModule() :
				spec = self.findLocalSpec(name)
				if spec : return spec

	sys.import_policy = [ # use a list rather than a tuple so it can easily be extended
	#	 suffix         isPkg loader
		('.so'         ,False,SoLoader)
	,	('.py'         ,False,PyLoader)
	,	('/__init__.py',True ,PyLoader)
	]
	# if within a package, we must pass before regular importers
	# to prevent uncontrolled filesystem accesses
	# this is harmless as we ignore recognized system modules and imports from system modules
	# else, we must pass after to prevent too many dependencies
	# inside the repository when importing system modules
	sys.meta_path = [
		sys.meta_path[0]      # builtin modules
	,	Importer(sys.meta_path[1:])
	]
