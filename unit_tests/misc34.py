if __name__!='__main__' :

	import lmake
	from lmake.rules import Rule

	lmake.manifest = (
		'Lmakefile.py'
	,	'step.py'
	)

	from step import step

	class High(Rule) :
		prio    = 1
		targets = { 'DUT' : ('dut','optional') }
		if step==1 : cmd = ''
		else       : cmd = 'exit 1'

	class Low(Rule) :
		target = 'dut'
		cmd    = 'echo low'

else :

	import ut

	print('step=1',file=open('step.py','w'))
	ut.lmake( 'dut' , steady=1 , done=1 )
	print('step=2',file=open('step.py','w'))
	ut.lmake( 'dut' , failed=1 , unlinked=1 , rc=1 )
