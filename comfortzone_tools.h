#ifndef _COMFORTZONE_TOOLS_H
#define _COMFORTZONE_TOOLS_H

static inline uint16_t get_uint16(const byte *ptr)
{
	uint16_t v;

	v = (ptr[1] << 8) | ptr[0];

	return v;
}

static inline int16_t get_int16(const byte *ptr)
{
	int16_t v;

	v = (ptr[1] << 8) | ptr[0];

	return v;
}

static inline uint32_t get_uint32(const byte *ptr)
{
	uint32_t v;

	v = (uint32_t(ptr[3]) << 24) | (uint32_t(ptr[2]) << 16) | (uint32_t(ptr[1]) << 8) | ptr[0];

	return v;
}

#endif
