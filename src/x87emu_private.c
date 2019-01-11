#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "debug.h"
#include "x86emu_private.h"
#include "x87emu_private.h"

void fpu_do_push(x86emu_t* emu)
{
    ++emu->fpu_stack;
    if(emu->fpu_stack == 9) {// overflow
        printf_log(LOG_NONE, "Error: %p: FPU Stack overflow\n", (void*)emu->old_ip);    // probably better to raise something
        emu->quit = 1;
        return;
    }
    emu->top = (emu->top-1)&7;
}

void fpu_do_pop(x86emu_t* emu)
{
    emu->top = (emu->top+1)&7;
    --emu->fpu_stack;
    if(emu->fpu_stack < 0) {// underflow
        printf_log(LOG_NONE, "Error: %p: FPU Stack underflow\n", (void*)emu->old_ip);    // probably better to raise something
        emu->quit = 1;
        return;
    }

}

void reset_fpu(x86emu_t* emu)
{
    memset(emu->fpu, 0, sizeof(emu->fpu));
    memset(emu->fpu_ld, 0, sizeof(emu->fpu_ld));
    emu->cw = 0x37F;
    emu->sw.x16 = 0x0000;
    emu->top = 0;
    emu->fpu_stack = 0;
}

void fpu_fcom(x86emu_t* emu, double b)
{
    if(isnan(ST0.d) || isnan(b)) {
        emu->sw.f.F87_C0 = 1;
        emu->sw.f.F87_C2 = 1;
        emu->sw.f.F87_C3 = 1;
    } else if (isgreater(ST0.d, b)) {
        emu->sw.f.F87_C0 = 0;
        emu->sw.f.F87_C2 = 0;
        emu->sw.f.F87_C3 = 0;
    } else if (isless(ST0.d, b)) {
        emu->sw.f.F87_C0 = 1;
        emu->sw.f.F87_C2 = 0;
        emu->sw.f.F87_C3 = 0;
    } else {
        emu->sw.f.F87_C0 = 0;
        emu->sw.f.F87_C2 = 0;
        emu->sw.f.F87_C3 = 1;
    }
}

void fpu_fcomi(x86emu_t* emu, double b)
{
    if(isnan(ST0.d) || isnan(b)) {
        emu->eflags.f.F_CF = 1;
        emu->eflags.f.F_PF = 1;
        emu->eflags.f.F_ZF = 1;
    } else if (isgreater(ST0.d, b)) {
        emu->eflags.f.F_CF = 0;
        emu->eflags.f.F_PF = 0;
        emu->eflags.f.F_ZF = 0;
    } else if (isless(ST0.d, b)) {
        emu->eflags.f.F_CF = 1;
        emu->eflags.f.F_PF = 0;
        emu->eflags.f.F_ZF = 0;
    } else {
        emu->eflags.f.F_CF = 0;
        emu->eflags.f.F_PF = 0;
        emu->eflags.f.F_ZF = 1;
    }
}

double fpu_round(x86emu_t* emu, double d) {
    if (!isfinite(d))
        return d;
    //switch(emu->cw)   // TODO: implement Rounding...
    return round(d);
}

void fpu_fxam(x86emu_t* emu) {
    emu->sw.f.F87_C1 = (ST0.l.upper<0)?1:0;
    if(!emu->fpu_stack) {
        emu->sw.f.F87_C3 = 1;
        emu->sw.f.F87_C2 = 0;
        emu->sw.f.F87_C0 = 1;
        return;
    }
    if(isinf(ST0.d)) {  // TODO: Unsuported and denormal not analysed...
        emu->sw.f.F87_C3 = 0;
        emu->sw.f.F87_C2 = 1;
        emu->sw.f.F87_C0 = 1;
        return;
    }
    if(isnan(ST0.d)) {  // TODO: Unsuported and denormal not analysed...
        emu->sw.f.F87_C3 = 0;
        emu->sw.f.F87_C2 = 0;
        emu->sw.f.F87_C0 = 1;
        return;
    }
    if(ST0.d==0.0) {
        emu->sw.f.F87_C3 = 1;
        emu->sw.f.F87_C2 = 0;
        emu->sw.f.F87_C0 = 0;
        return;
    }
    // normal...
    emu->sw.f.F87_C3 = 0;
    emu->sw.f.F87_C2 = 1;
    emu->sw.f.F87_C0 = 0;

}

void fpu_fbst(x86emu_t* emu, uint8_t* d) {
    // very aproximative... but should not be much used...
    uint8_t p;
    uint8_t sign = 0x00;
    double tmp, v = ST0.d;
    if(ST0.d<0.0) {
        sign = 0x80;
        v = -v;
    }
    for (int i=0; i<9; ++i) {
        tmp = floor(v/10.0);
        p = (v - 10.0*tmp);
        v = tmp;
        tmp = floor(v/10.0);
        p |= ((uint8_t)(v - 10.0*tmp))<<4;
        v = tmp;

        *(d++)=p;
    }
    tmp = floor(v/10.0);
    p = (v - 10.0*tmp);
    p |= sign;
    *(d++)=p;
    // no flags....
}

void fpu_fbld(x86emu_t* emu, uint8_t* s) {
    uint8_t p;
    uint64_t tmp = 0;
    uint64_t m = 1;
    for (int i=0; i<9; ++i) {
        p =*(s++);
        tmp += m * (p&0x0f);
        m *= 10;
        tmp += m * ((p>>4)&0x0f);
        m *= 10;
    }
    ST0.d = tmp;
    p =*(s++);
    ST0.d += m * (p&0x0f);
    if(p&0x80)
        ST0.d = -ST0.d;
}


