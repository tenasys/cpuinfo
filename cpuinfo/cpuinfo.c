#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <rt.h>

/*
 * Vendors of processor.
 */
#define	CPU_VENDOR_NSC		0x100b		/* NSC */
#define	CPU_VENDOR_IBM		0x1014		/* IBM */
#define	CPU_VENDOR_AMD		0x1022		/* AMD */
#define	CPU_VENDOR_SIS		0x1039		/* SiS */
#define	CPU_VENDOR_UMC		0x1060		/* UMC */
#define	CPU_VENDOR_NEXGEN	0x1074		/* Nexgen */
#define	CPU_VENDOR_CYRIX	0x1078		/* Cyrix */
#define	CPU_VENDOR_IDT		0x111d		/* Centaur/IDT/VIA */
#define	CPU_VENDOR_TRANSMETA	0x1279		/* Transmeta */
#define	CPU_VENDOR_INTEL	0x8086		/* Intel */
#define	CPU_VENDOR_RISE		0xdead2bad	/* Rise */
#define	CPU_VENDOR_CENTAUR	CPU_VENDOR_IDT

/*
 * CPUID manufacturers identifiers
 */
#define	AMD_VENDOR_ID		"AuthenticAMD"
#define	CENTAUR_VENDOR_ID	"CentaurHauls"
#define	CYRIX_VENDOR_ID		"CyrixInstead"
#define	INTEL_VENDOR_ID		"GenuineIntel"
#define	NEXGEN_VENDOR_ID	"NexGenDriven"
#define	NSC_VENDOR_ID		"Geode by NSC"
#define	RISE_VENDOR_ID		"RiseRiseRise"
#define	SIS_VENDOR_ID		"SiS SiS SiS "
#define	TRANSMETA_VENDOR_ID	"GenuineTMx86"
#define	UMC_VENDOR_ID		"UMC UMC UMC "

/*
 * CPUID instruction 1 eax info
 */
#define	CPUID_STEPPING		0x0000000f
#define	CPUID_MODEL		0x000000f0
#define	CPUID_FAMILY		0x00000f00
#define	CPUID_EXT_MODEL		0x000f0000
#define	CPUID_EXT_FAMILY	0x0ff00000
#define	CPUID_TO_MODEL(id) \
	((((id)& CPUID_MODEL) >> 4) | \
	((((id)& CPUID_FAMILY) >= 0x600) ? \
	(((id)& CPUID_EXT_MODEL) >> 12) : 0))
#define	CPUID_TO_FAMILY(id) \
	((((id)& CPUID_FAMILY) >> 8) + \
	((((id)& CPUID_FAMILY) == 0xf00) ? \
	(((id)& CPUID_EXT_FAMILY) >> 20) : 0))

/*
 * Various MSRs
 */
#define IA32_MPERF				0xe7
#define IA32_APERF				0xe8
#define	IA32_MISC_ENABLE		0x1a0
#define IA32_ENERGY_PERF_BIAS	0x1b0
#define IA32_PM_ENABLE			0x770
#define IA32_PKG_HCD_CTL		0xdb0

uint32_t cpu_id;
uint32_t cpu_high = 0;
uint32_t cpu_exthigh = 0;
uint32_t cpu_vendor_id = -1;
uint32_t cpu_feature = 0;
uint32_t cpu_feature2 = 0;
uint32_t cpu_procinfo2 = 0;
static uint32_t _cpunamebuf[4];
static char *cpu_vendor;
static char brand[49];

uint32_t amd_feature;
uint32_t amd_feature2;
uint32_t amd_pminfo;

static struct {
	char	*vendor;
	u_int	vendor_id;
} cpu_vendors[] = {
	{ INTEL_VENDOR_ID, CPU_VENDOR_INTEL },	/* GenuineIntel */
	{ AMD_VENDOR_ID, CPU_VENDOR_AMD },	/* AuthenticAMD */
	{ CENTAUR_VENDOR_ID, CPU_VENDOR_CENTAUR },	/* CentaurHauls */
	{ NSC_VENDOR_ID, CPU_VENDOR_NSC },	/* Geode by NSC */
	{ CYRIX_VENDOR_ID, CPU_VENDOR_CYRIX },	/* CyrixInstead */
	{ TRANSMETA_VENDOR_ID, CPU_VENDOR_TRANSMETA },	/* GenuineTMx86 */
	{ SIS_VENDOR_ID, CPU_VENDOR_SIS },	/* SiS SiS SiS  */
	{ UMC_VENDOR_ID, CPU_VENDOR_UMC },	/* UMC UMC UMC  */
	{ NEXGEN_VENDOR_ID, CPU_VENDOR_NEXGEN },	/* NexGenDriven */
	{ RISE_VENDOR_ID, CPU_VENDOR_RISE },	/* RiseRiseRise */
#if 0
	/* XXX CPUID 8000_0000h and 8086_0000h, not 0000_0000h */
	{ "TransmetaCPU", CPU_VENDOR_TRANSMETA },
#endif
};

void
getcpuidx(unsigned select, unsigned subselect, unsigned *regs)
{
	__asm mov eax, select;
	__asm mov ecx, subselect;
	__asm cpuid;
	__asm mov edi, regs;
	__asm mov[edi], eax;
	__asm mov[edi + 4], ebx;
	__asm mov[edi + 8], ecx;
	__asm mov[edi + 12], edx;
}

unsigned 
getcpuid(unsigned select, unsigned *regs)
{
	getcpuidx(select, 0, regs);

	return regs[0];
}

struct msrrec {
	uint32_t regnum;
	uint32_t reserved;
	uint64_t regval;
};

void do_rdmsr(LPVOID *param)
{
	struct msrrec *msrrec = (struct msrrec *)param;
	uint32_t reg = msrrec->regnum;
	uint32_t vallo;
	uint32_t valhi;
	__asm mov ecx, reg;
	__asm rdmsr;
	__asm mov vallo, eax;
	__asm mov valhi, edx;
	msrrec->regval = (((uint64_t)valhi) << 32) | vallo;
}

