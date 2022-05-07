	.file	1 "halt.c"
gcc2_compiled.:
__gnu_compiled_c:
	.text
	.align	2
	.globl	main
	.ent	main
main:
	.frame	$fp,40,$31		# vars= 16, regs= 2/0, args= 16, extra= 0
	.mask	0xc0000000,-4
	.fmask	0x00000000,0	# $31($ra): 存放函数返回地址
	subu	$sp,$sp,40		# 移动栈指针, 为main函数开辟40字大小栈帧空间
	sw	$31,36($sp)			# 在栈帧空间(32~36)$sp区域存储$ra返回地址
	sw	$fp,32($sp)			# 在栈帧空间(28~32)$sp区域存储$fp栈帧地址
	move	$fp,$sp			# $fp = $sp
	jal	__main			# 20~24($sp): k, 16~20($fp): j, 12~16($fp): i
	li	$2,3			# 0x00000003 $2 = 3
	sw	$2,24($fp)		# k = 3
	li	$2,2			# 0x00000002 $2 = 2
	sw	$2,16($fp)		# i = 2
	lw	$2,20($fp)		# $2 = j
	addu	$3,$2,-1	
	sw	$3,20($fp)		# j = j - 1
	lw	$2,16($fp)		# $2 = i
	lw	$3,20($fp)		# $3 = j
	subu	$2,$2,$3	# $2 = i - j
	lw	$3,24($fp)		# $3 = k
	addu	$2,$3,$2	# $2 = i - j + k
	sw	$2,24($fp)		# k = i - j + k
	jal	Halt			# 调用Halt函数
$L1:					# 回收main函数栈帧空间
	move	$sp,$fp		# $sp = $fp
	lw	$31,36($sp)		# $ra = (32~36)$sp
	lw	$fp,32($sp)		# $fp = (28~32)$sp
	addu	$sp,$sp,40	# 移动栈指针, 释放为main函数分配的40字大小空间
	j	$31				# 返回函数
	.end	main
