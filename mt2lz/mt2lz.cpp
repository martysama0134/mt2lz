////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// from https://www.elitepvpers.com/forum/metin2-pserver-guides-strategies/1965355-release-metin2-original-patcher-file-archiver.html (by balika) //
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <conio.h>
#include <cstdio>
#include <sstream>

#define LZO_E_OK 1
#define LZO_E_INPUT_OVERRUN (-1)
#define LZO_E_EOF_NOT_FOUND (-2)
#define LZO_E_INPUT_NOT_CONSUMED (-3)

static unsigned long lzo_docompress(const unsigned char * in, unsigned long in_len, unsigned char * out, unsigned long * out_len,
									unsigned long ti, void * wrkmem)
{
	register const unsigned char * ip;
	unsigned char * op;
	const unsigned char * const in_end = in + in_len;
	const unsigned char * const ip_end = in + in_len - 20;
	const unsigned char * ii;
	unsigned short * const dict = (unsigned short *) wrkmem;

	op = out;
	ip = in;
	ii = ip;

	ip += ti < 4 ? 4 - ti : 0;
	for (;;)
	{
		const unsigned char * m_pos;
		unsigned long m_off;
		unsigned long m_len;
		{
			int dv;
			unsigned long dindex;
		literal:
			ip += 1 + ((ip - ii) >> 5);
		next:
			if (ip >= ip_end)
				break;
			dv = (*(volatile const int *) (volatile const void *) (ip));
			dindex = ((unsigned long) (((((((unsigned long) ((0x1824429d) * (dv)))) >> (32 - 14))) & (((1u << 14) - 1) >> (0))) << (0)));
			m_pos = in + dict[dindex];
			dict[dindex] = ((unsigned short) ((unsigned long) ((ip) - (in))));
			if (dv != (*(volatile const int *) (volatile const void *) (m_pos)))
				goto literal;
		}

		ii -= ti;
		ti = 0;
		{
			register unsigned long t = (unsigned long) ((ip) - (ii));
			if (t != 0)
			{
				if (t <= 3)
				{
					op[-2] |= (unsigned char) (t);
					((*(volatile int *) (volatile void *) (op)) = (int) ((*(volatile const int *) (volatile const void *) (ii))));
					op += t;
				}
				else if (t <= 16)
				{
					*op++ = (unsigned char) (t - 3);
					((*(volatile int *) (volatile void *) (op)) = (int) ((*(volatile const int *) (volatile const void *) (ii))));
					((*(volatile int *) (volatile void *) (op + 4)) = (int) ((*(volatile const int *) (volatile const void *) (ii + 4))));
					((*(volatile int *) (volatile void *) (op + 8)) = (int) ((*(volatile const int *) (volatile const void *) (ii + 8))));
					((*(volatile int *) (volatile void *) (op + 12)) = (int) ((*(volatile const int *) (volatile const void *) (ii + 12))));
					op += t;
				}
				else
				{
					if (t <= 18)
						*op++ = (unsigned char) (t - 3);
					else
					{
						register unsigned long tt = t - 18;
						*op++ = 0;
						while (tt > 255)
						{
							tt -= 255;
							*(volatile unsigned char *) op++ = 0;
						}
						*op++ = (unsigned char) (tt);
					}
					do
					{
						((*(volatile int *) (volatile void *) (op)) = (int) ((*(volatile const int *) (volatile const void *) (ii))));
						((*(volatile int *) (volatile void *) (op + 4)) =
							 (int) ((*(volatile const int *) (volatile const void *) (ii + 4))));
						((*(volatile int *) (volatile void *) (op + 8)) =
							 (int) ((*(volatile const int *) (volatile const void *) (ii + 8))));
						((*(volatile int *) (volatile void *) (op + 12)) =
							 (int) ((*(volatile const int *) (volatile const void *) (ii + 12))));
						op += 16;
						ii += 16;
						t -= 16;
					} while (t >= 16);
					if (t > 0)
					{
						do
							*op++ = *ii++;
						while (--t > 0);
					}
				}
			}
		}
		m_len = 4;
		{
			int v;
			v = (*(volatile const int *) (volatile const void *) (ip + m_len)) ^
				(*(volatile const int *) (volatile const void *) (m_pos + m_len));
			if (v == 0)
			{
				do
				{
					m_len += 4;
					v = (*(volatile const int *) (volatile const void *) (ip + m_len)) ^
						(*(volatile const int *) (volatile const void *) (m_pos + m_len));
					if (ip + m_len >= ip_end)
						goto m_len_done;
				} while (v == 0);
			}
		}
	m_len_done:
		m_off = (unsigned long) ((ip) - (m_pos));
		ip += m_len;
		ii = ip;
		if (m_len <= 8 && m_off <= 0x0800)
		{
			m_off -= 1;
			*op++ = (unsigned char) (((m_len - 1) << 5) | ((m_off & 7) << 2));
			*op++ = (unsigned char) (m_off >> 3);
		}
		else if (m_off <= 0x4000)
		{
			m_off -= 1;
			if (m_len <= 33)
				*op++ = (unsigned char) (32 | (m_len - 2));
			else
			{
				m_len -= 33;
				*op++ = 32 | 0;
				while (m_len > 255)
				{
					m_len -= 255;
					*(volatile unsigned char *) op++ = 0;
				}
				*op++ = (unsigned char) (m_len);
			}
			*op++ = (unsigned char) (m_off << 2);
			*op++ = (unsigned char) (m_off >> 6);
		}
		else
		{
			m_off -= 0x4000;
			if (m_len <= 9)
				*op++ = (unsigned char) (16 | ((m_off >> 11) & 8) | (m_len - 2));
			else
			{
				m_len -= 9;
				*op++ = (unsigned char) (16 | ((m_off >> 11) & 8));
				while (m_len > 255)
				{
					m_len -= 255;
					*(volatile unsigned char *) op++ = 0;
				}
				*op++ = (unsigned char) (m_len);
			}
			*op++ = (unsigned char) (m_off << 2);
			*op++ = (unsigned char) (m_off >> 6);
		}
		goto next;
	}

	*out_len = (unsigned long) ((op) - (out));
	return (unsigned long) ((in_end) - (ii - ti));
}

