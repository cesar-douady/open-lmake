import os
import os.path as osp
import random
import sys

def prepare_files(dir,n_files) :
	files = {'Manifest','Lmakefile.py'}
	dir = osp.abspath(dir)
	l = len(dir)
	if dir[-1]!='/' : l += 1
	for dp,ds,fs in os.walk(dir) :
		if dp[-1]!='/' : dp+= '/'
		dp = dp[l:]
		if dp : os.makedirs(dp,exist_ok=True)
		for f in fs :
			f = dp+f
			if f not in files :
				open(f,'w')
				files.add(f)
			f += '.XXXXXXXXXXXXXXXXX'
			if f not in files :
				open(f,'w')
				files.add(f)
			n = len(files)
			if n%1000==0 : print('.',end='',flush=True)
			if n>=n_files : return files
	return files

# use a dir as a source of files
print(f'preparing {sys.argv[2]} files from {sys.argv[1]} ',end='',flush=True)
files = prepare_files( sys.argv[1] , int(sys.argv[2]) )
print(' done')

# random shuffle
print(f'shuffling Manifest ...',end='',flush=True)
files = list(files)
random.shuffle(files)
print(' done')

# add necessary files
print(f'writing Manifest ...',end='',flush=True)
with open('Manifest','w') as m :
	for f in files : print(f,file=m)
print(f' done {len(files)} files')

# clean up
print(f'cleaning repo ...',end='',flush=True)
assert os.system(f'rm -rf LMAKE')==0
print(' done')

# read makefile
print(f'running lmake ...')
assert os.system(f'lmake')==0
print('done')

# check db
print(f'checking lmake store ...',end='',flush=True)
assert os.system(f'ldump >/dev/null')==0
print(' done')