uint64_t rdmsr(uint32_t msr)
{
	struct msrrec msrrec;
	extern BOOLEAN Callback(LPPROC lpfnEntry, LPVOID lpParameter);

	msrrec.regnum = msr;
	if (Callback(do_rdmsr, &msrrec))
		return msrrec.regval;

	return (uint64_t)(-1);
}

void do_wrmsr(LPVOID *param)
{
	struct msrrec *msrrec = (struct msrrec *)param;
	uint32_t reg = msrrec->regnum;
	uint32_t vallo = (uint32_t)msrrec->regval;
	uint32_t valhi = (uint32_t)(msrrec->regval >> 32);
	__asm mov ecx, reg;
	__asm mov eax, vallo;
	__asm mov edx, valhi;
	__asm wrmsr;
}

void wrmsr(uint32_t msr, uint64_t value)
{
	struct msrrec msrrec;
	extern BOOLEAN Callback(LPPROC lpfnEntry, LPVOID lpParameter);

	msrrec.regnum = msr;
	msrrec.regval = value;
	Callback(do_wrmsr, &msrrec);
}


#define HTT_BIT (1 << 28)

int HT_detect(unsigned *eax)
{
	char vendor_string[] = "GenuineIntel";
	unsigned regs[4];
	int max;

	if (getcpuid(0, regs)) {
		max = regs[0];
		if (memcmp("GenuntelineI", &regs[1], 12) == 0) {
			if (max) {
				getcpuid(1, regs);
				if (eax)
					*eax = regs[0];
				if (regs[3] & HTT_BIT)
					return 1;
			}
		}
	}

	return 0;
}

unsigned char logical_processors(void)
{
	unsigned regs[4];

	if (!HT_detect(NULL))
		return 1;
	getcpuid(1, regs);
	return (regs[1] & 0x00ff0000) >> 16;
}

unsigned char get_apic_id(void)
{
	unsigned regs[4];

	if (!HT_detect(NULL))
		return (unsigned char)(-1);
	getcpuid(1, regs);
	return (regs[1] & 0xff000000) >> 24;
}

#pragma warning(push)
#pragma warning(disable : 4474)
#pragma warning(disable : 4476)

void features(void)
{
	printf("\nFeatures=0x%B\n", cpu_feature,
		"\020"
		"\001FPU"	/* Integral FPU */
		"\002VME"	/* Extended VM86 mode support */
		"\003DE"	/* Debugging Extensions (CR4.DE) */
		"\004PSE"	/* 4MByte page tables */
		"\005TSC"	/* Timestamp counter */
		"\006MSR"	/* Machine specific registers */
		"\007PAE"	/* Physical address extension */
		"\010MCE"	/* Machine Check support */
		"\011CX8"	/* CMPEXCH8 instruction */
		"\012APIC"	/* SMP local APIC */
		"\013oldMTRR"	/* Previous implementation of MTRR */
		"\014SEP"	/* Fast System Call */
		"\015MTRR"	/* Memory Type Range Registers */
		"\016PGE"	/* PG_G (global bit) support */
		"\017MCA"	/* Machine Check Architecture */
		"\020CMOV"	/* CMOV instruction */
		"\021PAT"	/* Page attributes table */
		"\022PSE36"	/* 36 bit address space support */
		"\023PN"	/* Processor Serial number */
		"\024CLFLUSH"	/* Has the CLFLUSH instruction */
		"\025<b20>"
		"\026DTS"	/* Debug Trace Store */
		"\027ACPI"	/* ACPI support */
		"\030MMX"	/* MMX instructions */
		"\031FXSR"	/* FXSAVE/FXRSTOR */
		"\032SSE"	/* Streaming SIMD Extensions */
		"\033SSE2"	/* Streaming SIMD Extensions #2 */
		"\034SS"	/* Self snoop */
		"\035HTT"	/* Hyperthreading (see EBX bit 16-23) */
		"\036TM"	/* Thermal Monitor clock slowdown */
		"\037IA64"	/* CPU can execute IA64 instructions */
		"\040PBE"	/* Pending Break Enable */
		);

	if (cpu_feature2 != 0) {
		printf("\nFeatures2=0x%B\n", cpu_feature2,
			"\020"
			"\001SSE3"	/* SSE3 */
			"\002PCLMULQDQ"	/* Carry-Less Mul Quadword */
			"\003DTES64"	/* 64-bit Debug Trace */
			"\004MON"	/* MONITOR/MWAIT Instructions */
			"\005DS_CPL"	/* CPL Qualified Debug Store */
			"\006VMX"	/* Virtual Machine Extensions */
			"\007SMX"	/* Safer Mode Extensions */
			"\010EST"	/* Enhanced SpeedStep */
			"\011TM2"	/* Thermal Monitor 2 */
			"\012SSSE3"	/* SSSE3 */
			"\013CNXT-ID"	/* L1 context ID available */
			"\014<b11>"
			"\015FMA"	/* Fused Multiply Add */
			"\016CX16"	/* CMPXCHG16B Instruction */
			"\017xTPR"	/* Send Task Priority Messages*/
			"\020PDCM"	/* Perf/Debug Capability MSR */
			"\021<b16>"
			"\022PCID"	/* Process-context Identifiers*/
			"\023DCA"	/* Direct Cache Access */
			"\024SSE4.1"	/* SSE 4.1 */
			"\025SSE4.2"	/* SSE 4.2 */
			"\026x2APIC"	/* xAPIC Extensions */
			"\027MOVBE"	/* MOVBE Instruction */
			"\030POPCNT"	/* POPCNT Instruction */
			"\031TSCDLT"	/* TSC-Deadline Timer */
			"\032AESNI"	/* AES Crypto */
			"\033XSAVE"	/* XSAVE/XRSTOR States */
			"\034OSXSAVE"	/* OS-Enabled State Management*/
			"\035AVX"	/* Advanced Vector Extensions */
			"\036F16C"	/* Half-precision conversions */
			"\037RDRAND"	/* RDRAND Instruction */
			"\040HV"	/* Hypervisor */
			);
	}
}