int lzo_compress(const unsigned char * in, unsigned long in_len, unsigned char * out, unsigned long * out_len)
{
	const unsigned char * ip = in;
	unsigned char * op = out;
	unsigned long l = in_len;
	unsigned long t = 0;
	void * wrkmem = new void *[16384];

	while (l > 20)
	{
		unsigned long ll = l;
		size_t ll_end;
		ll = ((ll) <= (49152) ? (ll) : (49152));
		ll_end = (size_t) ip + ll;
		if ((ll_end + ((t + ll) >> 5)) <= ll_end || (const unsigned char *) (ll_end + ((t + ll) >> 5)) <= ip + ll)
			break;
		memset(wrkmem, 0, ((unsigned long) 1 << 14) * sizeof(unsigned short));
		t = lzo_docompress(ip, ll, op, out_len, t, wrkmem);
		ip += ll;
		op += *out_len;
		l -= ll;
	}
	t += l;

	if (t > 0)
	{
		const unsigned char * ii = in + in_len - t;

		if (op == out && t <= 238)
			*op++ = (unsigned char) (17 + t);
		else if (t <= 3)
			op[-2] |= (unsigned char) (t);
		else if (t <= 18)
			*op++ = (unsigned char) (t - 3);
		else
		{
			unsigned long tt = t - 18;

			*op++ = 0;
			while (tt > 255)
			{
				tt -= 255;

				*(volatile unsigned char *) op++ = 0;
			}
			*op++ = (unsigned char) (tt);
		}
		do
			*op++ = *ii++;
		while (--t > 0);
	}

	*op++ = 16 | 1;
	*op++ = 0;
	*op++ = 0;

	*out_len = (unsigned long) ((op) - (out));
	return LZO_E_OK;
}

