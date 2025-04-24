# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2025 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

import sys

import os
import os.path    as osp
import re
import shutil
import subprocess as sp

gcov = os.environ['GCOV']

summary       = sys.argv[1 ]
srcs          = sys.argv[2:]
out_dir       = osp.dirname(summary)
gcov_dir      = osp.join(out_dir,'gcov_dir')
anti_gcov_dir = '/'.join('..' for c in gcov_dir.split('/'))

file_re         = re.compile(r"^File '(?P<file>.*)'$"                                                        )
lines_re        = re.compile(r'^(Lines executed:(?P<cov>[\d.]*)% of (?P<n_lines>[\d]*)|No executable lines)$')
dashes_re       = re.compile(r'^-+$'                                                                         )
skip_re         = re.compile(r'^\w+:$'                                                                       )
cov_re          = re.compile(r'^(?P<cov>[^:]*):(?P<line_no>[^:]*):(?P<line>.*)$'                             )
no_cov_start_re = re.compile(r'^.*//.*\b(START_OF_)?NO_COV\b.*$'                                             ) # NO_COV as a word matches both start and end, so it is a standalone marker
no_cov_end_re   = re.compile(r'^.*//.*\b(END_OF_)?NO_COV\b.*$'                                               ) # .

try                      : shutil.rmtree(gcov_dir)
except FileNotFoundError : pass
os.makedirs(gcov_dir)
for f in ('src','ext','sys_config.h','version.hh') :
	os.symlink(f'{anti_gcov_dir}/{f}',f'{gcov_dir}/{f}')
gcov_out = sp.check_output( (gcov,'-p','-r',*srcs) , universal_newlines=True , cwd=gcov_dir )

file_tab = {}
file     = None
for l in gcov_out.split('\n') :
	m = file_re.match(l)
	if m :
		assert file==None
		file = m.group('file')
	m = lines_re.match(l)
	if not m                                          : continue
	if file==None                                     : continue              # not interested in global summary
	if file.startswith('src/') and m.group('n_lines') : file_tab[file] = None # only interested in info with src
	if True                                           : file = None

for f in file_tab.keys() :
	gcov_f = f"{gcov_dir}/{f.replace('/','#')}.gcov"
	out_f  = f'{out_dir}/{f}'
	d = osp.dirname(out_f)
	if d : os.makedirs(d,exist_ok=True)
	nuc = 0
	nc  = 0
	try :
		gcov_fd = open(gcov_f)
	except FileNotFoundError :
		file_tab[f] = (len(open(f)),0) # if not executed, consider all lines as uncovered
		continue
	with open(out_f,'w') as file_fd :
		seen_dashes = False
		skip        = False
		first       = True
		no_cov      = 0
		for l in gcov_fd :             # suppress lines refering to instantiated templates : lines starting with a label: surrounded by dashes
			if first :
				first = False
				continue               # skip first line providing useless source info so that line numbers correspond
			if seen_dashes :
				skip = skip_re.match(l)
			seen_dashes = dashes_re.match(l)
			if seen_dashes :
				seen_dashes = True
				continue
			if skip : continue
			m = cov_re.match(l)
			assert m
			cov  = m.group('cov' ).strip()
			line = m.group('line')
			no_cov += bool(no_cov_start_re.match(l))
			if   no_cov                     : cov = 'X'
			elif not line or line.isspace() : cov = ''
			elif cov :
				if cov.endswith('*') : cov = cov[:-1]
				c = cov[-1]
				if   c=='=' : cov = '-'
				elif c=='#' : nuc += 1
				elif c=='-' : pass
				elif c in '0123456789' :
					nc += 1
					if len(cov)>7 : cov = '*******'
				else : assert False,f'unexpected cov : {cov} ({c})'
			print(f"{cov:>7}:{m.group('line')}",file=file_fd)
			no_cov -= bool(no_cov_end_re.match(l))
			assert no_cov>=0 , f'orphan END_OF_NO_COV in {f}'
		assert not no_cov , f'orphan START_OF_NO_COV in {f}'
	file_tab[f] = (nuc,nc)

nuc = sum( nuc for f,(nuc,nc) in file_tab.items() )
nc  = sum( nc  for f,(nuc,nc) in file_tab.items() )

with open(summary,'w') as sum_fd :
	print( f'coverage in src : {100*nc/(nc+nuc):.1f}% ( {nc} / {nc+nuc} )' , file=sum_fd )
	print( '# uncovered / total : file'                             , file=sum_fd )
	for f,(nuc,nc) in sorted(file_tab.items(),key=(lambda e:e[1][0]),reverse=True) : # sort by decreasing number of uncovered lines
		print(f'{nuc:>4} / {nuc+nc:>4} : {f}',file=sum_fd)
