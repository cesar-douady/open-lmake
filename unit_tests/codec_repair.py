# This file is part of the open-lmake distribution (git@github.com:cesar-douady/open-lmake.git)
# Copyright (c) 2023-2026 Doliam
# This program is free software: you can redistribute/modify under the terms of the GPL-v3 (https://www.gnu.org/licenses/gpl-3.0.html).
# This program is distributed WITHOUT ANY WARRANTY, without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

# lcodec_repair on a shared codec dir, as seen from jobs :
# - repair with nothing to repair must trigger no rerun
# - hand-written values (regular decode files) are integrated, modified values are seen as changed
# - a removed entry is recreated by the encoding job
# - user codes are preferred over checksum codes in case of conflict
# - 2 contexts (store cleanup must not remove entries of the other context), codes with escaped chars, junk in the dir

import locale
for l in ('C.UTF-8','en_US.UTF-8','C.utf8','en_US.utf8') :
	try    : locale.setlocale( locale.LC_ALL , l ) # default encoding is ascii on Centos7 and this crashes with DecAccent
	except : continue
	break
else :
	print('cannot find a UTF-8 locale',file=open('skipped','w'))
	exit()

if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'../codec_files/'
	)

	class Enc(Rule) :
		target = r'{File:.*}.enc'
		shell  = ('/bin/bash','-e')
		cmd    = 'echo {File}_val | lencode -t ../codec_files/sub/ -x ctx -l 4'

	class Enc2(Rule) :
		target = r'{File:.*}.enc2'
		shell  = ('/bin/bash','-e')
		cmd    = 'echo {File}_val | lencode -t ../codec_files/sub/ -x ctx2 -l 4' # same values as Enc, in another context

	class Dec(Rule) :
		target = r'{File:.*}.dec'
		dep    = '{File}.enc'
		shell  = ('/bin/bash','-e')
		cmd    = 'ldecode -t ../codec_files/sub/ -x ctx -c $(cat {File}.enc)'

	class Dec2(Rule) :
		target = r'{File:.*}.dec2'
		dep    = '{File}.enc2'
		shell  = ('/bin/bash','-e')
		cmd    = 'ldecode -t ../codec_files/sub/ -x ctx2 -c $(cat {File}.enc2)'

	class DecUser(Rule) :
		target = r'udec'
		shell  = ('/bin/bash','-e')
		cmd    = 'ldecode -t ../codec_files/sub/ -x ctx -c user_code'

	class DecAccent(Rule) :
		target = r'adec'
		shell  = ('/bin/bash','-e')
		cmd    = "ldecode -t ../codec_files/sub/ -x ctx -c 'é'"

