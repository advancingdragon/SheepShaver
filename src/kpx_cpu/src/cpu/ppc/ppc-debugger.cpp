/*
 *  ppc-debugger.cpp - PowerPC CPU scriptable debugger
 *
 *  Kheperix (C) 2011 Long Nguyen
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include "sysdeps.h"
#include "cpu/vm.hpp"
#include "cpu/ppc/ppc-cpu.hpp"
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <python2.6/Python.h>

// TODO error handling
static powerpc_cpu *the_cpu;
static volatile sig_atomic_t pause_requested;
static PyObject *the_module;
static PyObject *hook_pause_func;
static PyObject *hook_read_func;
static PyObject *hook_write_func;
static PyObject *hook_decode_func;

extern "C"
{
    static PyObject *sheepbug_gpr(PyObject *self, PyObject *args)
    {
        int reg;
        int s = PyArg_ParseTuple(args, "i", &reg);
        if (reg >= 32)
            return Py_None;
        return Py_BuildValue("i", the_cpu->gpr(reg));
    }

    static PyObject *sheepbug_fpr(PyObject *self, PyObject *args)
    {
        int reg;
        int s = PyArg_ParseTuple(args, "i", &reg);
        if (reg >= 32)
            return Py_None;
        return Py_BuildValue("d", the_cpu->fpr(reg));
    }

    static PyObject *sheepbug_fpscr(PyObject *self, PyObject *args)
    {
        return Py_BuildValue("i", the_cpu->fpscr());
    }
    static PyObject *sheepbug_cr(PyObject *self, PyObject *args)
    {
        return Py_BuildValue("i", the_cpu->cr().get());
    }

    static PyObject *sheepbug_xer(PyObject *self, PyObject *args)
    {
        return Py_BuildValue("i", the_cpu->xer().get());
    }

    static PyObject *sheepbug_lr(PyObject *self, PyObject *args)
    {
        return Py_BuildValue("i", the_cpu->lr());
    }

    static PyObject *sheepbug_ctr(PyObject *self, PyObject *args)
    {
        return Py_BuildValue("i", the_cpu->ctr());
    }

    static PyObject *sheepbug_pc(PyObject *self, PyObject *args)
    {
        return Py_BuildValue("i", the_cpu->pc());
    }

    static PyObject *sheepbug_read_byte(PyObject *self, PyObject *args)
    {
        int addr;
        int s = PyArg_ParseTuple(args, "i", &addr);
	    int val = vm_read_memory_1(addr);
        return Py_BuildValue("i", val);
    }
    static PyObject *sheepbug_read_half(PyObject *self, PyObject *args)
    {
        int addr;
        int s = PyArg_ParseTuple(args, "i", &addr);
	    int val = vm_read_memory_2(addr);
        return Py_BuildValue("i", val);
    }
    static PyObject *sheepbug_read_word(PyObject *self, PyObject *args)
    {
        int addr;
        int s = PyArg_ParseTuple(args, "i", &addr);
	    int val = vm_read_memory_4(addr);
        return Py_BuildValue("i", val);
    }

    static PyObject *sheepbug_write_byte(PyObject *self, PyObject *args)
    {
        int addr, val;
        int s = PyArg_ParseTuple(args, "ii", &addr, &val);
	    vm_write_memory_1(addr, val);
        return Py_None;
    }
    
    static PyObject *sheepbug_write_half(PyObject *self, PyObject *args)
    {
        int addr, val;
        int s = PyArg_ParseTuple(args, "ii", &addr, &val);
	    vm_write_memory_2(addr, val);
        return Py_None;
    }
    
    static PyObject *sheepbug_write_word(PyObject *self, PyObject *args)
    {
        int addr, val;
        int s = PyArg_ParseTuple(args, "ii", &addr, &val);
	    vm_write_memory_4(addr, val);
        return Py_None;
    }

    static PyObject *sheepbug_add_bp(PyObject *self, PyObject *args)
    {
        uint32 type;
        uint32 addr;
        int s = PyArg_ParseTuple(args, "ii", &type, &addr);
        switch (type)
        {
        case 1:
            the_cpu->bp_read_set.insert(addr);
            break;
        case 2:
            the_cpu->bp_write_set.insert(addr);
            break;
        case 3:
            the_cpu->bp_exec_set.insert(addr);
            break;
        default:
            printf("Invalid add_bp.\n");
        }
        return Py_None;
    }

    static PyObject *sheepbug_remove_bp(PyObject *self, PyObject *args)
    {
        uint32 type;
        uint32 addr;
        int s = PyArg_ParseTuple(args, "ii", &type, &addr);
        switch (type)
        {
        case 1:
            the_cpu->bp_read_set.erase(addr);
            break;
        case 2:
            the_cpu->bp_write_set.erase(addr);
            break;
        case 3:
            the_cpu->bp_exec_set.erase(addr);
            break;
        default:
            printf("Invalid add_bp.\n");
        }
        return Py_None;
    }

    static PyObject *sheepbug_set_bp_opcode_on(PyObject *self, PyObject *args)
    {
        uint32 flag;
        int s = PyArg_ParseTuple(args, "i", &flag);
        switch (flag)
        {
        case 0:
            the_cpu->bp_opcode_on = false;
            break;
        case 1:
            the_cpu->bp_opcode_on = true;
            break;
        default:
            printf("Invalid set_bp_opcode_on.\n");
        }
        return Py_None;
    }

    static PyObject *sheepbug_set_bp_opcode(PyObject *self, PyObject *args)
    {
        uint32 val;
        int s = PyArg_ParseTuple(args, "i", &val);
        the_cpu->bp_opcode = val;
        return Py_None;
    }

    static PyObject *sheepbug_set_bp_opcode_mask(PyObject *self, PyObject *args)
    {
        uint32 val;
        int s = PyArg_ParseTuple(args, "i", &val);
        the_cpu->bp_opcode_mask = val;
        return Py_None;
    }
};

static PyMethodDef SheepbugMethods[] =
{
    {"gpr", sheepbug_gpr, METH_VARARGS, "Get general-purpose register."}, 
    {"fpr", sheepbug_fpr, METH_VARARGS, "Get floating-point register."}, 
    {"fpscr", sheepbug_fpscr, METH_VARARGS, "Get FPSCR."}, 
    {"cr", sheepbug_cr, METH_VARARGS, "Get CR."}, 
    {"xer", sheepbug_xer, METH_VARARGS, "Get XER."}, 
    {"lr", sheepbug_lr, METH_VARARGS, "Get LR."}, 
    {"ctr", sheepbug_ctr, METH_VARARGS, "Get CTR."}, 
    {"pc", sheepbug_pc, METH_VARARGS, "Get PC."}, 
    {"read_byte", sheepbug_read_byte, METH_VARARGS, "Read byte."}, 
    {"read_half", sheepbug_read_half, METH_VARARGS, "Read halfword."}, 
    {"read_word", sheepbug_read_word, METH_VARARGS, "Read word."}, 
    {"write_byte", sheepbug_write_byte, METH_VARARGS, "Write byte."}, 
    {"write_half", sheepbug_write_half, METH_VARARGS, "Write halfword."}, 
    {"write_word", sheepbug_write_word, METH_VARARGS, "Write word."}, 
    {"add_bp", sheepbug_add_bp, METH_VARARGS, "Add breakpoint."}, 
    {"remove_bp", sheepbug_remove_bp, METH_VARARGS, "Remove breakpoint."}, 
    {"set_bp_opcode_on", sheepbug_set_bp_opcode_on, METH_VARARGS, "Set opcode bp on."}, 
    {"set_bp_opcode", sheepbug_set_bp_opcode, METH_VARARGS, "Set opcode bp."}, 
    {"set_bp_opcode_mask", sheepbug_set_bp_opcode_mask, METH_VARARGS, "Set opcode bp mask."}, 
    {NULL, NULL, 0, NULL}
};

void sigusr1_handler(int signum)
{
    pause_requested = 1;
}

void powerpc_cpu::debugger_hook_pause()
{
    if (pause_requested == 1)
    {
        pause_requested = 0;
        PyObject *args = PyTuple_New(0);
        PyObject *res = PyObject_CallObject(hook_pause_func, args);
        Py_DECREF(args);
        Py_DECREF(res);
    }
}

void powerpc_cpu::debugger_hook_read(int sz, uint32 addr)
{
    if (bp_read_set.count(addr) == 1)
    {
        PyObject *args = PyTuple_New(2);
        PyTuple_SetItem(args, 0, PyInt_FromLong(sz));
        PyTuple_SetItem(args, 1, PyInt_FromLong(addr));
        PyObject *res = PyObject_CallObject(hook_read_func, args);
        Py_DECREF(args);
        Py_DECREF(res);
    }
}
void powerpc_cpu::debugger_hook_write(int sz, uint32 addr)
{
    if (bp_write_set.count(addr) == 1)
    {
        PyObject *args = PyTuple_New(2);
        PyTuple_SetItem(args, 0, PyInt_FromLong(sz));
        PyTuple_SetItem(args, 1, PyInt_FromLong(addr));
        PyObject *res = PyObject_CallObject(hook_write_func, args);
        Py_DECREF(args);
        Py_DECREF(res);
    }
}

void powerpc_cpu::debugger_hook_decode(uint32 opcode)
{
    if ((bp_write_set.count(pc()) == 1) || 
        (bp_opcode_on && (opcode & bp_opcode_mask) == bp_opcode))
    {
        PyObject *args = PyTuple_New(2);
        PyTuple_SetItem(args, 0, PyInt_FromLong(pc()));
        PyTuple_SetItem(args, 1, PyInt_FromLong(opcode));
        PyObject *res = PyObject_CallObject(hook_decode_func, args);
        Py_DECREF(args);
        Py_DECREF(res);
    }
}

void powerpc_cpu::init_debugger()
{
    the_cpu = this;
    pause_requested = 0;
    bp_opcode_on = false;

    struct sigaction sa;
    sa.sa_handler = sigusr1_handler;
    sa.sa_flags = SA_RESTART;
    sigemptyset(&sa.sa_mask);
    sigaddset(&sa.sa_mask, SIGUSR1);
    sigaction(SIGUSR1, &sa, NULL);

    Py_Initialize();
    Py_InitModule("sheepbug", SheepbugMethods);
    PyRun_SimpleString("import sys");
    PyRun_SimpleString("sys.path.append(\"/Applications/SheepShaver/\")");
    the_module = PyImport_ImportModule("sheepbug_hooks");
    if (the_module == NULL)
    {
        printf("Cannot load debugger hook module.\n");
        exit(42);
    }

    PyObject *init_func = PyObject_GetAttrString(the_module, "init_debugger");
    if (init_func == NULL)
    {
        Py_DECREF(the_module);
        printf("Cannot find init_debugger function.\n");
        exit(42);
    }
    if (!PyCallable_Check(init_func))
    {
        Py_DECREF(init_func);
        Py_DECREF(the_module);
        printf("init_debugger is not callable.\n");
        exit(42);
    }

    hook_pause_func = PyObject_GetAttrString(the_module, "hook_pause");
    if (hook_pause_func == NULL)
    {
        Py_DECREF(the_module);
        printf("Cannot find hook_pause function.\n");
        exit(42);
    }
    if (!PyCallable_Check(hook_pause_func))
    {
        Py_DECREF(hook_pause_func);
        Py_DECREF(the_module);
        printf("hook_pause is not callable.\n");
        exit(42);
    }

    hook_read_func = PyObject_GetAttrString(the_module, "hook_read");
    if (hook_read_func == NULL)
    {
        Py_DECREF(the_module);
        printf("Cannot find hook_read function.\n");
        exit(42);
    }
    if (!PyCallable_Check(hook_read_func))
    {
        Py_DECREF(hook_read_func);
        Py_DECREF(the_module);
        printf("hook_read is not callable.\n");
        exit(42);
    }

    hook_write_func = PyObject_GetAttrString(the_module, "hook_write");
    if (hook_write_func == NULL)
    {
        Py_DECREF(the_module);
        printf("Cannot find hook_write function.\n");
        exit(42);
    }
    if (!PyCallable_Check(hook_write_func))
    {
        Py_DECREF(hook_write_func);
        Py_DECREF(the_module);
        printf("hook_write is not callable.\n");
        exit(42);
    }

    hook_decode_func = PyObject_GetAttrString(the_module, "hook_decode");
    if (hook_decode_func == NULL)
    {
        Py_DECREF(the_module);
        printf("Cannot find hook_decode function.\n");
        exit(42);
    }
    if (!PyCallable_Check(hook_decode_func))
    {
        Py_DECREF(hook_decode_func);
        Py_DECREF(the_module);
        printf("hook_decode is not callable.\n");
        exit(42);
    }

    PyObject *args = PyTuple_New(0);
    PyObject *res = PyObject_CallObject(init_func, args);
    Py_DECREF(args);
    Py_DECREF(res);
}