#define BIAS80 16383
#define BIAS64 1023
// long double (80bits) -> double (64bits)
void LD2D(void* ld, void* d)
{
	fpu_reg_t result;
    #pragma pack(push, 1)
	struct {
		fpu_reg_t f;
		int16_t b;
	} val;
    #pragma pack(pop)
	val.f.l.lower = *(uint32_t*)ld;
    val.f.l.upper = *(uint32_t*)(char*)(ld+4);
	val.b  = *(int16_t*)((char*)ld+8);
	int32_t exp64 = (((uint32_t)(val.b&0x7fff) - BIAS80) + BIAS64);
	int32_t exp64final = exp64&0x7ff;
    // do specific value first (0, infinite...)
    // bit 63 is "intetger part"
    // bit 62 is sign
    if((uint32_t)(val.b&0x7fff)==0x7fff) {
        // infinity and nans
        int t = 0; //nan
        switch((val.f.l.upper>>30)) {
            case 0: if((val.f.l.upper&(1<<29))==0) t = 1;
                    break;
            case 2: if((val.f.l.upper&(1<<29))==0) t = 1;
                    break;
        }
        if(t) {    // infinite
            result.d = HUGE_VAL;
        } else {      // NaN
            result.l.upper |= 0x7ff << 20;
            result.l.lower = 0;
        }
        if(val.b&0x8000)
            result.l.upper |= 0x80000000;
        *(uint64_t*)d = result.ll;
        return;
    }
    if(((uint32_t)(val.b&0x7fff)==0) || (exp64<=0)) {
        //if(val.f.ll==0)
        // zero
        //if(val.f.ll!=0)
        // denormal, but that's to small value for double 
        uint64_t r = 0;
        if(val.b&0x8000)
            r |= 0x8000000000000000LL;
        *(uint64_t*)d = r;
        return;
    }

    if(exp64>=0x7ff) {
        // to big value...
        result.d = HUGE_VAL;
        if(val.b&0x8000)
            result.l.upper |= 0x80000000;
        *(uint64_t*)d = result.ll;
        return;
    }

	uint64_t mant64 = (val.f.ll >> 11) & 0xfffffffffffffLL;
	uint32_t sign = (val.b&0x8000)?1:0;
    result.ll = mant64;
	result.l.upper |= (sign <<31)|((exp64final&0x7ff) << 20);

	*(uint64_t*)d = result.ll;
}

// double (64bits) -> long double (80bits)
void D2LD(void* d, void* ld)
{
    #pragma pack(push, 1)
	struct {
		fpu_reg_t f;
		int16_t b;
	} val;
    #pragma pack(pop)
    fpu_reg_t s;
    s.ll = *(uint64_t*)d;   // use memcpy to avoid risk of Bus Error?
    // do special value first
    if(s.ll&0x7fffffffffffffffLL==0) {
        // zero...
        val.f.ll = 0;
        if(s.l.upper&0x8000)
            val.b = 0x8000;
        else
            val.b = 0;
        memcpy(ld, &val, 10);
        return;
    }

	int32_t sign80 = (s.l.upper&0x80000000)?1:0;
	int32_t exp80 =  s.l.upper&0x7ff00000;
	int32_t exp80final = (exp80>>20);
	int64_t mant80 = s.ll&0x000fffffffffffffLL;
	int64_t mant80final = (mant80 << 11);
    if(exp80final==0x7ff) {
        // NaN and Infinite
        exp80final = 0x7fff;
        if(mant80==0x0)
            mant80final = 0x8000000000000000LL; //infinity
        else
            mant80final = 0xc000000000000000LL; //(quiet)NaN
    } else {
        if(exp80!=0){ 
            mant80final |= 0x8000000000000000LL;
            exp80final += (BIAS80 - BIAS64);
        }
    }
	val.b = ((int16_t)(sign80)<<15)| (int16_t)(exp80final);
	val.f.ll = mant80final;
    memcpy(ld, &val, 10);
    /*memcpy(ld, &f.ll, 8);
    memcpy((char*)ld + 8, &val.b, 2);*/
}


void fpu_loadenv(x86emu_t* emu, char* p, int b16)
{
    emu->cw = *(uint16_t*)p;
    p+=(b16)?2:4;
    emu->sw.x16 = *(uint16_t*)p;
    emu->top = emu->sw.f.F87_TOP;
    p+=(b16)?2:4;
    // tagword: 2bits*8
    // simplied tags... just reading back the top pointer
    uint16_t tags = *(uint16_t*)p;
    for (emu->fpu_stack=0; emu->fpu_stack<8; ++emu->fpu_stack)
        if(tags & (3<<(emu->fpu_stack*2)))
            break; 
    // intruction pointer: 16bits
    // data (operand) pointer: 16bits
    // last opcode: 11bits save: 16bits restaured (1st and 2nd opcode only)
}

void fpu_savenv(x86emu_t* emu, char* p, int b16)
{
    emu->sw.f.F87_TOP = emu->top&7;
    *(uint16_t*)p = emu->cw;
    p+=2;
    if(!b16) {*(uint16_t*)p = 0; p+=2;}
    *(uint16_t*)p = emu->sw.x16;
    p+=2;
    if(!b16) {*(uint16_t*)p = 0; p+=2;}
    // tagword: 2bits*8
    // simplied tags...
    uint16_t tags = 0;
    for (int i=0; i<8; ++i)
        if(i>=emu->fpu_stack)
            tags |= (3)<<(i*2);
    *(uint16_t*)p = tags;
    // other stuff are not pushed....
}