#pragma warning(pop)

static void
print_INTEL_TLB(u_int data)
{
	switch (data) {
	default:
		printf("\n(undecoded type %02x)", data);
		break;
	case 0x0:
		break;
	case 0x1:
		printf("\n%02x: Instruction TLB: 4 KB pages, 4-way set associative, 32 entries", data);
		break;
	case 0x2:
		printf("\n%02x: Instruction TLB: 4 MB pages, fully associative, 2 entries", data);
		break;
	case 0x3:
		printf("\n%02x: Data TLB: 4 KB pages, 4-way set associative, 64 entries", data);
		break;
	case 0x4:
		printf("\n%02x: Data TLB: 4 MB Pages, 4-way set associative, 8 entries", data);
		break;
	case 0x6:
		printf("\n%02x: 1st-level instruction cache: 8 KB, 4-way set associative, 32 byte line size", data);
		break;
	case 0x8:
		printf("\n%02x: 1st-level instruction cache: 16 KB, 4-way set associative, 32 byte line size", data);
		break;
	case 0x09:
		printf("\n%02x: 1st-level instruction cache: 32KBytes, 4-way set associative, 64 byte line size", data);
		break;
	case 0xa:
		printf("\n%02x: 1st-level data cache: 8 KB, 2-way set associative, 32 byte line size", data);
		break;
	case 0x0b:
		printf("\n%02x: Instruction TLB: 4 MByte pages, 4-way set associative, 4 entries", data);
		break;
	case 0xc:
		printf("\n%02x: 1st-level data cache: 16 KB, 4-way set associative, 32 byte line size", data);
		break;
	case 0x0d:
		printf("\n%02x: 1st-level data cache: 16 KBytes, 4-way set associative, 64 byte line size", data);
		break;
	case 0x0e:
		printf("\n%02x: 1st-level data cache: 24 KBytes, 6-way set associative, 64 byte line size", data);
		break;
	case 0x1d:
		printf("\n%02x: 2nd-level cache: 128 KBytes, 2-way set associative, 64 byte line size", data);
		break;
	case 0x21:
		printf("\n%02x: 2nd-level cache: 256 KBytes, 8-way set associative, 64 byte line size", data);
		break;
	case 0x22:
		printf("\n%02x: 3rd-level cache: 512 KB, 4-way set associative, sectored cache, 64 byte line size", data);
		break;
	case 0x23:
		printf("\n%02x: 3rd-level cache: 1 MB, 8-way set associative, sectored cache, 64 byte line size", data);
		break;
	case 0x24:
		printf("\n%02x: 2nd-level cache: 1 MBytes, 16-way set associative, 64 byte line size", data);
		break;
	case 0x25:
		printf("\n%02x: 3rd-level cache: 2 MB, 8-way set associative, sectored cache, 64 byte line size", data);
		break;
	case 0x29:
		printf("\n%02x: 3rd-level cache: 4 MB, 8-way set associative, sectored cache, 64 byte line size", data);
		break;
	case 0x2c:
		printf("\n%02x: 1st-level data cache: 32 KB, 8-way set associative, 64 byte line size", data);
		break;
	case 0x30:
		printf("\n%02x: 1st-level instruction cache: 32 KB, 8-way set associative, 64 byte line size", data);
		break;
	case 0x39:
		printf("\n%02x: 2nd-level cache: 128 KB, 4-way set associative, sectored cache, 64 byte line size", data);
		break;
	case 0x3b:
		printf("\n%02x: 2nd-level cache: 128 KB, 2-way set associative, sectored cache, 64 byte line size", data);
		break;
	case 0x3c:
		printf("\n%02x: 2nd-level cache: 256 KB, 4-way set associative, sectored cache, 64 byte line size", data);
		break;
	case 0x40:
		printf("\n%02x: No 2nd-level cache or, if processor contains a valid 2nd-level cache, no 3rd-level cache", data);
		break;
	case 0x41:
		printf("\n%02x: 2nd-level cache: 128 KB, 4-way set associative, 32 byte line size", data);
		break;
	case 0x42:
		printf("\n%02x: 2nd-level cache: 256 KB, 4-way set associative, 32 byte line size", data);
		break;
	case 0x43:
		printf("\n%02x: 2nd-level cache: 512 KB, 4-way set associative, 32 byte line size", data);
		break;
	case 0x44:
		printf("\n%02x: 2nd-level cache: 1 MB, 4-way set associative, 32 byte line size", data);
		break;
	case 0x45:
		printf("\n%02x: 2nd-level cache: 2 MB, 4-way set associative, 32 byte line size", data);
		break;
	case 0x46:
		printf("\n%02x: 3rd-level cache: 4 MB, 4-way set associative, 64 byte line size", data);
		break;
	case 0x47:
		printf("\n%02x: 3rd-level cache: 8 MB, 8-way set associative, 64 byte line size", data);
		break;
	case 0x48:
		printf("\n%02x: 2nd-level cache: 3MByte, 12-way set associative, 64 byte line size", data);
		break;
	case 0x49:
		printf("\n%02x: ** 2nd-level cache: 4 MByte, 16-way set associative, 64 byte line size **", data);
		break;
	case 0x4a:
		printf("\n%02x: 3rd-level cache: 6MByte, 12-way set associative, 64 byte line size", data);
		break;
	case 0x4b:
		printf("\n%02x: 3rd-level cache: 8MByte, 16-way set associative, 64 byte line size", data);
		break;
	case 0x4c:
		printf("\n%02x: 3rd-level cache: 12MByte, 12-way set associative, 64 byte line size", data);
		break;
	case 0x4d:
		printf("\n%02x: 3rd-level cache: 16MByte, 16-way set associative, 64 byte line size", data);
		break;
	case 0x4e:
		printf("\n%02x: 2nd-level cache: 6MByte, 24-way set associative, 64 byte line size", data);
		break;
	case 0x4f:
		printf("\n%02x: Instruction TLB: 4 KByte pages, 32 entries", data);
		break;
	case 0x50:
		printf("\n%02x: Instruction TLB: 4 KB, 2 MB or 4 MB pages, fully associative, 64 entries", data);
		break;
	case 0x51:
		printf("\n%02x: Instruction TLB: 4 KB, 2 MB or 4 MB pages, fully associative, 128 entries", data);
		break;
	case 0x52:
		printf("\n%02x: Instruction TLB: 4 KB, 2 MB or 4 MB pages, fully associative, 256 entries", data);
		break;
	case 0x55:
		printf("\n%02x: Instruction TLB: 2-MByte or 4-MByte pages, fully associative, 7 entrie", data);
		break;
	case 0x56:
		printf("\n%02x: Data TLB0: 4 MByte pages, 4-way set associative, 16 entries", data);
		break;
	case 0x57:
		printf("Data TLB0: 4 KByte pages, 4-way associative, 16 entries%02x: ", data);
		break;
	case 0x59:
		printf("\n%02x: Data TLB0: 4 KByte pages, fully associative, 16 entries", data);
		break;
	case 0x5a:
		printf("\n%02x: Data TLB0: 2-MByte or 4 MByte pages, 4-way set associative, 32 entries", data);
		break;
	case 0x5b:
		printf("\n%02x: Data TLB: 4 KB or 4 MB pages, fully associative, 64 entries", data);
		break;
	case 0x5c:
		printf("\n%02x: Data TLB: 4 KB or 4 MB pages, fully associative, 128 entries", data);
		break;
	case 0x5d:
		printf("\n%02x: Data TLB: 4 KB or 4 MB pages, fully associative, 256 entries", data);
		break;
	case 0x60:
		printf("\n%02x: 1st-level data cache: 16 KB, 8-way set associative, sectored cache, 64 byte line size", data);
		break;
	case 0x61:
		printf("\n%02x: Instruction TLB: 4 KByte pages, fully associative, 48 entries", data);
		break;
	case 0x63:
		printf("\n%02x: Data TLB: 1 GByte pages, 4-way set associative, 4 entries", data);
		break;
	case 0x66:
		printf("\n%02x: 1st-level data cache: 8 KB, 4-way set associative, sectored cache, 64 byte line size", data);
		break;
	case 0x67:
		printf("\n%02x: 1st-level data cache: 16 KB, 4-way set associative, sectored cache, 64 byte line size", data);
		break;
	case 0x68:
		printf("\n%02x: 1st-level data cache: 32 KB, 4 way set associative, sectored cache, 64 byte line size", data);
		break;
	case 0x70:
		printf("\n%02x: Trace cache: 12K-uops, 8-way set associative", data);
		break;
	case 0x71:
		printf("\n%02x: Trace cache: 16K-uops, 8-way set associative", data);
		break;
	case 0x72:
		printf("\n%02x: Trace cache: 32K-uops, 8-way set associative", data);
		break;
	case 0x76:
		printf("\n%02x: Instruction TLB: 2M/4M pages, fully associative, 8 entries", data);
		break;
	case 0x78:
		printf("\n%02x: 2nd-level cache: 1 MB, 4-way set associative, 64-byte line size", data);
		break;
	case 0x79:
		printf("\n%02x: 2nd-level cache: 128 KB, 8-way set associative, sectored cache, 64 byte line size", data);
		break;
	case 0x7a:
		printf("\n%02x: 2nd-level cache: 256 KB, 8-way set associative, sectored cache, 64 byte line size", data);
		break;
	case 0x7b:
		printf("\n%02x: 2nd-level cache: 512 KB, 8-way set associative, sectored cache, 64 byte line size", data);
		break;
	case 0x7c:
		printf("\n%02x: 2nd-level cache: 1 MB, 8-way set associative, sectored cache, 64 byte line size", data);
		break;
	case 0x7d:
		printf("\n%02x: 2nd-level cache: 2-MB, 8-way set associative, 64-byte line size", data);
		break;
	case 0x7f:
		printf("\n%02x: 2nd-level cache: 512-KB, 2-way set associative, 64-byte line size", data);
		break;
	case 0x80:
		printf("\n%02x: 2nd-level cache: 512 KByte, 8-way set associative, 64-byte line size", data);
		break;
	case 0x82:
		printf("\n%02x: 2nd-level cache: 256 KB, 8-way set associative, 32 byte line size", data);
		break;
	case 0x83:
		printf("\n%02x: 2nd-level cache: 512 KB, 8-way set associative, 32 byte line size", data);
		break;
	case 0x84:
		printf("\n%02x: 2nd-level cache: 1 MB, 8-way set associative, 32 byte line size", data);
		break;
	case 0x85:
		printf("\n%02x: 2nd-level cache: 2 MB, 8-way set associative, 32 byte line size", data);
		break;
	case 0x86:
		printf("\n%02x: 2nd-level cache: 512 KB, 4-way set associative, 64 byte line size", data);
		break;
	case 0x87:
		printf("\n%02x: 2nd-level cache: 1 MB, 8-way set associative, 64 byte line size", data);
		break;
	case 0xa0:
		printf("\n%02x: DTLB: 4k pages, fully associative, 32 entries", data);
		break;
	case 0xb0:
		printf("\n%02x: Instruction TLB: 4 KB Pages, 4-way set associative, 128 entries", data);
		break;
	case 0xb1:
		printf("\n%02x: Instruction TLB: 2M pages, 4-way, 8 entries or 4M pages, 4-way, 4 entries", data);
		break;
	case 0xb2:
		printf("\n%02x: Instruction TLB: 4KByte pages, 4-way set associative, 64 entries", data);
		break;
	case 0xb3:
		printf("\n%02x: Data TLB: 4 KB Pages, 4-way set associative, 128 entries", data);
		break;
	case 0xb4:
		printf("\n%02x: Data TLB1: 4 KByte pages, 4-way associative, 256 entries", data);
		break;
	case 0xb5:
		printf("\n%02x: Instruction TLB: 4KByte pages, 8-way set associative, 64 entries", data);
		break;
	case 0xb6:
		printf("\n%02x: Instruction TLB: 4KByte pages, 8-way set associative, 128 entries", data);
		break;
	case 0xba:
		printf("\n%02x: Data TLB1: 4 KByte pages, 4-way associative, 64 entries", data);
		break;
	case 0xC0:
		printf("\n%02x: TLB Data TLB : 4 KByte and 4 MByte pages, 4 - way associative, 8 entries", data);
		break;
	case 0xC1:
		printf("\n%02x: STLB Shared 2nd - Level TLB : 4 KByte / 2MByte pages, 8 - way associative, 1024 entries", data);
		break;
	case 0xC2:
		printf("\n%02x: DTLB DTLB : 4 KByte / 2 MByte pages, 4 - way associative, 16 entries", data);
		break;
	case 0xCA:
		printf("\n%02x: STLB Shared 2nd - Level TLB : 4 KByte pages, 4 - way associative, 512 entries", data);
		break;
	case 0xD0:
		printf("\n%02x: Cache 3rd - level cache : 512 KByte, 4 - way set associative, 64 byte line size", data);
		break;
	case 0xD1:
		printf("\n%02x: Cache 3rd - level cache : 1 MByte, 4 - way set associative, 64 byte line size", data);
		break;
	case 0xD2:
		printf("\n%02x: Cache 3rd - level cache : 2 MByte, 4 - way set associative, 64 byte line size", data);
		break;
	case 0xD6:
		printf("\n%02x: Cache 3rd - level cache : 1 MByte, 8 - way set associative, 64 byte line size", data);
		break;
	case 0xD7:
		printf("\n%02x: Cache 3rd - level cache : 2 MByte, 8 - way set associative, 64 byte line size", data);
		break;
	case 0xD8:
		printf("\n%02x: Cache 3rd - level cache : 4 MByte, 8 - way set associative, 64 byte line size", data);
		break;
	case 0xDC:
		printf("\n%02x: Cache 3rd - level cache : 1.5 MByte, 12 - way set associative, 64 byte line size", data);
		break;
	case 0xDD:
		printf("\n%02x: Cache 3rd - level cache : 3 MByte, 12 - way set associative, 64 byte line size", data);
		break;
	case 0xDE:
		printf("\n%02x: Cache 3rd - level cache : 6 MByte, 12 - way set associative, 64 byte line size", data);
		break;
	case 0xE2:
		printf("\n%02x: Cache 3rd - level cache : 2 MByte, 16 - way set associative, 64 byte line size", data);
		break;
	case 0xE3:
		printf("\n%02x: Cache 3rd - level cache : 4 MByte, 16 - way set associative, 64 byte line size", data);
		break;
	case 0xE4:
		printf("\n%02x: Cache 3rd - level cache : 8 MByte, 16 - way set associative, 64 byte line size", data);
		break;
	case 0xEA:
		printf("\n%02x: Cache 3rd - level cache : 12MByte, 24 - way set associative, 64 byte line size", data);
		break;
	case 0xEB:
		printf("\n%02x: Cache 3rd - level cache : 18MByte, 24 - way set associative, 64 byte line size", data);
		break;
	case 0xEC:
		printf("\n%02x: Cache 3rd - level cache : 24MByte, 24 - way set associative, 64 byte line size", data);
		break;
	case 0xF0:
		printf("\n%02x: Prefetch 64 - Byte prefetching", data);
		break;
	case 0xF1:
		printf("\n%02x: Prefetch 128 - Byte prefetching", data);
		break;
	case 0xFF:
		printf("\n%02x: General CPUID leaf 2 does not report cache information", data);
		break;
	}
}

