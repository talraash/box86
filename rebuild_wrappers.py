#!/usr/bin/env python

import os
import glob
import sys

values = ['E', 'v', 'c', 'w', 'i', 'I', 'C', 'W', 'u', 'U', 'f', 'd', 'D', 'p', 'V']
def splitchar(s):
	ret = [len(s)]
	i = 0
	for c in s:
		i = i + 1
		if i == 2:
			continue
		ret.append(values.index(c))
	return ret

def main(root):
	# Initialize variables: gbl for all values, vals for file-per-file values, redirects for redirections
	gbl = []
	vals = {}
	redirects = {}
	
	# First read the files inside the headers
	for filepath in glob.glob(os.path.join(root, "src", "wrapped*_private.h")):
		filename = filepath.split("/")[-1]
		locval = []
		with open(filepath, 'r') as file:
			for line in file:
				ln = line.strip()
				# If the line is a `GO...' line (GO/GOM/GO2/...)...
				if ln.startswith("GO"):
					# ... then look at the second parameter of the line
					ln = ln.split(",")[1].split(")")[0].strip()
					if any(c not in values for c in ln[2:]) or (('v' in ln[2:]) and (len(ln) > 3)):
						old = ln
						# This needs more work
						acceptables = ['v', 'o', '0', '1'] + values
						if any(c not in acceptables for c in ln[2:]):
							raise NotImplementedError("{0}".format(ln[2:]))
						# Ok, this is acceptable: there is 0, 1, stdout and void
						ln = (ln
							.replace("v", "")   # void   -> nothing
							.replace("o", "p")  # stdout -> pointer
							.replace("0", "p")  # 0      -> pointer
							.replace("1", "i")) # 1      -> integer
						redirects[old] = ln
					# Simply append the function name if it's not yet existing
					if ln not in locval:
						locval.append(ln)
					if ln not in gbl:
						gbl.append(ln)
		
		# Sort the file local values and add it to the dictionary
		locval.sort(key=lambda v: splitchar(v))
		vals[filename] = locval
	
	# Sort the table
	gbl.sort(key=lambda v: splitchar(v))
	
	def length(s):
		l = len(s)
		if l < 10:
			ret = "0" + str(l)
		else:
			ret = str(l)
		return ret
	
	# Now the files rebuilding part
	# File headers and guards
	files_headers = {
		"wrapper.c": """/*****************************************************************
 * File automatically generated by rebuild_wrappers.py (v0.0.1.01)
 *****************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "wrapper.h"
#include "x86emu_private.h"
#include "x87emu_private.h"
#include "regs.h"

typedef union ui64_s {
    int64_t     i;
    uint64_t    u;
    uint32_t    d[2];
} ui64_t;

""",
		"wrapper.h": """/*****************************************************************
 * File automatically generated by rebuild_wrappers.py (v0.0.1.01)
 *****************************************************************/
#ifndef __WRAPPER_H_
#define __WRAPPER_H_
#include <stdint.h>

typedef struct x86emu_s x86emu_t;

// the generic wrapper pointer functions
typedef void (*wrapper_t)(x86emu_t* emu, uintptr_t fnc);

// list of defined wrapper
// v = void, i = int32, u = uint32, U/I= (u)int64
// p = pointer, P = callback
// f = float, d = double, D = long double, L = fake long double
// V = vaargs, E = current x86emu struct
// 0 = constant 0, 1 = constant 1
// o = stdout
// C = unsigned byte c = char
// W = unsigned short w = short
// Q = ...
// S8 = struct, 8 bytes

"""
	}
	files_guards = {"wrapper.c": """""",
		"wrapper.h": """
#endif //__WRAPPER_H_
"""
	}
	
	# Transform strings into arrays
	gbl = [[c for c in v] for v in gbl]
	
	# Rewrite the wrapper.h file:
	with open(os.path.join(root, "src", "wrapper.h"), 'w') as file:
		file.write(files_headers["wrapper.h"])
		for v in gbl:
			file.write("void " + ''.join(v) + "(x86emu_t *emu, uintptr_t fnc);\n")
		for v in redirects:
			file.write("void " + ''.join(v) + "(x86emu_t *emu, uintptr_t fnc);\n")
		file.write(files_guards["wrapper.h"])
	
	# Rewrite the wrapper.c file:
	with open(os.path.join(root, "src", "wrapper.c"), 'w') as file:
		file.write(files_headers["wrapper.c"])
		
		# First part: typedefs
		for v in gbl:
			types = ["x86emu_t*", "void", "char", "short", "int32_t", "int64_t", "unsigned char", "unsigned short", "uint32_t", "uint64_t", "float", "double", "long double", "void*", "void*"]
			
			file.write("typedef " + types[values.index(v[0])] + " (*" + ''.join(v) + "_t)"
				+ "(" + ', '.join(types[values.index(t)] for t in v[2:]) + ");\n")
		
		# Next part: function definitions
		
		# Helper functions to write the function definitions
		def function_args(args, d=4):
			if len(args) == 0:
				return ""
			if d % 4 != 0:
				raise ValueError("{d} is not a multiple of 4. Did you try passing a V and something else?")
			
			if args[0] == "0":
				return "(void*)(R_ESP + {p}), ".format(p=d) + function_args(args[1:], d + 4)
			elif args[0] == 1:
				return "1, " + function_args(args[1:], d)
			
			arg = [
				"emu, ",                          # E
				"",                               # v
				"*(int8_t*)(R_ESP + {p}), ",      # c
				"*(int16_t*)(R_ESP + {p}), ",     # w
				"*(int32_t*)(R_ESP + {p}), ",     # i
				"*(int64_t*)(R_ESP + {p}), ",     # I
				"*(uint8_t*)(R_ESP + {p}), ",     # C
				"*(uint16_t*)(R_ESP + {p}), ",    # W
				"*(uint32_t*)(R_ESP + {p}), ",    # u
				"*(uint64_t*)(R_ESP + {p}), ",    # U
				"*(float*)(R_ESP + {p}), ",       # f
				"*(double*)(R_ESP + {p}), ",      # d
				"*(long double*)(R_ESP + {p}), ", # D
				"*(void**)(R_ESP + {p}), ",       # p
				"(void*)(R_ESP + {p} - 4), "      # V
			]
			deltas = [0, 4, 4, 4, 4, 8, 4, 4, 4, 8, 4, 8, 12, 4, 1]
			if len(values) != len(arg):
				raise NotImplementedError("len(values) != len(arg)")
				raise NotImplementedError("len(values) = {lenval} != len(arg) = {lenarg}".format(lenval=len(values), lenarg=len(arg)))
			if len(values) != len(deltas):
				raise NotImplementedError("len(values) != len(deltas)")
				raise NotImplementedError("len(values) = {lenval} != len(deltas) = {lendeltas}".format(lenval=len(values), lendeltas=len(deltas)))
			return arg[values.index(args[0])].format(p=d) + function_args(args[1:], d + deltas[values.index(args[0])])
		
		def function_writer(f, N, W, rettype, args):
			f.write("void {0}(x86emu_t *emu, uintptr_t fcn) {2} {1} fn = ({1})fcn; ".format(N, W, "{"))
			vals = [
				"\n#error Invalid return type: emulator\n",                     # E
				"fn({0});",                                                     # v
				"R_EAX=fn({0});",                                               # c
				"R_EAX=fn({0});",                                               # w
				"R_EAX=fn({0});",                                               # i
				"ui64_t r; r.i=fn({0}); R_EAX=r.d[0]; R_EDX=r.d[1];",           # I
				"R_EAX=(unsigned char)fn({0});",                                # C
				"R_EAX=(unsigned short)fn({0});",                               # W
				"R_EAX=(uint32_t)fn({0});",                                     # u
				"ui64_t r; r.u=(uint64_t)fn({0}); R_EAX=r.d[0]; R_EDX=r.d[1];", # U
				"float fl=fn({0}); fpu_do_push(emu); ST0.d = fl;",              # f
				"double db=fn({0}); fpu_do_push(emu); ST0.d = db;",             # d
				"long double ld=fn({0}); fpu_do_push(emu); ST0.d = ld;",        # D
				"R_EAX=(uintptr_t)fn({0});",                                    # p
				"\n#error Invalid return type: va_list\n",                      # V
			]
			if len(values) != len(vals):
				raise NotImplementedError("len(values) = {lenval} != len(vals) = {lenvals}".format(lenval=len(values), lenvals=len(vals)))
			f.write(vals[values.index(rettype)].format(function_args(args)[:-2]) + " }\n")
		
		for v in gbl:
			function_writer(file, ''.join(v), ''.join(v) + "_t", v[0], v[2:])
		for v in redirects:
			function_writer(file, v, redirects[v] + "_t", v[0], v[2:])
		
		file.write(files_guards["wrapper.c"])
	
	return 0

if __name__ == '__main__':
	if main(sys.argv[1]) != 0:
		exit(2)
	exit(0)