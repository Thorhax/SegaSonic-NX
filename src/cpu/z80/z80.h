// license:BSD-3-Clause
// copyright-holders:Juergen Buchmueller
#ifndef MAME_CPU_Z80_Z80_H
#define MAME_CPU_Z80_Z80_H

#pragma once

// Port: MAME device framework removed (marked "Port:"); memory through
// segas32::Bus8 interfaces; no daisy chain, debugger or callbacks.
#include "cpu/v60/mame_shim.h"

#include <functional>

enum
{
	Z80_INPUT_LINE_WAIT = INPUT_LINE_IRQ0 + 1,
	Z80_INPUT_LINE_BUSREQ, // Zilog renamed BUSRQ to BUSREQ and BUSAK to BUSACK in 1981

	Z80_INPUT_LINE_MAX
};



class z80_device
{
public:
	// Port: construction on the board's buses; `irq_vector` is the vector
	// given with set_input_line_and_vector (MAME's standard_irq_callback).
	z80_device(segas32::Bus8 &program, segas32::Bus8 &io);
	void reset() { device_reset(); }
	int run(int cycles);                    // returns cycles used (MAME execute_run)
	void set_input(int inputnum, int state) { execute_set_input(inputnum, state); }
	int irq_vector = 0xff;
	std::function<void(z80_device &)> trace; // Port: called before each instruction (tracing)
	void abort_run() { m_icount = 0; }
	int cycles_left() const { return m_icount; }
	uint16_t pc() const { return m_pc.w; }
	uint16_t sp() const { return m_sp.w; }
	uint16_t af() { m_af.b.l = get_f(); return m_af.w; }
	uint16_t bc() const { return m_bc.w; }
	uint16_t de() const { return m_de.w; }
	uint16_t hl() const { return m_hl.w; }

protected:
	void device_start();
	void device_reset();
	void execute_run();
	void execute_set_input(int inputnum, int state);

	// Port: stand-ins for what the generated code calls.
	struct null_cb {
		bool isunset() const { return true; }
		template <typename... T> void operator()(T &&...) const {}
	};
	struct null_debug { void interrupt_hook(int, int) {} };
	struct device_execute_interface {
		struct dev { null_debug *debug() { static null_debug d; return &d; } };
		static dev &device() { static dev d; return d; }
	};
	static constexpr bool debugger_enabled() { return false; }
	void debugger_instruction_hook(offs_t) { if (trace) trace(*this); }
	void debugger_wait_hook() {}
	static constexpr bool access_to_be_redone() { return false; }
	static constexpr bool daisy_chain_present() { return false; }
	struct device_z80daisy_interface { int z80daisy_irq_ack() { return 0xff; } void z80daisy_irq_reti() {} };
	device_z80daisy_interface *daisy_get_irq_device() { return nullptr; }
	void daisy_call_reti_device() {}
	int daisy_update_irq_state() { return CLEAR_LINE; }
	int standard_irq_callback(int, offs_t) { return irq_vector; }
	uint64_t total_cycles() const { return m_total_cycles; }
	struct space8 {
		segas32::Bus8 *bus = nullptr;
		u8 read_byte(offs_t a) { return bus->read8(u16(a)); }
		u8 read_interruptible(offs_t a) { return bus->read8(u16(a)); }
		void write_interruptible(offs_t a, u8 v) { bus->write8(u16(a), v); }
	};
	uint64_t m_total_cycles = 0;

	void illegal_1();
	void illegal_2();
	template <u8 Bit, bool State> void set_service_attention() { static_assert(Bit < 8, "out of range bit index"); if (State) m_service_attention |= (1 << Bit); else m_service_attention &= ~(1 << Bit); };
	template <u8 Bit> bool get_service_attention() { static_assert(Bit < 8, "out of range bit index"); return m_service_attention & (1 << Bit); };

