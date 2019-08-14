/*
  ===========================================================================

  Copyright (C) 2019 Emvivre

  This file is part of READ_WRITE_MCx.

  READ_WRITE_MCx is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  READ_WRITE_MCx is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with READ_WRITE_MCx.  If not, see <http://www.gnu.org/licenses/>.

  ===========================================================================
*/

#include <efi.h>
#include <efilib.h>
#include "msr-index.h"

typedef uint32_t u32;
typedef uint64_t u64;

/**** FROM linux/arch/x86/include/asm/asm.h ****/

# define _ASM_EXTABLE_HANDLE(from, to, handler)

/**** FROM linux/arch/x86/include/asm/msr.h ****/

/* Using 64-bit values saves one instruction clearing the high half of low */
#define DECLARE_ARGS(val, low, high)    unsigned long low, high
#define EAX_EDX_VAL(val, low, high)     ((low) | (high) << 32)
#define EAX_EDX_RET(val, low, high)     "=a" (low), "=d" (high)

/*
 * __rdmsr() and __wrmsr() are the two primitives which are the bare minimum MSR
 * accessors and should not have any tracing or other functionality piggybacking
 * on them - those are *purely* for accessing MSRs and nothing more. So don't even
 * think of extending them - you will be slapped with a stinking trout or a frozen
 * shark will reach you, wherever you are! You've been warned.
 */
static inline unsigned long long __rdmsr(unsigned int msr)
{
 DECLARE_ARGS(val, low, high);
 asm volatile("1: rdmsr\n"
       "2:\n"
       _ASM_EXTABLE_HANDLE(1b, 2b, ex_handler_rdmsr_unsafe)
       : EAX_EDX_RET(val, low, high) : "c" (msr));
 return EAX_EDX_VAL(val, low, high);
}


static inline unsigned long long native_read_msr(unsigned int msr)
{
 unsigned long long val;
 val = __rdmsr(msr);
 #if 0
 if (msr_tracepoint_active(__tracepoint_read_msr))
  do_trace_read_msr(msr, val, 0);
 #endif
 return val;
}

#define rdmsr(msr, low, high)          \
do {                                   \
 u64 __val = native_read_msr((msr));   \
 (void)((low) = (u32)__val);           \
 (void)((high) = (u32)(__val >> 32));  \
} while (0)


#define rdmsrl(msr, val)   \
 ((val) = native_read_msr((msr)))



static inline void __wrmsr(unsigned int msr, u32 low, u32 high)
{
 asm volatile("1: wrmsr\n"
       "2:\n"
       _ASM_EXTABLE_HANDLE(1b, 2b, ex_handler_wrmsr_unsafe)
       : : "c" (msr), "a"(low), "d" (high) : "memory");
}

/* Can be uninlined because referenced by paravirt */
static inline void native_write_msr(unsigned int msr, u32 low, u32 high)
{
 __wrmsr(msr, low, high);
 #if 0
 if (msr_tracepoint_active(__tracepoint_write_msr))
  do_trace_write_msr(msr, ((u64)high << 32 | low), 0);
 #endif
}

static inline void wrmsrl(unsigned int msr, u64 val)
{
 native_write_msr(msr, (u32)(val & 0xffffffffULL), (u32)(val >> 32));
}



# define get_cs()                        \
        ({                               \
        uint64_t cs;                     \
        asm volatile (                   \
             "mov %%cs, %%rax"           \
             : "=A"(cs));                \
        cs;                              \
        })

int get_ring_level()
{
        int cpl = get_cs() & 3;
	return cpl;
}

#define DELAY_COUNTER 100000000

EFI_STATUS EFIAPI efi_main (EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
  InitializeLib(ImageHandle, SystemTable);
  int CPL = get_ring_level();
  if ( CPL != 0 ) {
	  Print(L"ERROR: wrong permission level \n");
	  Print(L"Please run the current EFI program in the ring level 0 !\n");
	  Pause();
	  return 0;
  }

  /* get number of banks */
  int nb_banks = native_read_msr(MSR_IA32_MCG_CAP) & 0xff;
  Print(L"Number of available banks : %d\n", nb_banks);

  /* clear bank */
  Print(L"Initializing all Machine Check banks ...\n");
  for( int i = 0; i < nb_banks; i++) {
	  wrmsrl( MSR_IA32_MCx_CTL(i), 0xffffffffffffffff ); // activate monitoring of all errors
	  // wrmsrl( MSR_IA32_MCx_STATUS(i), 0 ); // clear status registers
  }

  /* checking loop */
  Print(L"Waiting Machine Check errors...\n");
  Print(L"----\n");
  while( 1 ) {
	  for( int i = 0; i < nb_banks; i++) {
		  u64 r_status = native_read_msr( MSR_IA32_MCx_STATUS(i) );
		  u64 val_bit = r_status & ~(1ULL<<63);
		  if ( val_bit ) {
			  Print(L"Error detected into bank %d : %016lX\n", i, r_status);
			  int is_error_uncorrected = (r_status >> 61) & 1;
			  if ( is_error_uncorrected ) {
				  Print(L"CPU was unable to fix this error.\n");
			  } else {
				  Print(L"Fixed by the CPU.\n");
			  }
			  int mca_err_code = r_status & 0xffff;
			  Print(L"  MCA error code : %04X : ", mca_err_code);
			  switch ( mca_err_code ) {
			  case 0: Print(L"No Error\n"); break;
			  case 1: Print(L"Unclassified\n"); break;
			  case 2: Print(L"Microcode ROM Parity Error\n"); break;
			  case 3: Print(L"External Error\n"); break;
			  case 4: Print(L"FRC Error\n"); break;
			  case 0x40: Print(L"Internal Timer Error\n"); break;
			  default:
				  if ( mca_err_code > 0x40 && mca_err_code < 0x80 ) {
					  Print(L"Internal Unclassified\n");
				  } else {
					  Print(L"ERROR UNKNOWN MCA ERROR CODE!\n");
				  }
			  }
			  int model_specific_err_code = (r_status >> 16) & 0xffff;
			  Print(L"  Model-specific error code : %04X\n", model_specific_err_code);

			  u64 addr_bit = r_status & ~(1ULL<<58);
			  if ( addr_bit ) {
				  u64 r_addr = native_read_msr( MSR_IA32_MCx_ADDR(i) );
				  Print(L"  ADDR: %016lX\n", r_addr);
			  }
			  #if 0
			  u64 misc_bit = r_status & ~(1ULL<<59);
			  if ( misc_bit ) {
				  u64 r_misc =  native_read_msr( MSR_IA32_MCx_MISC(i) );
				  Print(L"  MISC: %016lX\n", r_misc);
			  }
			  #endif
			  wrmsrl( MSR_IA32_MCx_STATUS(i), 0 ); // reset associated status register
			  Print(L"----\n");
			  // for( int j = 0; j < 1000000000; j++) ; // waiting
			  for( int j = 0; j < DELAY_COUNTER; j++) ; // waiting
		  }
	  }
	  for( int j = 0; j < DELAY_COUNTER; j++) ; // waiting
	  Print(L"----\n");
  }
  Print(L"-----\n");
  return EFI_SUCCESS;
}