static void print_INTEL_info(void)
{
	u_int regs[4], regs2[4], cpuid6[4];
	u_int rounds, regnum, set;
	u_int nwaycode, nway;

	printf("\nEIST: Enhanced Intel SpeedStep Technology: ");
	getcpuid(01, regs);
	if ((regs[2] & 0x00000080) != 0) {
		printf("present: %sabled\n", (rdmsr(IA32_MISC_ENABLE) & (1 << 16)) != 0 ? "en" : "dis");
	}
	else {
		printf("NOT present\n");
	}

	printf("\nIDA: Intel Dynamic Acceleration: ");
	if (cpu_high >= 6) {
		getcpuid(6, cpuid6);
		if ((cpuid6[0] & 2) != 0) {
			printf("present: %sabled\n", (rdmsr(IA32_MISC_ENABLE) & ((QWORD)1 << 38)) != 0 ? "en" : "dis");
		}
		else {
			printf("NOT present\n");
		}
	}
	else {
		printf("NOT detected\n");
	}

	printf("\nIntel Turbo Boost Technology: ");
	if (cpu_high >= 6 && (cpuid6[2] & 8) != 0) {
		printf("present: ENERGY_PERF_BIAS = %u\n", (DWORD)(rdmsr(IA32_ENERGY_PERF_BIAS) & 0xf));
	}
	else {
		printf("NOT present\n");
	}

	printf("\nHWP: Hardware-controlled Performance States: ");
	if (cpu_high >= 6 && (cpuid6[0] & (1 << 7)) != 0) {
		printf("present: %sabled\n", (rdmsr(IA32_PM_ENABLE) != 0) ? "en" : "dis");
	}
	else {
		printf("NOT present\n");
	}

	printf("\nHDC: Hardware Duty-Cycling: ");
	if (cpu_high >= 6 && (cpuid6[0] & (1 << 13)) != 0) {
		printf("present: %sabled\n", (rdmsr(IA32_PKG_HCD_CTL) & 1) ? "en" : "dis");
	}
	else {
		printf("NOT present\n");
	}

	if (cpu_high >= 2) {
		printf("\nCache and TLB info:");
		rounds = 0;
		do {
			getcpuid(0x2, regs);
			if (rounds == 0 && (rounds = (regs[0] & 0xff)) == 0)
				break;	/* we have a buggy CPU */

			for (regnum = 0; regnum <= 3; ++regnum) {
				if (regs[regnum] & (1 << 31))
					continue;
				if (regnum != 0)
					print_INTEL_TLB(regs[regnum] & 0xff);
				print_INTEL_TLB((regs[regnum] >> 8) & 0xff);
				print_INTEL_TLB((regs[regnum] >> 16) & 0xff);
				print_INTEL_TLB((regs[regnum] >> 24) & 0xff);
			}
		} while (--rounds > 0);

		printf("\n\nCache info\n");
		for (set = 0; ; set++) {
			getcpuidx(4, set, regs);
			if (regs[0] == 0)
				break;
			printf("Type %u: lvl %u, EAX=%08x, L%u, P%u W%u S%u (size=%u) flags=%x\n",
				regs[0] & 0x1f, (regs[0] >> 5) & 7, regs[0],
				1+(regs[1] & 0xfff), 1+((regs[1] >> 12) & 0x3ff), 1+(regs[1] >> 22),
				1+regs[2], 
				(1 + regs[2]) * (1 + (regs[1] & 0xfff)),
				regs[3]);
		}
	}

	if (cpu_exthigh >= 0x80000006) {
		getcpuid(0x80000006, regs);
		nwaycode = (regs[2] >> 12) & 0x0f;
		if (nwaycode >= 0x02 && nwaycode <= 0x08)
			nway = 1 << (nwaycode / 2);
		else
			nway = 0;
		printf("\nL2 cache: %u kbytes, %u-way associative, %u bytes/line\n",
			(regs[2] >> 16) & 0xffff, nway, regs[2] & 0xff);
	}

	if (cpu_high >= 7) {
		getcpuidx(7, 0, regs);
		if (regs[1] & (1 << 12)) {
			printf("\nSupports Resource Director Technology - Monitor\n");
		}
		else {
			printf("\n NO Resource Director Technology - Monitor\n");
		}
		if (regs[1] & (1 << 15)) {
			printf("\nSupports Resource Director Technology - Allocation\n");
			if (cpu_high >= 0x10) {
				getcpuidx(0x10, 0, regs);
				if ((regs[1] & 0x04) != 0) {
					printf("  Supports L2 CAT\n");
					getcpuidx(0x10, 2, regs2);
					printf("  L2CAT: Length of capability bitmask = %u\n", regs2[0] & 0x1f);
					printf("  L2CAT: Highest COS number = %u\n", regs2[3] & 0xffff);
					printf("  L2CAT: Allocation unit map: %08x\n", regs2[1]);
				}
				if ((regs[1] & 0x02) != 0) {
					printf("  Supports L3 CAT\n");
					getcpuidx(0x10, 1, regs2);
					printf("  L3CAT: Length of capability bitmask = %u\n", regs2[0] & 0x1f);
					printf("  L3CAT: Highest COS number = %u\n", regs2[3] & 0xffff);
					printf("  L3CAT: Allocation unit map: %08x\n", regs2[1]);
					printf("  L3CAT: ECX=%#x\n", regs2[2]);
				}

			}
		}
		else {
			printf("\n NO Resource Director Technology - Allocation\n");
		}
	}

	printf("\n");
}

