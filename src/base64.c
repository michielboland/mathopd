#include "mathopd.h"

static unsigned char b64[256];

void base64initialize(void)
{
	int i;

	memset(b64, 64, sizeof b64);
	for (i = 'A'; i <= 'Z'; i++)
		b64[i] = i - 'A';
	for (i = 'a'; i <= 'z'; i++)
		b64[i] = i - 'a' + 26;
	for (i = '0'; i <= '9'; i++)
		b64[i] = i - '0' + 52;
	b64['+'] = 62;
	b64['/'] = 63;
	b64['='] = 0;
}

int base64decode(const unsigned char *encoded, unsigned char *decoded)
{
	register int c;
	register unsigned char t1, t2, u1, u2, u3;

	while (1) {

		c = *encoded++;
		if (c == 0) {
			*decoded = 0;
			return 0;
		}
		t1 = b64[c];
		if (t1 == 64)
			return -1;
	
		c = *encoded++;
		if (c == 0)
			return -1;
		t2 = b64[c];
		if (t2 == 64)
			return -1;

		u1 = t1 << 2 | t2 >> 4;

		c = *encoded++;
		if (c == 0)
			return -1;
		t1 = b64[c];
		if (t1 == 64)
			return -1;

		u2 = (t2 & 0xf) << 4 | t1 >> 2;

		c = *encoded++;
		if (c == 0)
			return -1;
		t2 = b64[c];
		if (t2 == 64)
			return -1;

		u3 = (t1 & 0x3) << 6 | t2;

		*decoded++ = u1;
		if (u1 == 0)
			return 0;

		*decoded++ = u2;
		if (u2 == 0)
			return 0;

		*decoded++ = u3;
		if (u3 == 0)
			return 0;
	}

}

int base64compare(const unsigned char *encoded, const unsigned char *decoded)
{
	char tmp[256];

	if (strlen(encoded) >= sizeof tmp)
		return -1;
	if (base64decode(encoded, tmp) == -1)
		return -1;
	return strcmp(tmp, decoded);
}