int lzo_decompress(const unsigned char * in, unsigned long in_len, unsigned char * out, unsigned long * out_len)
{
	register unsigned char * op;
	register const unsigned char * ip;
	register unsigned long t;
	register const unsigned char * m_pos;

	const unsigned char * const ip_end = in + in_len;
	unsigned char * const op_end = out + *out_len;

	*out_len = 0;

	op = out;
	ip = in;

	if (*ip > 17)
	{
		t = *ip++ - 17;
		if (t < 4)
			goto match_next;
		do
			*op++ = *ip++;
		while (--t > 0);
		goto first_literal_run;
	}

	while (ip < ip_end)
	{
		t = *ip++;
		if (t >= 16)
			goto match;
		if (t == 0)
		{
			while (*ip == 0)
			{
				t += 255;
				ip++;
			}
			t += 15 + *ip++;
		}
		((*(volatile unsigned int *) (volatile void *) (op)) =
			 (unsigned long) (*(volatile const unsigned int *) (volatile const void *) (ip)));
		op += 4;
		ip += 4;
		if (--t > 0)
		{
			if (t >= 4)
			{
				do
				{
					((*(volatile unsigned int *) (volatile void *) (op)) =
						 (unsigned long) (*(volatile const unsigned int *) (volatile const void *) (ip)));
					op += 4;
					ip += 4;
					t -= 4;
				} while (t >= 4);
				if (t > 0)
					do
						*op++ = *ip++;
					while (--t > 0);
			}
			else
				do
					*op++ = *ip++;
				while (--t > 0);
		}

	first_literal_run:

		t = *ip++;
		if (t >= 16)
			goto match;
		m_pos = op - (1 + 0x0800);
		m_pos -= t >> 2;
		m_pos -= *ip++ << 2;
		*op++ = *m_pos++;
		*op++ = *m_pos++;
		*op++ = *m_pos;
		goto match_done;

		do
		{
		match:
			if (t >= 64)
			{
				m_pos = op - 1;
				m_pos -= (t >> 2) & 7;
				m_pos -= *ip++ << 3;
				t = (t >> 5) - 1;
				goto copy_match;
			}
			else if (t >= 32)
			{
				t &= 31;
				if (t == 0)
				{
					while (*ip == 0)
					{
						t += 255;
						ip++;
					}
					t += 31 + *ip++;
				}
				m_pos = op - 1;
				m_pos -= (*(volatile const unsigned short *) (volatile const void *) (ip)) >> 2;
				ip += 2;
			}
			else if (t >= 16)
			{
				m_pos = op;
				m_pos -= (t & 8) << 11;
				t &= 7;
				if (t == 0)
				{
					while (*ip == 0)
					{
						t += 255;
						ip++;
					}
					t += 7 + *ip++;
				}
				m_pos -= (*(volatile const unsigned short *) (volatile const void *) (ip)) >> 2;
				ip += 2;
				if (m_pos == op)
					goto eof_found;
				m_pos -= 0x4000;
			}
			else
			{
				m_pos = op - 1;
				m_pos -= t >> 2;
				m_pos -= *ip++ << 2;
				*op++ = *m_pos++;
				*op++ = *m_pos;
				goto match_done;
			}

			if (t >= 2 * 4 - (3 - 1) && (op - m_pos) >= 4)
			{
				((*(volatile unsigned int *) (volatile void *) (op)) =
					 (unsigned long) (*(volatile const unsigned int *) (volatile const void *) (m_pos)));
				op += 4;
				m_pos += 4;
				t -= 4 - (3 - 1);
				do
				{
					((*(volatile unsigned int *) (volatile void *) (op)) =
						 (unsigned long) (*(volatile const unsigned int *) (volatile const void *) (m_pos)));
					op += 4;
					m_pos += 4;
					t -= 4;
				} while (t >= 4);
				if (t > 0)
					do
						*op++ = *m_pos++;
					while (--t > 0);
			}
			else
			{
			copy_match:
				*op++ = *m_pos++;
				*op++ = *m_pos++;
				do
					*op++ = *m_pos++;
				while (--t > 0);
			}


		match_done:
			t = ip[-2] & 3;
			if (t == 0)
				break;

		match_next:
			*op++ = *ip++;
			if (t > 1)
			{
				*op++ = *ip++;
				if (t > 2)
				{
					*op++ = *ip++;
				}
			}
			t = *ip++;
		} while (ip < ip_end);
	}
	*out_len = ((unsigned long) ((op) - (out)));
	return LZO_E_EOF_NOT_FOUND;

eof_found:
	*out_len = ((unsigned long) ((op) - (out)));
	return (ip == ip_end ? LZO_E_OK : (ip < ip_end ? LZO_E_INPUT_NOT_CONSUMED : LZO_E_INPUT_OVERRUN));
}