static void
init_exthigh(void)
{
	static int done = 0;
	u_int regs[4];

	if (done == 0) {
		if (cpu_high > 0 &&
			(cpu_vendor_id == CPU_VENDOR_INTEL ||
			cpu_vendor_id == CPU_VENDOR_AMD ||
			cpu_vendor_id == CPU_VENDOR_TRANSMETA ||
			cpu_vendor_id == CPU_VENDOR_CENTAUR ||
			cpu_vendor_id == CPU_VENDOR_NSC)) {
			getcpuid(0x80000000, regs);
			if (regs[0] >= 0x80000000)
				cpu_exthigh = regs[0];
		}

		done = 1;
	}
}

static u_int
find_cpu_vendor_id(void)
{
	int	i;

	for (i = 0; i < sizeof(cpu_vendors) / sizeof(cpu_vendors[0]); i++)
		if (strcmp(cpu_vendor, cpu_vendors[i].vendor) == 0)
			return (cpu_vendors[i].vendor_id);
	return (0);
}

/**
 * Get the Max CPU frequency from the brand string
 */

QWORD GetCpuMaxFrequency(void)
{
	char *qq = "Intel(R) Core(TM) i5-6500T CPU @ 2.50GHz";
	char *p;
	double multiplier;
	QWORD maxFreq = 0;
	const char *subStr;

	p = strdup(brand);
	if (!p) {
		printf("strdup failed\n");
		return 0;  // Arbitrary
	}

	strrev(p);

	if ((subStr = strstr(p, "zHM")) != NULL)
		multiplier = 1e6;
	else if ((subStr = strstr(p, "zHG")) != NULL)
		multiplier = 1e9;
	else if ((subStr = strstr(p, "zHT")) != NULL)
		multiplier = 1e12;
	else {
		printf("Could not determine maximum qualified frequency!\n");
		multiplier = 0.0;
	}

#ifdef COMMENT1
	DPRINTF("brand string - %s", brandString);
#endif

	if (multiplier > 1.0) {
		char digits[16], *digit = digits;

		subStr = p + (subStr - p + 3);

		while (*subStr != ' ')
			*(digit++) = *(subStr++);

		*digit = '\0';
		strrev(digits);
		maxFreq = (QWORD)(strtod(digits, NULL) * multiplier);
	}

	free(p);

	return maxFreq;
}

