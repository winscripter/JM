#include "global.h"
#include "annexb.h"
#include "memalloc.h" 
#include "fast_memory.h"

static const int IOBUFFERSIZE = 512 * 1024; //65536;

void malloc_annex_b(VideoParameters* p_Vid, ANNEXB_t** p_annex_b)
{
	*p_annex_b = (ANNEXB_t*)calloc(1, sizeof(ANNEXB_t));
	(*p_annex_b)->Buf = (byte*)malloc(p_Vid->nalu->max_size);
}

void init_annex_b(ANNEXB_t* annex_b)
{
	annex_b->BitStreamFile = -1;
	annex_b->iobuffer = NULL;
	annex_b->iobufferread = NULL;
	annex_b->bytesinbuffer = 0;
	annex_b->is_eof = FALSE;
	annex_b->IsFirstByteStreamNALU = 1;
	annex_b->nextstartcodebytes = 0;
}

void free_annex_b(ANNEXB_t** p_annex_b)
{
	free((*p_annex_b)->Buf);
	(*p_annex_b)->Buf = NULL;
	free(*p_annex_b);
	*p_annex_b = NULL;
}

static inline int getChunk(ANNEXB_t* annex_b)
{
	unsigned int readbytes = read(annex_b->BitStreamFile, annex_b->iobuffer, annex_b->iIOBufferSize);
	if (0 == readbytes)
	{
		annex_b->is_eof = TRUE;
		return 0;
	}

	annex_b->bytesinbuffer = readbytes;
	annex_b->iobufferread = annex_b->iobuffer;
	return readbytes;
}

static inline byte getfbyte(ANNEXB_t* annex_b)
{
	if (annex_b->bytesinbuffer == 0)
	{
		if (getChunk(annex_b) == 0)
			return 0;
	}

	annex_b->bytesinbuffer--;
	return (*annex_b->iobufferread++);
}

static inline int FindStartCode(unsigned char* buffer, int zeros_in_startcode)
{
	for (int i = 0; i < zeros_in_startcode; i++)
	{
		if (*(buffer++) != 0)
			return 0;
	}

	return *buffer == 1;
}

int get_annex_b_NALU(VideoParameters* p_Vid, NALU_t* nalu, ANNEXB_t* annex_b)
{
	int i;
	int info2 = 0, info3 = 0, pos = 0;
	int StartCodeFound = 0;
	int LeadingZero8BitsCount = 0;
	byte* pBuf = annex_b->Buf;

	if (annex_b->nextstartcodebytes != 0)
	{
		for (i = 0; i < annex_b->nextstartcodebytes - 1; i++)
		{
			(*pBuf++) = 0;
			pos++;
		}
		(*pBuf++) = 1;
		pos++;
	}
	else
	{
		while (!annex_b->is_eof)
		{
			pos++;
			if ((*(pBuf++) = getfbyte(annex_b)) != 0)
				break;
		}
	}

	if (annex_b->is_eof == TRUE)
	{
		return pos == 0 ? 0 : -1;
	}

	if (pos == 3)
	{
		nalu->startcodeprefix_len = 3;
	}
	else
	{
		LeadingZero8BitsCount = pos - 4;
		nalu->startcodeprefix_len = 4;
	}

	LeadingZero8BitsCount = pos;
	annex_b->IsFirstByteStreamNALU = 0;

	while (!StartCodeFound)
	{
		if (annex_b->is_eof == TRUE)
		{
			pBuf -= 2;
			while (*(pBuf--) == 0)
				pos--;

			nalu->len = (pos - 1) - LeadingZero8BitsCount;
			memcpy(nalu->buf, annex_b->Buf + LeadingZero8BitsCount, nalu->len);
			nalu->forbidden_bit = (*(nalu->buf) >> 7) & 1;
			nalu->nal_reference_idc = (NalRefIdc)((*(nalu->buf) >> 5) & 3);
			nalu->nal_unit_type = (NaluType)((*(nalu->buf)) & 0x1f);
			annex_b->nextstartcodebytes = 0;

			return pos - 1;
		}

		pos++;
		*(pBuf++) = getfbyte(annex_b);
		info3 = FindStartCode(pBuf - 4, 3);

		if (info3 != 1)
		{
			info2 = FindStartCode(pBuf - 3, 2);
		}

		StartCodeFound = info3 != 1 ? (info2 & 1) : 1;
	}

	if (info3 == 1)
	{
		pBuf -= 5;
		while (*(pBuf--) == 0) pos--;
		annex_b->nextstartcodebytes = 4;
	}
	else if (info2 == 1)
		annex_b->nextstartcodebytes = 3;

	pos -= annex_b->nextstartcodebytes;

	nalu->len = pos - LeadingZero8BitsCount;
	fast_memcpy(nalu->buf, annex_b->Buf + LeadingZero8BitsCount, nalu->len);
	nalu->forbidden_bit = (*(nalu->buf) >> 7) & 1;
	nalu->nal_reference_idc = (NalRefIdc)((*(nalu->buf) >> 5) & 3);
	nalu->nal_unit_type = (NaluType)((*(nalu->buf)) & 0x1f);
	nalu->lost_packets = 0;

	return pos;
}

void open_annex_b(char* fn, ANNEXB_t* annex_b)
{
	annex_b->BitStreamFile = open(fn, OPENFLAGS_READ);
	annex_b->iIOBufferSize = IOBUFFERSIZE * sizeof(byte);
	annex_b->iobuffer = malloc(annex_b->iIOBufferSize);
	annex_b->is_eof = FALSE;
	getChunk(annex_b);
}

void close_annex_b(ANNEXB_t* annex_b)
{
	if (annex_b->BitStreamFile != -1)
	{
		close(annex_b->BitStreamFile);
		annex_b->BitStreamFile = -1;
	}

	free(annex_b->iobuffer);
	annex_b->iobuffer = NULL;
}

void reset_annex_b(ANNEXB_t* annex_b)
{
	annex_b->is_eof = FALSE;
	annex_b->bytesinbuffer = 0;
	annex_b->iobufferread = annex_b->iobuffer;
}