	void halt();
	void leave_halt();
	void inc(u8 &r);
	void dec(u8 &r);
	void rlca();
	void rrca();
	void rla();
	void rra();
	void add_a(u8 value);
	void adc_a(u8 value);
	void sub_a(u8 value);
	void sbc_a(u8 value);
	void neg();
	void daa();
	void and_a(u8 value);
	void or_a(u8 value);
	void xor_a(u8 value);
	void cp(u8 value);
	void exx();
	u8 rlc(u8 value);
	u8 rrc(u8 value);
	u8 rl(u8 value);
	u8 rr(u8 value);
	u8 sla(u8 value);
	u8 sra(u8 value);
	u8 sll(u8 value);
	u8 srl(u8 value);
	void bit(int bit, u8 value);
	void bit_hl(int bit, u8 value);
	void bit_xy(int bit, u8 value);
	u8 res(int bit, u8 value);
	u8 set(int bit, u8 value);
	void ei();
	void block_io_interrupted_flags();

	virtual u8 data_read(u16 addr);
	virtual void data_write(u16 addr, u8 value);
	virtual u8 stack_read(u16 addr) { return data_read(addr); }
	virtual void stack_write(u16 addr, u8 value) { return data_write(addr, value); }
	virtual u8 opcode_read();
	virtual u8 arg_read();

	// address spaces (Port: ours)
	space8 m_args, m_opcodes, m_data, m_io;
	null_cb m_irqack_cb, m_refresh_cb, m_nomreq_cb, m_halt_cb, m_busack_cb;

	static constexpr u8 SA_BUSREQ        = 0;
	static constexpr u8 SA_NMI_PENDING   = 1;
	static constexpr u8 SA_IRQ_ON        = 2;
	static constexpr u8 SA_HALT          = 3;
	static constexpr u8 SA_AFTER_EI      = 4;
	static constexpr u8 SA_AFTER_LDAIR   = 5;
	static constexpr u8 SA_NSC800_IRQ_ON = 6;
	u8 m_service_attention; // bitmap for required handling in service step

	PAIR16 m_prvpc;
	PAIR16 m_pc;
	PAIR16 m_sp;
	PAIR16 m_af;
	PAIR16 m_bc;
	PAIR16 m_de;
	PAIR16 m_hl;
	PAIR16 m_ix;
	PAIR16 m_iy;
	PAIR16 m_wz;
	PAIR16 m_af2;
	PAIR16 m_bc2;
	PAIR16 m_de2;
	PAIR16 m_hl2;
	u8     m_r;
	u8     m_r2;
	bool   m_iff1;
	bool   m_iff2;
	u8     m_halt;
	u8     m_im;
	u8     m_i;
	u8     m_nmi_state;    // nmi pin state
	u8     m_irq_state;    // irq pin state
	int    m_wait_state;   // wait pin state
	int    m_busreq_state; // bus request pin state
	u8     m_busack_state; // bus acknowledge pin state
	u16    m_ea;

	struct
	{
		u8 s_val;  // bit 7, other bits = don't care
		u8 z_val;  // 0 or not 0
		u8 yx_val; // bit 5 for Y, bit 3 for X, other bits = don't care
		u8 h_val;  // bit 4, other bits = don't care
		u8 pv_val; // overflow case set in the way that pv() returns desired value
		bool n;
		bool c;

		u8 q;      // CCF/SCF YX mask
		u8 qtemp;

		u8 s() const { return s_val & 0x80; }
		u8 z() const { return z_val ? 0 : 0x40; }
		u8 yx() const { return yx_val & 0x28; }
		u8 h() const { return h_val & 0x10; }
		u8 pv() const {
			u8 val = pv_val;
			val ^= val >> 4;
			val ^= val << 2;
			val ^= val >> 1;
			return ~val & 0x04;
		}
	} m_f;
	u8 get_f();
	void set_f(u8 f);

	int    m_icount;
	int    m_tmp_irq_vector;
	PAIR16 m_shared_data;
	PAIR16 m_shared_data2;
	u8     m_rtemp;

	u32    m_ref;
	u8     m_m1_cycles;
	u8     m_mreq_cycles;
	u8     m_iorq_cycles;
};




#endif // MAME_CPU_Z80_Z80_H