double HwGetCPUFrequency(void)
{
	QWORD aperf, mperf;
	static QWORD HwCPUMaxFrequency = 0;
	double ret;

	if (HwCPUMaxFrequency == 0) {
		HwCPUMaxFrequency = GetCpuMaxFrequency();
	}

	disable();
	mperf = rdmsr(IA32_MPERF);
	aperf = rdmsr(IA32_APERF);
	enable();

	printf("aperf=%llu, mperf=%llu\n", aperf, mperf);
	ret = ((double)aperf / (double)mperf) * ((double)HwCPUMaxFrequency / 1e9);

	disable();
	wrmsr(IA32_MPERF, 0);
	wrmsr(IA32_APERF, 0);
	enable();

	return ret;
}

void main(int argc, char **argv)
{
	unsigned ver = -1, regs[4];
	int header_only = 0;

	if (argc > 1 && strcmp(argv[1], "-h") == 0)
		header_only = 1;

	if (!HT_detect(&ver))
		printf("Not a HT CPU: version = %08x\n", ver);
	else
		printf("HT CPU detected: version = %#x\n", ver);

	getcpuid(0, regs);
	cpu_high = regs[0];	// EAX
	_cpunamebuf[0] = regs[1];
	_cpunamebuf[1] = regs[3];
	_cpunamebuf[2] = regs[2];
	_cpunamebuf[3] = 0;
	cpu_vendor = (char *)_cpunamebuf;
	cpu_vendor_id = find_cpu_vendor_id();

	getcpuid(1, regs);
	cpu_id = regs[0];
	cpu_feature = regs[3];	// EDX
	cpu_feature2 = regs[2];	// ECX

#if _DO_MSR_
	/*
	* Clear "Limit CPUID Maxval" bit and get the largest standard CPUID
	* function number again if it is set from BIOS.  It is necessary
	* for probing correct CPU topology later.
	* XXX This is only done on the BSP package.
	*/
	if (cpu_vendor_id == CPU_VENDOR_INTEL && cpu_high > 0 && cpu_high < 4 &&
		((CPUID_TO_FAMILY(cpu_id) == 0xf && CPUID_TO_MODEL(cpu_id) >= 0x3) ||
		(CPUID_TO_FAMILY(cpu_id) == 0x6 && CPUID_TO_MODEL(cpu_id) >= 0xe))) {
		uint64_t msr;
		msr = rdmsr(IA32_MISC_ENABLE);
		if ((msr & 0x400000ULL) != 0) {
			wrmsr(IA32_MISC_ENABLE, msr & ~0x400000ULL);
			getcpuid(0, regs);
			cpu_high = regs[0];
		}
	}
#endif

	/* Detect AMD features (PTE no-execute bit, 3dnow, 64 bit mode etc) */
	if (cpu_vendor_id == CPU_VENDOR_INTEL ||
		cpu_vendor_id == CPU_VENDOR_AMD) {
		init_exthigh();
		if (cpu_exthigh >= 0x80000001) {
			getcpuid(0x80000001, regs);
			amd_feature = regs[3] & ~(cpu_feature & 0x0183f3ff);
			amd_feature2 = regs[2];
		}
		if (cpu_exthigh >= 0x80000007) {
			getcpuid(0x80000007, regs);
			amd_pminfo = regs[3];
		}
		if (cpu_exthigh >= 0x80000008) {
			getcpuid(0x80000008, regs);
			cpu_procinfo2 = regs[2];
		}
	}
	else if (cpu_vendor_id == CPU_VENDOR_CENTAUR) {
		init_exthigh();
		if (cpu_exthigh >= 0x80000001) {
			getcpuid(0x80000001, regs);
			amd_feature = regs[3] & ~(cpu_feature & 0x0183f3ff);
		}
	}
#if 0
	else if (cpu_vendor_id == CPU_VENDOR_CYRIX) {
		if (cpu == CPU_486) {
			/*
			* These conditions are equivalent to:
			*     - CPU does not support cpuid instruction.
			*     - Cyrix/IBM CPU is detected.
			*/
			isblue = identblue();
			if (isblue == IDENTBLUE_IBMCPU) {
				strcpy(cpu_vendor, "IBM");
				cpu_vendor_id = CPU_VENDOR_IBM;
				cpu = CPU_BLUE;
				return;
			}
		}
		switch (cpu_id & 0xf00) {
		case 0x600:
			/*
			* Cyrix's datasheet does not describe DIRs.
			* Therefor, I assume it does not have them
			* and use the result of the cpuid instruction.
			* XXX they seem to have it for now at least. -Peter
			*/
			identifycyrix();
			cpu = CPU_M2;
			break;
		default:
			identifycyrix();
			/*
			* This routine contains a trick.
			* Don't check (cpu_id & 0x00f0) == 0x50 to detect M2, now.
			*/
			switch (cyrix_did & 0x00f0) {
			case 0x00:
			case 0xf0:
				cpu = CPU_486DLC;
				break;
			case 0x10:
				cpu = CPU_CY486DX;
				break;
			case 0x20:
				if ((cyrix_did & 0x000f) < 8)
					cpu = CPU_M1;
				else
					cpu = CPU_M1SC;
				break;
			case 0x30:
				cpu = CPU_M1;
				break;
			case 0x40:
				/* MediaGX CPU */
				cpu = CPU_M1SC;
				break;
			default:
				/* M2 and later CPUs are treated as M2. */
				cpu = CPU_M2;

				/*
				* enable cpuid instruction.
				*/
				ccr3 = read_cyrix_reg(CCR3);
				write_cyrix_reg(CCR3, CCR3_MAPEN0);
				write_cyrix_reg(CCR4, read_cyrix_reg(CCR4) | CCR4_CPUID);
				write_cyrix_reg(CCR3, ccr3);

				getcpuid(0, regs);
				cpu_high = regs[0];	/* eax */
				getcpuid(1, regs);
				cpu_id = regs[0];	/* eax */
				cpu_feature = regs[3];	/* edx */
				break;
			}
		}
	}
	else if (cpu == CPU_486 && *cpu_vendor == '\0') {
		/*
		* There are BlueLightning CPUs that do not change
		* undefined flags by dividing 5 by 2.  In this case,
		* the CPU identification routine in locore.s leaves
		* cpu_vendor null string and puts CPU_486 into the
		* cpu.
		*/
		isblue = identblue();
		if (isblue == IDENTBLUE_IBMCPU) {
			strcpy(cpu_vendor, "IBM");
			cpu_vendor_id = CPU_VENDOR_IBM;
			cpu = CPU_BLUE;
			return;
		}
	}
#endif

	getcpuid(1, regs);

	printf("Max logical processors = %u, local APIC ID = %u, cache line size = %u\n",
		(regs[1] >> 16) & 255, (regs[1] >> 24),
		((regs[1] >> 8) & 255) * 8);
	if (getcpuid(0x80000000, regs)) {
		unsigned max = regs[0], n;
		char *p;
		if (max & 0x80000000) {
			if (max > 0x80000004)
				max = 0x80000004;
			printf("Brand string: ");
			p = brand;
			for (n = 0x80000002; n <= max; n++) {
				getcpuid(n, regs);
				memcpy(p, regs, 16);
				p += 16;
			}
			*p = '\0';
			printf("%s\n", brand);
		}
	}

	printf("Max clock frequency: %lluHz\n", GetCpuMaxFrequency());
	printf("Current frequency: %gGHz\n", HwGetCPUFrequency());

	features();

	if (cpu_vendor_id == CPU_VENDOR_INTEL) {
		print_INTEL_info();
	}

	if (header_only)
		return;

	printf("\nDump of CPUID registers:\n"
		"    code  idx   EAX       EBX       ECX       EDX  \n");
	if (getcpuid(0, regs)) {
		unsigned max = regs[0], n, i, max7;
		char *p;

		for (n = 0; n <= max; n++) {
			switch (n) {
			case 4:
				for (i = 0; ; i++) {
					getcpuidx(n, i, regs);
					if (regs[0] == 0)
						break;
					printf("%08x: %2u  %08x  %08x  %08x  %08x\n",
						n, i, regs[0], regs[1], regs[2], regs[3]);
				}
				break;
			case 7:
				getcpuidx(n, 0, regs);
				max7 = regs[0];
				for (i = 0; i <= max7; i++) {
					getcpuidx(n, i, regs);
					printf("%08x: %2u  %08x  %08x  %08x  %08x\n",
						n, i, regs[0], regs[1], regs[2], regs[3]);
				}
				break;
			case 11:	// extended topology
				getcpuidx(n, 0, regs);
				printf("%08x:     %08x  %08x  %08x  %08x\n",
					n, regs[0], regs[1], regs[2], regs[3]);
				for (i = 1; (regs[2] & 0x0000ff00) != 0; i++) {
					getcpuidx(n, i, regs);
					if ((regs[2] & 0x0000ff00) != 0)
						printf("%08x: %2u  %08x  %08x  %08x  %08x\n",
						n, i, regs[0], regs[1], regs[2], regs[3]);
				}
				break;
			case 15:	// Platform QoS/L3 Cache QoS
				getcpuidx(n, 0, regs);
				printf("%08x: %08x  %08x  %08x  %08x\n",
					n, regs[0], regs[1], regs[2], regs[3]);
				getcpuidx(n, 0, regs);
				printf("%08x: %08x  %08x  %08x  %08x\n",
					n, regs[0], regs[1], regs[2], regs[3]);
				break;
			case 16:
				getcpuidx(n, 0, regs);
				printf("%08x:     %08x  %08x  %08x  %08x\n",
					n, regs[0], regs[1], regs[2], regs[3]);
				if ((regs[1] & 2) != 0) {
					// cache QOS detected
					getcpuidx(n, 1, regs);
					printf("%08x: %2u  %08x  %08x  %08x  %08x\n",
						n, i, regs[0], regs[1], regs[2], regs[3]);
				}
				break;
			default:
				getcpuid(n, regs);
				printf("%08x:     %08x  %08x  %08x  %08x  ",
					n, regs[0], regs[1], regs[2], regs[3]);
				if (n == 0) {
					for (p = (char *)regs, i = 0; i < 16; i++)
						printf("%c", isprint(p[i]) ? p[i] : '.');
				}
				printf("\n");
				break;
			}
		}
	}
	if (getcpuid(0x80000000, regs)) {
		unsigned max = regs[0], n, i;
		char *p;

		for (n = 0x80000000; n <= max; n++) {
			getcpuid(n, regs);
			printf("%08x:     %08x  %08x  %08x  %08x  ",
				n, regs[0], regs[1], regs[2], regs[3]);
			for (p = (char *)regs, i = 0; i < 16; i++)
				printf("%c", isprint(p[i]) ? p[i] : '.');
			printf("\n");
		}
	}

	getcpuid(1, regs);
	if (regs[3] & (1 << 18)) {
		printf("Processor serial number: \n");
		printf("%08x", regs[0]);
		getcpuid(3, regs);
		printf("%08x%08x\n", regs[3], regs[2]);
	}

	return;
}