else :

	import os
	import subprocess as sp
	import time

	import ut

	codec_dir = '../codec_files/sub'

	def repair(*flags) :
		res = sp.run( ('lcodec_repair','-f',*flags,codec_dir) , universal_newlines=True , stdout=sp.PIPE , stderr=sp.STDOUT )
		print(f'+ lcodec_repair -f {" ".join(flags)} -> rc={res.returncode}\n{res.stdout}')
		assert res.returncode==0
		res = sp.run( ('lcodec_repair','-n',*flags,codec_dir) , universal_newlines=True , stdout=sp.PIPE , check=True ).stdout                     # 2nd run must be a no-op
		acts = [ l for l in res.splitlines() if l.split(' ',1)[0] in ('rm','cp','ln','rmdir')  and not l.split()[:3]==['rm','-rf','LMAKE/lmake'] ] # lcodec_repair may leave a trace file
		assert not acts,acts

	def set_decode( code , val ) :
		f = f'{codec_dir}/tab/ctx/{code}.decode'
		if os.path.lexists(f) : os.unlink(f)
		open(f,'w').write(val)

	os.makedirs('codec_files/sub')
	os.makedirs('repo/LMAKE') ; os.symlink('../Lmakefile.py','repo/Lmakefile.py') ; os.chdir('repo')

	# initial build (codec dir is initialized by jobs), 2 contexts with the same values
	ut.lmake( 'a.dec' , 'b.dec' , 'a.dec2' , 'b.dec2' , new=... , done=8 )
	ut.lmake( 'a.dec' , 'b.dec' , 'a.dec2' , 'b.dec2'                    )

	# repair with no change (with and without -r), then rebuild : must do nothing
	repair(    )
	ut.lmake( 'a.dec' , 'b.dec' , 'a.dec2' , 'b.dec2' )
	repair('-r')
	ut.lmake( 'a.dec' , 'b.dec' , 'a.dec2' , 'b.dec2' )

	# add user values by hand (including a code with a non-ascii char), repair -r, build : only new targets are done
	set_decode( 'user_code' , 'user_val_1\n' )
	set_decode( '\\xc3\\xa9' , 'accent_val\n' ) # on-disk name of code é (cf. mk_printable)
	repair('-r')
	ut.lmake( 'a.dec' , 'b.dec' , 'a.dec2' , 'b.dec2' , 'udec' , 'adec' , done=2 )
	assert open('udec').read()=='user_val_1\n',open('udec').read()
	assert open('adec').read()=='accent_val\n',open('adec').read()

	# change user value by hand, repair -r : udec must be rebuilt with new value, others untouched
	set_decode( 'user_code' , 'user_val_2\n' )
	repair('-r')
	ut.lmake( 'a.dec' , 'b.dec' , 'a.dec2' , 'b.dec2' , 'udec' , 'adec' , done=1 , changed=1 )
	assert open('udec').read()=='user_val_2\n',open('udec').read()

	# remove decode link of a by hand, repair (w/o -r, encode link is removed too) : a.enc must rerun and recreate the entry, ctx2 is untouched
	a_code = open('a.enc').read().strip()
	os.unlink(f'{codec_dir}/tab/ctx/{a_code}.decode')
	repair()
	ut.lmake( 'a.dec' , 'b.dec' , 'a.dec2' , 'b.dec2' , 'udec' , 'adec' , done=... , steady=... , changed=... , rerun=... )
	assert open('a.dec').read()=='a_val\n',open('a.dec').read()
	ut.lmake( 'a.dec' , 'b.dec' , 'a.dec2' , 'b.dec2' , 'udec' , 'adec' )

	# add a hand-written code for the value of b, repair -r : b.enc must be rerun and get the user code (user codes are preferred)
	set_decode( 'bcode' , 'b_val\n' )
	repair('-r')
	ut.lmake( 'a.dec' , 'b.dec' , 'a.dec2' , 'b.dec2' , 'udec' , 'adec' , done=... , steady=... , changed=... , rerun=... )
	assert open('b.enc' ).read().strip()=='bcode'  ,open('b.enc' ).read()
	assert open('b.dec' ).read()        =='b_val\n',open('b.dec' ).read()
	assert open('b.enc2').read().strip()!='bcode'  ,open('b.enc2').read() # ctx2 is not impacted
	ut.lmake( 'a.dec' , 'b.dec' , 'a.dec2' , 'b.dec2' , 'udec' , 'adec' )

	# junk : stray files, bad encode names, empty dirs, a decode dir : all cleaned up, nothing else impacted
	os.symlink( 'a.decode' , f'{codec_dir}/tab/ctx/foo.encode' )
	os.makedirs( f'{codec_dir}/tab/ctx/dir.decode' )
	os.makedirs( f'{codec_dir}/tab/empty_ctx'      )
	open( f'{codec_dir}/README'         , 'w' )
	open( f'{codec_dir}/tab/ctx/stray'  , 'w' )
	open( f'{codec_dir}/store/badname'  , 'w' )
	repair()
	assert sorted(os.listdir(codec_dir))=='LMAKE stamp store tab'.split(),os.listdir(codec_dir)
	assert not os.path.exists(f'{codec_dir}/tab/empty_ctx')
	ut.lmake( 'a.dec' , 'b.dec' , 'a.dec2' , 'b.dec2' , 'udec' , 'adec' )