void pause()
{
	printf("Press a key to continue...\n");
	getchar();
}

int main(int argc, char * argv[])
{
	if (argc != 4)
	{
	USAGE:
		std::string exename = argv[0];
		exename = exename.substr(exename.find_last_of("\\") + 1, exename.find_last_of(".") - exename.find_last_of("\\") - 1);
		printf("Usage: %s <pack / unpack> <path to in file> <path to out file>\n", exename.c_str());
		pause();
		return -1;
	}
	std::string todo = argv[1];
	if (todo == "unpack")
	{
		FILE * InFile = fopen(argv[2], "rb");
		if (InFile == NULL)
		{
			printf("Could not open %s file.\n", argv[2]);
			pause();
			return 0;
		}
		fseek(InFile, 0, SEEK_END);
		unsigned long Size = ftell(InFile);
		rewind(InFile);
		unsigned char * Buffer = new unsigned char[Size];
		if (Buffer == NULL)
		{
			printf("Memory error when make buffer for %s file.\n", argv[2]);
			pause();
			return 0;
		}
		if (fread(Buffer, 1, Size, InFile) != Size)
		{
			printf("Could not read %s file.\n", argv[2]);
			pause();
			return 0;
		}
		fclose(InFile);

		unsigned long uncompressedSize = *(unsigned long *) Buffer;

		unsigned char * uncompressedBuffer = new unsigned char[uncompressedSize];
		memset(uncompressedBuffer, 0, uncompressedSize);

		unsigned long finalSize = 0;
		lzo_decompress(Buffer + 4, uncompressedSize, uncompressedBuffer, &finalSize);
		if (finalSize == uncompressedSize)
		{
			FILE * OutFile = fopen(argv[3], "wb");
			if (OutFile)
			{
				fwrite(uncompressedBuffer, 1, uncompressedSize, OutFile);
				fclose(OutFile);
			}
		}
		else
		{
			printf("There was an error when decompressing %s file.\n", argv[2]);
			printf("%i %i\n", finalSize, uncompressedSize);
		}
		delete[] uncompressedBuffer;
		delete[] Buffer;
	}
	else if (todo == "pack")
	{
		FILE * InFile = fopen(argv[2], "rb");
		if (InFile == NULL)
		{
			printf("Could not open %s file.\n", argv[2]);
			pause();
			return 0;
		}
		fseek(InFile, 0, SEEK_END);
		unsigned long Size = ftell(InFile);
		rewind(InFile);
		unsigned char * Buffer = new unsigned char[Size];
		if (Buffer == NULL)
		{
			printf("Memory error when make buffer for %s file.\n", argv[2]);
			pause();
			return 0;
		}
		if (fread(Buffer, 1, Size, InFile) != Size)
		{
			printf("Could not read %s file.\n", argv[2]);
			pause();
			return 0;
		}
		fclose(InFile);

		unsigned char * compressedBuffer = new unsigned char[Size + (Size / 16) + 64 + 3 + 4];
		memset(compressedBuffer, 0, Size + (Size / 16) + 64 + 3 + 4);

		unsigned long compressedSize = 0;
		if (lzo_compress(Buffer, Size, compressedBuffer + 4, &compressedSize) != LZO_E_OK)
		{
			printf("Error when compressing %s.\n", argv[2]);
			delete[] compressedBuffer;
			pause();
			return 0;
		}
		*(unsigned long *) compressedBuffer = Size;

		unsigned char * uncompressedBuffer = new unsigned char[compressedSize];
		memset(uncompressedBuffer, 0, compressedSize);

		FILE * OutFile = fopen(argv[3], "wb");
		if (OutFile)
		{
			fwrite(compressedBuffer, 1, compressedSize + 4, OutFile);
			fclose(OutFile);
		}
	}
	else
		goto USAGE;

	//pause();
	return 0;
}
