#ifndef __PATCHES_H__
#define __PATCHES_H__
#ifdef __cplusplus
extern "C" {
#endif

inline uint64_t peekq(uint64_t addr);
inline void pokeq( uint64_t addr, uint64_t val);
inline void poke_lv1( uint64_t addr, uint64_t val);
inline void lv2poke32(u64 addr, u32 value);
void patches(const float);

inline void pokeq( uint64_t addr, uint64_t val)
{
	system_call_2(7, addr, val);
}

inline void poke_lv1( uint64_t addr, uint64_t val)
{
	system_call_2(9, addr, val);
}

inline uint64_t peekq(uint64_t addr)
{
	system_call_1(6, addr);
	return_to_user_prog(uint64_t);
}

inline void lv2poke32(u64 addr, u32 value)
{
    pokeq(addr, (((u64) value) <<32) | (peekq(addr) & 0xffffffffULL));
}

void patches(const float c_firmware)
{
	if(c_firmware==4.75f)
	{
		pokeq(0x800000000026714CULL, 0x4E80002038600000ULL ); // fix 8001003C error  Original: 0x4E8000208003026CULL
		pokeq(0x8000000000267154ULL, 0x7C6307B44E800020ULL ); // fix 8001003C error  Original: 0x3D6000473D201B43ULL
		pokeq(0x800000000005658CULL, 0x63FF003D60000000ULL ); // fix 8001003D error  Original: 0x63FF003D419EFFD4ULL
		pokeq(0x8000000000056650ULL, 0x3FE080013BE00000ULL ); // fix 8001003E error  Original: 0x3FE0800163FF003EULL

		pokeq(0x80000000000565FCULL, 0x419E00D860000000ULL ); // Original: 0x419E00D8419D00C0ULL
		pokeq(0x8000000000056604ULL, 0x2F84000448000098ULL ); // Original: 0x2F840004409C0048ULL //PATCH_JUMP
		pokeq(0x800000000005A6E0ULL, 0x2F83000060000000ULL ); // fix 80010009 error  Original: 0x2F830000419E00ACULL
		pokeq(0x800000000005A6F4ULL, 0x2F83000060000000ULL ); // fix 80010009 error  Original: 0x2F830000419E00ACULL

		pokeq(0x8000000000056230ULL, 0x386000012F830000ULL ); // ignore LIC.DAT check
		pokeq(0x80000000002275F4ULL, 0x38600000F8690000ULL ); // fix 0x8001002B / 80010017 errors (2015-01-03)

		pokeq(0x8000000000055C5CULL, 0xF821FE917C0802A6ULL ); // just restore the original
		pokeq(0x8000000000058E1CULL, 0x419E0038E8610098ULL ); // just restore the original
	}

	if(c_firmware==4.80f)
	{

		pokeq(0x8000000000267144ULL, 0x4E80002038600000ULL ); // fix 8001003C error  Original: 0x4E8000208003026CULL
		pokeq(0x800000000026714CULL, 0x7C6307B44E800020ULL ); // fix 8001003C error  Original: 0x3D201B433C608001
		pokeq(0x8000000000056588ULL, 0x63FF003D60000000ULL ); // fix 8001003D error  Original: 0x63FF003D419EFFD4ULL
		pokeq(0x800000000005664CULL, 0x3FE080013BE00000ULL ); // fix 8001003E error  Original: 0x3FE0800163FF003EULL

		pokeq(0x80000000000565F8ULL, 0x419E00D860000000ULL ); // Original: 0x419E00D8419D00C0ULL
		pokeq(0x8000000000056600ULL, 0x2F84000448000098ULL ); // Original: 0x2F840004409C0048ULL //PATCH_JUMP
		pokeq(0x800000000005A6DCULL, 0x2F83000060000000ULL ); // fix 80010009 error  Original: 0x2F830000419E00ACULL
		pokeq(0x800000000005A6F0ULL, 0x2F83000060000000ULL ); // fix 80010009 error  Original: 0x2F830000419E00ACULL

		pokeq(0x800000000005622CULL, 0x386000012F830000ULL ); // ignore LIC.DAT check
		pokeq(0x80000000002275ECULL, 0x38600000F8690000ULL ); // fix 0x8001002B / 80010017 errors (2015-01-03)

		pokeq(0x8000000000055C58ULL, 0xF821FE917C0802A6ULL ); // just restore the original
		pokeq(0x8000000000058E18ULL, 0x419E0038E8610098ULL ); // just restore the original
	}
}

#ifdef __cplusplus
}
#endif

#endif /* __PATCHES_H__ */
