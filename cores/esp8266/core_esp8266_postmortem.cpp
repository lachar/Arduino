/*
 postmortem.c - output of debug info on sketch crash
 Copyright (c) 2015 Ivan Grokhotkov. All rights reserved.
 This file is part of the esp8266 core for Arduino environment.

 This library is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This library is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this library; if not, write to the Free Software
 Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */


#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdarg.h>
#include "debug.h"
#include "ets_sys.h"
#include "user_interface.h"
#include "esp8266_peri.h"
#include "cont.h"
#include "pgmspace.h"
#include "gdb_hooks.h"
#include "StackThunk.h"

extern "C" {

extern void __real_system_restart_local();

// These will be pointers to PROGMEM const strings
static const char* s_panic_file = 0;
static int s_panic_line = 0;
static const char* s_panic_func = 0;
static const char* s_panic_what = 0;

static bool s_abort_called = false;
static const char* s_unhandled_exception = NULL;

void abort() __attribute__((noreturn));
static void uart_write_char_d(char c);
static void uart0_write_char_d(char c);
static void uart1_write_char_d(char c);
static void print_stack(uint32_t start, uint32_t end);

// using numbers different from "REASON_" in user_interface.h (=0..6)
enum rst_reason_sw
{
    REASON_USER_SWEXCEPTION_RST = 254
};
static int s_user_reset_reason = REASON_DEFAULT_RST;

// From UMM, the last caller of a malloc/realloc/calloc which failed:
extern void *umm_last_fail_alloc_addr;
extern int umm_last_fail_alloc_size;

static void raise_exception() __attribute__((noreturn));

extern void __custom_crash_callback( struct rst_info * rst_info, uint32_t stack, uint32_t stack_end ) {
    (void) rst_info;
    (void) stack;
    (void) stack_end;
}

extern void custom_crash_callback( struct rst_info * rst_info, uint32_t stack, uint32_t stack_end ) __attribute__ ((weak, alias("__custom_crash_callback")));


// Prints need to use our library function to allow for file and function
// to be safely accessed from flash. This function encapsulates snprintf()
// [which by definition will 0-terminate] and dumping to the UART
static void ets_printf_P(const char *str, ...) {
    char destStr[160];
    char *c = destStr;
    va_list argPtr;
    va_start(argPtr, str);
    vsnprintf(destStr, sizeof(destStr), str, argPtr);
    va_end(argPtr);
    while (*c) {
        ets_putc(*(c++));
    }
}

volatile static struct {
    uint32_t pc;
    uint32_t ps;
    uint32_t sar;
    uint32_t vpri;
    uint32_t a[16]; //a0..a15
    uint32_t litbase;
    uint32_t sr176;
    uint32_t sr208;
    uint32_t valid;
} core_regs;

void __wrap_system_restart_local() {
    register uint32_t sp asm("a1");
    uint32_t sp_dump = sp;

    struct rst_info rst_info;
    memset(&rst_info, 0, sizeof(rst_info));
    if (s_user_reset_reason == REASON_DEFAULT_RST)
    {
        system_rtc_mem_read(0, &rst_info, sizeof(rst_info));
        if (rst_info.reason != REASON_SOFT_WDT_RST &&
            rst_info.reason != REASON_EXCEPTION_RST &&
            rst_info.reason != REASON_WDT_RST)
        {
            rst_info.reason = REASON_DEFAULT_RST;
        }
    }
    else
        rst_info.reason = s_user_reset_reason;

    // TODO:  ets_install_putc1 definition is wrong in ets_sys.h, need cast
    ets_install_putc1((void *)&uart_write_char_d);

    if (s_panic_line) {
        ets_printf_P(PSTR("\nPanic %S:%d %S"), s_panic_file, s_panic_line, s_panic_func);
        if (s_panic_what) {
            ets_printf_P(PSTR(": Assertion '%S' failed."), s_panic_what);
        }
        ets_putc('\n');
    }
    else if (s_unhandled_exception) {
        ets_printf_P(PSTR("\nUnhandled C++ exception: %S\n"), s_unhandled_exception);
    }
    else if (s_abort_called) {
        ets_printf_P(PSTR("\nAbort called\n"));
    }
    else if (rst_info.reason == REASON_EXCEPTION_RST) {
        ets_printf_P(PSTR("\nException (%d):\nepc1=0x%08x epc2=0x%08x epc3=0x%08x excvaddr=0x%08x depc=0x%08x\n"),
            rst_info.exccause, rst_info.epc1, rst_info.epc2, rst_info.epc3, rst_info.excvaddr, rst_info.depc);
    }
    else if (rst_info.reason == REASON_SOFT_WDT_RST) {
        ets_printf_P(PSTR("\nSoft WDT reset\n"));
    }
    else {
        ets_printf_P(PSTR("\nGeneric Reset\n"));
    }

    uint32_t cont_stack_start = (uint32_t) &(g_pcont->stack);
    uint32_t cont_stack_end = (uint32_t) g_pcont->stack_end;
    uint32_t stack_end;

    // amount of stack taken by interrupt or exception handler
    // and everything up to __wrap_system_restart_local
    // (determined empirically, might break)
    uint32_t offset = 0;
    if (rst_info.reason == REASON_SOFT_WDT_RST) {
        offset = 0x1b0;
    }
    else if (rst_info.reason == REASON_EXCEPTION_RST) {
        offset = 0x1a0;
    }
    else if (rst_info.reason == REASON_WDT_RST) {
        offset = 0x10;
    }

    ets_printf_P(PSTR("\n>>>stack>>>\n"));

    if (sp_dump > stack_thunk_get_stack_bot() && sp_dump <= stack_thunk_get_stack_top()) {
        // BearSSL we dump the BSSL second stack and then reset SP back to the main cont stack
        ets_printf_P(PSTR("\nctx: bearssl\nsp: %08x end: %08x offset: %04x\n"), sp_dump, stack_thunk_get_stack_top(), offset);
        print_stack(sp_dump + offset, stack_thunk_get_stack_top());
        offset = 0; // No offset needed anymore, the exception info was stored in the bssl stack
        sp_dump = stack_thunk_get_cont_sp();
    }

    if (sp_dump > cont_stack_start && sp_dump < cont_stack_end) {
        ets_printf_P(PSTR("\nctx: cont\n"));
        stack_end = cont_stack_end;
    }
    else {
        ets_printf_P(PSTR("\nctx: sys\n"));
        stack_end = 0x3fffffb0;
        // it's actually 0x3ffffff0, but the stuff below ets_run
        // is likely not really relevant to the crash
    }

    ets_printf_P(PSTR("sp: %08x end: %08x offset: %04x\n"), sp_dump, stack_end, offset);

    print_stack(sp_dump + offset, stack_end);

    ets_printf_P(PSTR("<<<stack<<<\n"));

    // Use cap-X formatting to ensure the standard EspExceptionDecoder doesn't match the address
    if (umm_last_fail_alloc_addr) {
      ets_printf_P(PSTR("\nlast failed alloc call: %08X(%d)\n"), (uint32_t)umm_last_fail_alloc_addr, umm_last_fail_alloc_size);
    }

    custom_crash_callback( &rst_info, sp_dump + offset, stack_end );

    

    ets_printf_P("\n---- begin regs ----\n");
    if (!core_regs.valid) {
    // At this point we have lost most registers.  Just update pc, sp
      __asm__ __volatile__("\n\
        movi    a0, .\n\
        s32i    a0, %0, 0x00\n" : : "r" (&core_regs) : "a0");
      core_regs.a[1] = sp_dump;
    }

    for (volatile uint32_t *r = &core_regs.pc; r < &core_regs.valid; r++) {
        ets_printf_P("%08x\n", *r);
    }
    ets_printf_P("---- end regs ----\n");

    ets_printf_P("\n---- begin core ----\n");
    uint8_t *ram = (uint8_t*)0x3FFE8000;
    while (ram < (uint8_t*)0x40000000) {
      for (size_t i=0; i<64; i++) { ets_printf_P("%02X", ram[i]); }
      ets_printf_P("\n");
      ram += 64;
      system_soft_wdt_feed();
    }
    ets_printf_P("---- end core ----\n");

    ets_delay_us(10000);
    __real_system_restart_local();
}


static void print_stack(uint32_t start, uint32_t end) {
    for (uint32_t pos = start; pos < end; pos += 0x10) {
        uint32_t* values = (uint32_t*)(pos);

        // rough indicator: stack frames usually have SP saved as the second word
        bool looksLikeStackFrame = (values[2] == pos + 0x10);

        ets_printf_P(PSTR("%08x:  %08x %08x %08x %08x %c\n"),
            pos, values[0], values[1], values[2], values[3], (looksLikeStackFrame)?'<':' ');
    }
}

static void uart_write_char_d(char c) {
    uart0_write_char_d(c);
    uart1_write_char_d(c);
}

static void uart0_write_char_d(char c) {
    while (((USS(0) >> USTXC) & 0xff)) { }

    if (c == '\n') {
        USF(0) = '\r';
    }
    USF(0) = c;
}

static void uart1_write_char_d(char c) {
    while (((USS(1) >> USTXC) & 0xff) >= 0x7e) { }

    if (c == '\n') {
        USF(1) = '\r';
    }
    USF(1) = c;
}


static void raise_exception() {
    __asm__ __volatile__("\n\
        s32i    a0, %0, 0x10\n\
        s32i    a1, %0, 0x14\n\
        rsr     a0, ps\n\
        s32i    a0, %0, 0x04\n\
        s32i    a2, %0, 0x18\n\
        s32i    a3, %0, 0x1c\n\
        s32i    a4, %0, 0x20\n\
        s32i    a5, %0, 0x24\n\
        s32i    a6, %0, 0x28\n\
        s32i    a7, %0, 0x2c\n\
        s32i    a8, %0, 0x30\n\
        s32i    a9, %0, 0x34\n\
        s32i    a10, %0, 0x38\n\
        s32i    a11, %0, 0x3c\n\
        s32i    a12, %0, 0x40\n\
        s32i    a13, %0, 0x44\n\
        s32i    a14, %0, 0x48\n\
        s32i    a15, %0, 0x4c\n\
        rsr     a0, SAR\n\
        s32i    a0, %0, 0x08\n\
        rsr     a0, LITBASE\n\
        s32i    a0, %0, 0x50\n\
        rsr     a0, 176\n\
        s32i    a0, %0, 0x54\n\
        rsr     a0, 208\n\
        s32i    a0, %0, 0x58\n\
        movi    a0, .\n\
        s32i    a0, %0, 0x00\n" : : "r" (&core_regs) : "a0");

    core_regs.valid = 1;

    if (gdb_present())
        __asm__ __volatile__ ("syscall"); // triggers GDB when enabled

    s_user_reset_reason = REASON_USER_SWEXCEPTION_RST;
    ets_printf_P(PSTR("\nUser exception (panic/abort/assert)"));
    __wrap_system_restart_local();

    while (1); // never reached, needed to satisfy "noreturn" attribute
}

void abort() {
    s_abort_called = true;
    raise_exception();
}

void __unhandled_exception(const char *str) {
    s_unhandled_exception = str;
    raise_exception();
}

void __assert_func(const char *file, int line, const char *func, const char *what) {
    s_panic_file = file;
    s_panic_line = line;
    s_panic_func = func;
    s_panic_what = what;
    gdb_do_break();     /* if GDB is not present, this is a no-op */
    raise_exception();
}

void __panic_func(const char* file, int line, const char* func) {
    s_panic_file = file;
    s_panic_line = line;
    s_panic_func = func;
    s_panic_what = 0;
    gdb_do_break();     /* if GDB is not present, this is a no-op */
    raise_exception();
}

};
