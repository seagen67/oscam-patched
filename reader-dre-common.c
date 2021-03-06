#include "globals.h"
#include "oscam-string.h"
#include "reader-common.h"
#include "reader-dre-st20.h"
#include "reader-dre-common.h"

int32_t dre_common_get_emm_type(EMM_PACKET *ep, struct s_reader *rdr)
{
	switch(ep->emm[0])
	{
		case 0x87:
			ep->type = UNIQUE;
			memset(ep->hexserial, 0, 8);
			memcpy(ep->hexserial, &ep->emm[3], 4);
			return (!memcmp(&rdr->sa[0][0], &ep->emm[3], 4));

		case 0x83:
		case 0x89:
			ep->type = SHARED;
			// FIXME: Seems to be that SA is only used with caid 0x4ae1
			if(rdr->caid == 0x4ae1)
			{
				memset(ep->hexserial, 0, 8);
				memcpy(ep->hexserial, &ep->emm[3], 4);
				return (!memcmp(&rdr->sa[0][0], &ep->emm[3], 4));
			}
			else
				{ return 1; }

		case 0x8B:
			ep->type = UNIQUE;
			memset(ep->hexserial, 0, 8);
			memcpy(ep->hexserial, &ep->emm[3], 4);
			switch(ep->emm[7])
			{
				case 0x61:
					return (!memcmp(&rdr->hexserial[3] ,&ep->hexserial[1] , 2));
				case 0x62:
				case 0x63:
				default:
					return (!memcmp(&rdr->hexserial[2] ,ep->hexserial , 4));
			}
		
		case 0x80:
		case 0x86:
		case 0x8C:
			ep->type = SHARED;
			memset(ep->hexserial, 0, 8);
			ep->hexserial[0] = ep->emm[3];
			return ep->hexserial[0] == rdr->sa[0][0];
			
		case 0x82:
		case 0x91:
			ep->type = GLOBAL;
			return 1;

		default:
			ep->type = UNKNOWN;
			return 1;
	}
}

int32_t dre_common_get_emm_filter(struct s_reader *rdr, struct s_csystem_emm_filter **emm_filters, unsigned int *filter_count)
{
	if(*emm_filters == NULL)
	{
		const unsigned int max_filter_count = 9;
		if(!cs_malloc(emm_filters, max_filter_count * sizeof(struct s_csystem_emm_filter)))
			{ return ERROR; }

		struct s_csystem_emm_filter *filters = *emm_filters;
		*filter_count = 0;

		int32_t idx = 0;

		if(rdr->caid == 0x2710)
		{
			filters[idx].type = EMM_UNIQUE;
			filters[idx].enabled   = 1;
			filters[idx].filter[0] = 0x8B;
			memcpy(&filters[idx].filter[1], &rdr->hexserial[2] , 4);
			filters[idx].mask[0]   = 0xFF;
			filters[idx].mask[1]   = 0x00;
			filters[idx].mask[2]   = 0xFF;
			filters[idx].mask[3]   = 0xFF;
			filters[idx].mask[4]   = 0xF0;
			idx++;
		}
		else
		{
			filters[idx].type = EMM_SHARED;
			filters[idx].enabled   = 1;
			filters[idx].filter[0] = 0x80;
			filters[idx].filter[1] = rdr->sa[0][0];
			filters[idx].mask[0]   = 0xF2;
			filters[idx].mask[1]   = 0xFF;
			idx++;

			filters[idx].type = EMM_GLOBAL;
			filters[idx].enabled   = 1;
			filters[idx].filter[0] = 0x82;
			filters[idx].mask[0]   = 0xFF;
			idx++;

			filters[idx].type = EMM_SHARED;
			filters[idx].enabled   = 1;
			filters[idx].filter[0] = 0x83;
			filters[idx].filter[1] = rdr->sa[0][0];
			filters[idx].mask[0]   = 0xF3;
			if(rdr->caid == 0x4ae1)
			{
				memcpy(&filters[idx].filter[1], &rdr->sa[0][0], 4);
				memset(&filters[idx].mask[1], 0xFF, 4);
			}
			filters[idx].mask[1]   = 0xFF;
			idx++;

			filters[idx].type = EMM_SHARED;
			filters[idx].enabled   = 1;
			filters[idx].filter[0] = 0x86;
			filters[idx].filter[1] = rdr->sa[0][0];
			filters[idx].mask[0]   = 0xFF;
			filters[idx].mask[1]   = 0xFF;
			idx++;

			filters[idx].type = EMM_UNIQUE;
			filters[idx].enabled   = 1;
			filters[idx].filter[0] = 0x87;
			filters[idx].mask[0]   = 0xFF;
			
			memcpy(&filters[idx].filter[1], &rdr->sa[0][0], 4);
			memset(&filters[idx].mask[1], 0xFF, 4);
			
			idx++;
			
			filters[idx].type = EMM_SHARED;
			filters[idx].enabled   = 1;
			filters[idx].filter[0] = 0x89;
			filters[idx].mask[0]   = 0xFF;
			// FIXME: Seems to be that SA is only used with caid 0x4ae1
			if(rdr->caid == 0x4ae1)
			{
				memcpy(&filters[idx].filter[1], &rdr->sa[0][0], 4);
				memset(&filters[idx].mask[1], 0xFF, 4);
			}
			idx++;
		
			filters[idx].type = EMM_SHARED;
			filters[idx].enabled   = 1;
			filters[idx].filter[0] = 0x8C;
			filters[idx].filter[1] = rdr->sa[0][0];
			filters[idx].mask[0]   = 0xFF;
			filters[idx].mask[1]   = 0xFF;
			idx++;
			
			filters[idx].type = EMM_GLOBAL;
			filters[idx].enabled   = 1;
			filters[idx].filter[0] = 0x91;
			filters[idx].mask[0]   = 0xFF;
			idx++;
		}

		*filter_count = idx;
	}

	return OK;
}

// overcrypt code

extern uint8_t dre_initial_snippet[3694];

static uint16_t gId = 0xFFFF;
static uint32_t decrypt_addr;
static uint32_t gRawSec = 0;
static uint8_t gVersion = 0xFF;

typedef struct data_block_t
{
	uint8_t *data;
	uint32_t size;
	uint32_t used;
} data_block;

typedef struct memory_block_t
{
	uint8_t *pos;
	uint8_t *end;
} memory_block;

static data_block raw_buffer = { NULL, 0, 0 };
static data_block code_buffer = { NULL, 0, 0 };

static int offtin(uint8_t *buf)
{
	int y;
	y = buf[7] & 0x7F;
	y = y * 256; y += buf[6];
	y = y * 256; y += buf[5];
	y = y * 256; y += buf[4];
	y = y * 256; y += buf[3];
	y = y * 256; y += buf[2];
	y = y * 256; y += buf[1];
	y = y * 256; y += buf[0];
	if(buf[7] & 0x80) y -= y;
	return y;
}

static int bspatch(uint8_t *dest, uint8_t *src, int src_len, uint8_t *patch)
{
	int i, newsize, bzctrllen, bzdatalen, oldpos, newpos, ctrl[3];
	uint8_t *cstream, *dstream, *estream;

	if(memcmp(patch, "BSDIFF40", 8)) return -1;

	bzctrllen = offtin(patch + 8);
	bzdatalen = offtin(patch + 16);
	newsize = offtin(patch + 24);
	if((bzctrllen < 0) || (bzdatalen < 0) || (newsize < 0)) return -1;

	oldpos = 0;
	newpos = 0;
	cstream = patch + 32;
	dstream = cstream + bzctrllen;
	estream = dstream + bzdatalen;

	while(newpos < newsize)
	{
			/* Read control data */
		for(i = 0; i < 3; i++)
		{
			ctrl[i] = offtin(cstream);
			cstream += 8;
		}
			/* Sanity-check */
		if((newpos + ctrl[0]) > newsize) return -1;
			/* Read diff string */
		memcpy(dest + newpos, dstream, ctrl[0]);
		dstream += ctrl[0];
			/* Add old data to diff string */
		for(i = 0; i < ctrl[0]; i++)
		{
			if(((oldpos + i) >= 0) && ((oldpos + i) < src_len)) dest[newpos + i] += src[oldpos + i];
		}
			/* Adjust pointers */
		newpos += ctrl[0];
		oldpos += ctrl[0];
			/* Sanity-check */
		if((newpos + ctrl[1]) > newsize) return -1;
			/* Read extra string */
		memcpy(dest + newpos, estream, ctrl[1]);
		estream += ctrl[1];
			/* Adjust pointers */
		newpos += ctrl[1];
		oldpos += ctrl[2];
	}
	return newsize;
}

static int dre_unpack(uint8_t *dest, uint8_t *src, int len)
{
	uint8_t dbuf[0x1000], mask;
	int i, soffs, doffs, dbidx, boffs, n;

	dbidx = 4078;
	memset(dbuf, 32, 4078);
	for(soffs = 0, doffs = 0; soffs < len; )
	{
		mask = src[soffs++];
		for(i = 0; i < 8 && soffs < len; i++, mask >>= 1)
		{
			if(mask & 1)
			{
				dest[doffs++] = dbuf[dbidx] = src[soffs++];
				dbidx = (dbidx + 1) & 0xfff;
			}
			else
			{
				boffs = src[soffs++];
				n = src[soffs++];
				boffs |= (n & 0xf0) << 4;
				n &= 0xf;
				n += 3;
				while(n--)
				{
					boffs &= 0xfff;
					dest[doffs++] = dbuf[dbidx] = dbuf[boffs++];
					dbidx = (dbidx + 1) & 0xfff;
				}
			}
		}
	}
	return doffs;
}

typedef struct 
{
	int magic_number;               /* magic number - value        .    */
	int codesize;                   /* size of code in the rcu.         */
	int entrypointoffset;           /* entrypoint offset into the code. */
	int datasize;                   /* size of data region.             */
	int datalocationoffset;         /* offset to place data at.         */
	int bsssize;                    /* size of bss region.              */
	int bsslocationoffset;          /* offset to place bss at.          */
	int staticlinkoffset;           /* offset to staticlink in data.    */
	int relsize;                    /* size of relocation table.        */
	int conssize;                   /* size of constructor table.       */
	int dessize;                    /* size of destructor table.        */
	int stacksize;                  /* size of stack this rcu needs.    */
	int heapsize;                   /* size of heap this rcu needs.     */
	int dbgfilenamesize;            /* size of .dbg filename.           */
	int slot0;                      /* 4 words space for user use.      */
	int slot1;
	int slot2;
	int slot3;
} rcu_header_t;

static void rcu_load_offs(uint32_t *offs, uint8_t *buf, int size)
{
	uint32_t i;
	for(i = 0; i < size / sizeof(int); i++)
	{
		offs[i] = (uint32_t)(buf[3 + i * 4] << 24) | (buf[2 + i * 4] << 16) | (buf[1 + i * 4] << 8) | buf[0 + i * 4];
	}
}

static void rcu_load(uint8_t *rcu)
{
	int rcu_data_size;
	uint32_t i;
	uint32_t nexports, nimports;
	rcu_header_t rcuh;
	uint8_t *ptr, *rcu_code, *rcu_data;

	rcu_load_offs((uint32_t *)&rcuh, rcu, sizeof(rcu_header_t));
	ptr = rcu + sizeof(rcu_header_t);
	rcu_code = ptr;
	ptr += rcuh.codesize;
	rcu_data_size = rcuh.datasize + rcuh.bsssize;
	rcu_data = ptr;
	ptr += rcuh.datasize;

	if(rcuh.relsize)
	{
		uint32_t relocations[rcuh.relsize / sizeof(int)];
		rcu_load_offs(relocations, ptr, rcuh.relsize);
		ptr += rcuh.relsize;
	}
	if(rcuh.conssize)
	{
		uint32_t constructors[rcuh.conssize / sizeof(int)];
		rcu_load_offs(constructors, ptr, rcuh.conssize);
		ptr += rcuh.conssize;
	}
	if(rcuh.dessize)
	{
		uint32_t destructors[rcuh.dessize / sizeof(int)];
		rcu_load_offs(destructors, ptr, rcuh.dessize);
		ptr += rcuh.dessize;
	}

	ptr = rcu_data + rcu_data_size - 16 + 4;
	rcu_load_offs(&nexports, ptr, sizeof(int));
	if(nexports)
	{
		uint32_t exports[nexports * 3];
		rcu_load_offs(exports, ptr - nexports * sizeof(int) * 3, nexports * sizeof(int) * 3);
		for(i = 0; i < nexports; i++)
		{
			char *symbol = (char *) (rcu_code + exports[0 + i * 3]);
			uint32_t faddr = exports[2 + i * 3];
			if(strcmp(symbol, "snippet_decrypt") == 0) decrypt_addr = faddr;
		}
	}
	ptr -= nexports * 3 * sizeof(int) + 8;
	rcu_load_offs(&nimports, ptr, sizeof(int));
	if(nimports)
	{
		uint32_t imports[nimports * 3];
		rcu_load_offs(imports, ptr - nimports * sizeof(int) * 3, nimports * sizeof(int) * 3);
	}
}

int load_sections(uint8_t *body)
{
	uint8_t section[4096];
	uint16_t sect_len, sect_no, last_sect_no, curr_sect_no = 0;
	int body_len, total_body_len = 0; 
	uint32_t i=0;

	while((i+3)<raw_buffer.used)
	{
		memcpy(section, raw_buffer.data+i, 3);
		i+=3;
		memcpy(&sect_len, section+1, 2);
		sect_len = ntohs(sect_len) & 0xfff;
		
		memcpy(section + 3, raw_buffer.data+i, sect_len);
		i+=sect_len;

		if(section[0] != 0x91 || sect_len < (4 + 5)) continue;

		uint32_t crc = crc32(-1, section, sect_len + 3 - 4);
		uint32_t sect_crc = (uint32_t) section[sect_len + 2] << 24 | section[sect_len + 1] << 16 | section[sect_len] << 8 | section[sect_len - 1];
		
		if(crc != sect_crc)
		{
			cs_log("[icg] Broken section crc %08x %08x\n",(uint32_t) crc,(uint32_t) sect_crc);
			continue;
		}

		sect_no = section[6];
		last_sect_no = section[7];
		
		if(curr_sect_no == 0)
		{
			gId = (section[13] << 8) | section[14];
			gVersion = (section[5] & 0x3e) >> 1;
		}
		if(curr_sect_no == sect_no)
		{
			body_len = sect_len - 5 - 4;
			memcpy(body, section + 8, body_len);
			total_body_len += body_len;
			body += body_len;
			curr_sect_no++;
		}
		if(curr_sect_no > last_sect_no) break;
	}
	return total_body_len;
}

static int32_t allocate_data_block(data_block *buff, uint32_t size)
{
	if (buff->data == NULL)
	{
		buff->data = malloc(size);
		if (buff->data == NULL) { return -1; }
		buff->size = size;
		buff->used = 0;
	}
	if (buff->size < size)
	{
		uint8_t *new_buff;

		new_buff = malloc(size);
		if (new_buff == NULL) { return -1; }
		memcpy(new_buff, buff->data, buff->used);

		free(buff->data);

		buff->data = new_buff;
		buff->size = size;
	}
	return 0;
}

uint8_t Drecrypt2OverCW(uint16_t overcryptId, uint8_t *cw)
{
	if(overcryptId == gId)
	{
		if(st20_run(code_buffer.data, code_buffer.used, decrypt_addr, cw, overcryptId)) return 0;
		else return 1;
	}
	else
	{
		cs_log("[icg] ICG algo %04X not found", overcryptId);
	}
	return 2;
}

void Drecrypt2OverEMM(uint8_t *emm)
{	
	uint32_t dataLen;
	
	if(gVersion == (emm[5] & 0x3e) >> 1)
	{
		return;
	}
	
	if(emm[6] == 0 && (gId == ((emm[13] << 8) | emm[14])))
	{
		return;
	}

	if (gRawSec == 0)
	{
		if(emm[6] != 0)
		{
			return;
		}
	}

	if (emm[6] != gRawSec)
	{
		gRawSec = 0;
		return;
	}

	dataLen = ((emm[1] & 0xF) << 8) | emm[2];
	dataLen += 3;
	if(dataLen < 4)
	{
		return;
	}

	if (gRawSec == 0)
	{
		raw_buffer.used = 0;
	}

	if (allocate_data_block(&raw_buffer, raw_buffer.used + dataLen) < 0)
	{
		cs_log("[icg] No free memory");
		return;
	}

	memcpy(raw_buffer.data + raw_buffer.used, emm, dataLen);
	raw_buffer.used += dataLen;

	if (emm[6] != emm[7])
	{
		gRawSec++;
		return;
	}

	gRawSec = 0;

	int patch_len, rcu_len, len, snip_len;
	uint8_t *buf = malloc(0x1000), *snip = malloc(0x10000), *rcu = malloc(0x10000), *patch = malloc(0x10000);
	
	if(buf == NULL || snip == NULL || rcu == NULL || patch == NULL) {cs_log("[icg] No free memory"); goto exit_werr;}
	
    snip_len = (dre_initial_snippet[4] << 24) | (dre_initial_snippet[5] << 16) | (dre_initial_snippet[6] << 8) | dre_initial_snippet[7];
    
    if(dre_unpack(snip, dre_initial_snippet + 8, sizeof(dre_initial_snippet) - 8) >= snip_len)
    {
		if((len = load_sections(buf)) > 0)
		{
			patch_len = (buf[14] << 24) | (buf[15] << 16) | (buf[16] << 8) | buf[17];
			if(dre_unpack(patch, buf + 18, len - 18) >= patch_len)
			{
				rcu_len = bspatch(rcu, snip, snip_len, patch);
				if(rcu_len > 0)
				{
					rcu_load(rcu);
					if(allocate_data_block(&code_buffer,rcu_len) < 0) {cs_log("[icg] No free memory"); goto exit_werr;}
					memcpy(code_buffer.data, rcu, rcu_len);
					code_buffer.used = (uint32_t) rcu_len;
					cs_log("[icg] snippet patch created. ICG algo %04X", gId);
				}
			}
		}
	}
exit_werr:	
	if(buf != NULL) free(buf);
	if(snip != NULL) free(snip);
	if(rcu != NULL) free(rcu);
	if(patch != NULL) free(patch);
}

void ReasmEMM82(uint8_t *emm)
{
	uint16_t dataLen = (uint16_t) (((emm[1] & 0xF) << 8) | emm[2]) + 5;
	uint8_t emmbuf[dataLen];
	uint32_t crc;
	
	emmbuf[0] = 0x91;
	emmbuf[1] = (uchar)(((dataLen-3) >> 8) | 0x80);
	emmbuf[2] = (uchar)(dataLen-3) & 0xFF;
	emmbuf[3] = ((emm[7] + 1) & 0x0F);
	emmbuf[4] = 0;
	
	memcpy(&emmbuf[5], &emm[7], dataLen);
	
	emmbuf[5] += 1;
	
	crc = crc32(-1, emmbuf, dataLen-4);
	
	emmbuf[dataLen-1] = (uchar) ((crc >> 24) & 0xFF);
	emmbuf[dataLen-2] = (uchar) ((crc >> 16) & 0xFF);
	emmbuf[dataLen-3] = (uchar) ((crc >> 8) & 0xFF);
	emmbuf[dataLen-4] = (uchar) (crc & 0xFF);
	
	Drecrypt2OverEMM(emmbuf);
}

uint8_t dre_initial_snippet[3694] =
{
	0x6F, 0xF0, 0x0E, 0x0D, 0x00, 0x00, 0x18, 0x75, 0xFF, 0x75, 0x63, 0x72, 0x2E, 0x2C, 0x17, 0x00, 
	0x00, 0x3F, 0x3D, 0x15, 0x00, 0x00, 0x9C, 0x00, 0xFB, 0xF7, 0xFA, 0xF5, 0x85, 0x4C, 0xFB, 0xF0, 
	0x04, 0xFB, 0xF8, 0xFB, 0xF1, 0xF7, 0xF0, 0x24, 0x0E, 0x42, 0xFF, 0x22, 0xF0, 0x41, 0x22, 0xF0, 
	0x60, 0xBF, 0x73, 0xFF, 0x24, 0x4B, 0x72, 0x9D, 0xD0, 0x70, 0x42, 0xF4, 0xBF, 0xA4, 0x70, 0xB1, 
	0x22, 0xF0, 0x40, 0x49, 0x00, 0x64, 0xFF, 0xB0, 0x25, 0x73, 0xD0, 0x24, 0x4C, 0x25, 0x72, 0xFF, 
	0x25, 0xFF, 0x21, 0x22, 0x29, 0xA2, 0x25, 0x72, 0xFF, 0x42, 0x21, 0xFB, 0xFA, 0xF6, 0x22, 0x26, 
	0x03, 0xFF, 0x80, 0x26, 0x2A, 0x0E, 0x80, 0x22, 0x2B, 0x07, 0xFF, 0x80, 0x22, 0x28, 0x05, 0x80, 
	0x26, 0x2C, 0x07, 0xFF, 0x80, 0x2C, 0x24, 0x05, 0x80, 0x27, 0x20, 0x09, 0xBF, 0x80, 0x26, 0x2E, 
	0x00, 0x80, 0x23, 0x73, 0x00, 0x22, 0xFF, 0x2C, 0x09, 0x80, 0x25, 0x22, 0x0B, 0x80, 0x25, 0xFF, 
	0x2C, 0x04, 0x80, 0x21, 0x21, 0x2F, 0x03, 0x2A, 0xFF, 0x24, 0x08, 0x80, 0x2A, 0x27, 0x0F, 0x80, 
	0x23, 0x7D, 0x2F, 0x8C, 0x00, 0x2D, 0x05, 0x80, 0x23, 0x2C, 0x84, 0x01, 0xFA, 0x90, 0x00, 0x2F, 
	0x78, 0x00, 0x26, 0x08, 0x80, 0x2E, 0x0A, 0xFF, 0x80, 0x80, 0x27, 0x24, 0x09, 0x80, 0x27, 0x2C, 
	0xCF, 0x06, 0x80, 0x27, 0x29, 0x78, 0x00, 0x6F, 0x00, 0x2A, 0x2F, 0xFF, 0x0D, 0x80, 0x28, 0x23, 
	0x05, 0x80, 0x2B, 0x28, 0xF7, 0x08, 0x80, 0x28, 0xB7, 0x00, 0x26, 0x2F, 0x0E, 0x80, 0xFF, 0x27, 
	0x25, 0x00, 0x80, 0x2B, 0x21, 0x06, 0x80, 0xFF, 0x2A, 0x25, 0x01, 0x80, 0x21, 0x28, 0x00, 0x80, 
	0xCB, 0x22, 0x2E, 0xB0, 0x01, 0x0D, 0x6D, 0x00, 0x88, 0x00, 0x28, 0x03, 0xFE, 0x91, 0x00, 0x0C, 
	0x80, 0x2C, 0x2D, 0x0E, 0x80, 0x2A, 0xB5, 0x04, 0xBC, 0x00, 0x2A, 0xD4, 0x00, 0x27, 0x0C, 0xE5, 
	0x00, 0x07, 0xFF, 0x80, 0x23, 0x2D, 0x0D, 0x80, 0x25, 0x21, 0x00, 0xFF, 0x80, 0x24, 0x24, 0x0D, 
	0x80, 0x24, 0x2B, 0x08, 0xFF, 0x80, 0x24, 0x2D, 0x0C, 0x80, 0x2D, 0x21, 0x0A, 0xFE, 0x0D, 0x10, 
	0x0C, 0x80, 0x28, 0x28, 0x02, 0x80, 0x26, 0xDF, 0x27, 0x0A, 0x80, 0x2A, 0x21, 0x24, 0x10, 0x20, 
	0x08, 0xEA, 0x85, 0x00, 0x03, 0x21, 0x10, 0x0A, 0x19, 0x10, 0x07, 0x80, 0x2F, 0xFE, 0xCF, 0x01, 
	0x2D, 0x0B, 0x80, 0x2A, 0x24, 0x00, 0x80, 0xFF, 0x29, 0x20, 0x0E, 0x80, 0x26, 0x22, 0x0A, 0x80, 
	0xFF, 0x21, 0x20, 0x25, 0x01, 0x2A, 0x21, 0x01, 0x80, 0xD3, 0x2A, 0x03, 0xBC, 0x00, 0x6F, 0x10, 
	0x24, 0x6F, 0x10, 0x21, 0x22, 0xEF, 0x03, 0x80, 0x2C, 0x02, 0x78, 0x10, 0x20, 0x29, 0x0E, 0xFF, 
	0x28, 0x27, 0x00, 0x80, 0x27, 0x2F, 0x03, 0x80, 0xFB, 0x29, 0x21, 0x8C, 0x00, 0x73, 0x45, 0x25, 
	0x71, 0x61, 0xFB, 0x2B, 0x96, 0x43, 0x01, 0x2D, 0x20, 0xAE, 0x70, 0x25, 0xFF, 0xB0, 0x22, 0xF0, 
	0x70, 0xF1, 0x41, 0x24, 0xF6, 0xF5, 0xC0, 0xA2, 0x12, 0x81, 0xA7, 0x17, 0x21, 0x1D, 0x42, 0x24, 
	0x7F, 0xFA, 0x21, 0x7D, 0x4C, 0x27, 0xF8, 0xD1, 0xC1, 0x10, 0xEF, 0x24, 0xF0, 0x21, 0xDD, 0xC7, 
	0x11, 0xF1, 0x21, 0xDD, 0xDF, 0x71, 0x21, 0x7D, 0x23, 0xF3, 0xCC, 0x10, 0x1D, 0x70, 0x6A, 0xBE, 
	0x10, 0x42, 0xB7, 0x13, 0x1F, 0xBE, 0x11, 0x7F, 0x43, 0xC4, 0x11, 0xEB, 0x7F, 0x43, 0xCA, 0x10, 
	0xDF, 0xF3, 0x11, 0xF1, 0x21, 0xDF, 0x77, 0x71, 0x21, 0x7F, 0xD8, 0x10, 0xDF, 0x21, 0x1F, 0xDE, 
	0x18, 0x3B, 0x1E, 0x44, 0xBF, 0x10, 0x7E, 0x21, 0x45, 0xC4, 0x11, 0x1A, 0x20, 0xFA, 0xCA, 0x10, 
	0xDE, 0x20, 0x22, 0xF1, 0x21, 0xDE, 0x71, 0x21, 0x3D, 0x7E, 0xD8, 0x10, 0xDE, 0x21, 0x1E, 0x70, 
	0x16, 0x20, 0xE2, 0x13, 0x7B, 0x22, 0x14, 0x16, 0x20, 0x22, 0x74, 0x21, 0x47, 0xC4, 0x10, 0xDE, 
	0x48, 0x21, 0x24, 0xF0, 0x22, 0xD4, 0x4F, 0x22, 0xF1, 0x22, 0xBF, 0xD4, 0x71, 0x22, 0x74, 0x23, 
	0xF3, 0x55, 0x20, 0x14, 0x3A, 0x39, 0x28, 0x18, 0x45, 0x21, 0x78, 0x21, 0x4E, 0x4C, 0x21, 0x78, 
	0x20, 0xFA, 0x53, 0x20, 0xD8, 0x7E, 0x22, 0xF1, 0x22, 0xD8, 0x71, 0x22, 0x5D, 0x78, 0x62, 0x20, 
	0xD8, 0x22, 0x18, 0x39, 0x28, 0x1A, 0x45, 0x21, 0x5B, 0x7A, 0x49, 0x4C, 0x21, 0x7A, 0x49, 0x53, 
	0x20, 0xDA, 0xAC, 0x21, 0xBF, 0xF1, 0x22, 0xDA, 0x71, 0x22, 0x7A, 0x62, 0x20, 0xDA, 0x7B, 0x22, 
	0x1A, 0x39, 0x27, 0x81, 0x21, 0x1A, 0x43, 0xBF, 0x10, 0xD1, 0x7A, 0x4B, 0x21, 0xD3, 0x20, 0xCA, 
	0x10, 0xDA, 0xD9, 0x21, 0xF1, 0x21, 0xE7, 0xDA, 0x71, 0x21, 0xBC, 0x20, 0xDE, 0x20, 0x1A, 0x70, 
	0x81, 0xAC, 0xD0, 0x20, 0xC7, 0x24, 0x22, 0x19, 0x45, 0x21, 0x79, 0x7A, 0x22, 0x79, 0xFA, 0x81, 
	0x21, 0xD9, 0x07, 0x31, 0xF1, 0x22, 0xD9, 0x71, 0x22, 0x7D, 0x79, 0x62, 0x20, 0xD9, 0x22, 0x19, 
	0x70, 0x81, 0xC4, 0x27, 0xAB, 0x22, 0x1B, 0x45, 0x21, 0x7B, 0x4B, 0x22, 0x7B, 0x52, 0x21, 0xDB, 
	0x7E, 0x35, 0x31, 0xF1, 0x22, 0xDB, 0x71, 0x22, 0x7B, 0x62, 0x20, 0x57, 0xDB, 0x22, 0x1B, 0x1E, 
	0x3A, 0x17, 0x45, 0x21, 0x77, 0xA8, 0x22, 0xF5, 0x77, 0xAE, 0x21, 0xD7, 0x63, 0x31, 0xF1, 0x22, 
	0xD7, 0x71, 0xBB, 0x22, 0x77, 0x62, 0x20, 0xD7, 0x22, 0x17, 0x1E, 0x38, 0x82, 0x1B, 0x22, 0x13, 
	0xD0, 0x20, 0x22, 0x73, 0xEF, 0x11, 0x8B, 0x30, 0x53, 0x20, 0xFD, 0xD3, 0x91, 0x31, 0xF1, 0x22, 
	0xD3, 0x71, 0x22, 0x73, 0xBE, 0x62, 0x20, 0xD3, 0x22, 0x13, 0x70, 0x82, 0xF2, 0x26, 0x82, 0xDB, 
	0x22, 0x12, 0x88, 0x31, 0x72, 0x44, 0x4C, 0x21, 0x72, 0x44, 0xFA, 0x53, 0x20, 0xD2, 0xBF, 0x31, 
	0xF1, 0x22, 0xD2, 0x71, 0x22, 0x5D, 0x72, 0x62, 0x20, 0xD2, 0x22, 0x12, 0xA8, 0x3A, 0x15, 0x88, 
	0x31, 0x5B, 0x75, 0x46, 0x4C, 0x21, 0x75, 0x46, 0x53, 0x20, 0xD5, 0xED, 0x31, 0xBF, 0xF1, 0x22, 
	0xD5, 0x71, 0x22, 0x75, 0x62, 0x20, 0xD5, 0x6B, 0x22, 0x15, 0xA8, 0x3A, 0x11, 0x45, 0x21, 0x71, 
	0x4F, 0x4C, 0x21, 0xF3, 0x71, 0x4F, 0x53, 0x20, 0x1A, 0x42, 0xF1, 0x22, 0xD1, 0x71, 0xFB, 0x22, 
	0x71, 0x62, 0x20, 0xD1, 0x22, 0x11, 0x70, 0x82, 0x16, 0x7C, 0x37, 0x21, 0x1C, 0x16, 0x21, 0x7C, 
	0x1B, 0x23, 0x44, 0x40, 0xCA, 0x10, 0xFD, 0xDC, 0x4A, 0x42, 0xF1, 0x21, 0xDC, 0x71, 0x21, 0x7C, 
	0x6E, 0xD8, 0x10, 0xDC, 0x21, 0x1C, 0x32, 0x49, 0x22, 0x10, 0x45, 0x21, 0x47, 0x70, 0x21, 0x4A, 
	0x4C, 0x21, 0x75, 0x40, 0x53, 0x20, 0xD0, 0x7B, 0x42, 0xBF, 0xF1, 0x22, 0xD0, 0x71, 0x22, 0x70, 
	0x62, 0x20, 0xD0, 0xB3, 0x22, 0x10, 0x32, 0x4A, 0x2B, 0x31, 0x21, 0x7B, 0xEF, 0x12, 0x7B, 0xFA, 
	0xF5, 0x11, 0xDB, 0xAB, 0x41, 0xF1, 0x21, 0xDB, 0x71, 0x21, 0x74, 0x45, 0x30, 0xB0, 0x40, 0x1B, 
	0x32, 0x48, 0x51, 0x21, 0x11, 0xBE, 0x11, 0xE5, 0x71, 0xC3, 0x12, 0x71, 0xC9, 0x11, 0xD8, 0x42, 
	0xF1, 0x21, 0xD1, 0x73, 0x71, 0x21, 0x2B, 0x40, 0xDE, 0x40, 0x11, 0x70, 0x51, 0xDF, 0x16, 0x57, 
	0x51, 0x21, 0x10, 0xBE, 0x11, 0x70, 0xD5, 0x22, 0x70, 0xDB, 0x21, 0x7D, 0xD0, 0x07, 0x51, 0xF1, 
	0x21, 0xD0, 0x71, 0x21, 0x8D, 0x40, 0xAA, 0x0C, 0x50, 0x10, 0xF0, 0x49, 0x1E, 0xBE, 0x10, 0x7E, 
	0xA8, 0x21, 0x7E, 0xBA, 0xAE, 0x20, 0xDE, 0x33, 0x50, 0xF1, 0xDE, 0x71, 0x32, 0x20, 0xDE, 0xD9, 
	0x1E, 0xF0, 0x49, 0xE3, 0x31, 0x75, 0x4D, 0xC4, 0x10, 0x75, 0x4D, 0x77, 0x24, 0xF0, 0xD5, 0x58, 
	0x50, 0xF1, 0xD5, 0x71, 0xFD, 0x30, 0xAF, 0xD5, 0x15, 0x70, 0x51, 0xF2, 0x26, 0x51, 0xB5, 0x31, 
	0x72, 0xEA, 0xE9, 0x31, 0x72, 0xEF, 0x30, 0xD2, 0x7D, 0x50, 0xF1, 0xD2, 0x71, 0xA6, 0xCF, 0x30, 
	0xD2, 0x12, 0x68, 0x59, 0x87, 0x31, 0x73, 0xA8, 0x21, 0x73, 0xBA, 0xAE, 0x20, 0xD3, 0xA2, 0x50, 
	0xF1, 0xD3, 0x71, 0xA1, 0x30, 0xD3, 0xD9, 0x13, 0x68, 0x59, 0x44, 0x21, 0x74, 0x4B, 0xC4, 0x10, 
	0x74, 0x4B, 0x77, 0x24, 0xF0, 0xD4, 0xC7, 0x50, 0xF1, 0xD4, 0x71, 0x61, 0x20, 0xCF, 0xD4, 0x14, 
	0x70, 0x51, 0xC4, 0x47, 0x11, 0x41, 0x71, 0x21, 0x9A, 0xA8, 0x20, 0xD2, 0xE7, 0x50, 0x24, 0xF0, 
	0xE6, 0x40, 0xB5, 0x20, 0xD1, 0xA5, 0x72, 0x2B, 0x40, 0xD1, 0xEF, 0x40, 0xC4, 0x47, 0x16, 0x16, 
	0x20, 0x76, 0xB9, 0x21, 0xC3, 0x51, 0x0F, 0x60, 0x24, 0xF0, 0xD6, 0x15, 0x61, 0xF1, 0x7F, 0xD6, 
	0x71, 0x76, 0x23, 0xF3, 0xD6, 0x16, 0xD7, 0x59, 0x72, 0x59, 0x31, 0x77, 0x79, 0x22, 0x37, 0x60, 
	0x24, 0xF0, 0xD7, 0x3D, 0x61, 0xB7, 0xF1, 0xD7, 0x71, 0x73, 0x30, 0xD7, 0x17, 0xD7, 0x58, 0x85, 
	0x55, 0x1F, 0xD0, 0x20, 0x7F, 0xBB, 0x31, 0x7F, 0xC1, 0x30, 0xDF, 0x64, 0x60, 0xF7, 0xF1, 0xDF, 
	0x71, 0x03, 0x20, 0xDF, 0x1F, 0x70, 0x85, 0x6E, 0xF2, 0x26, 0x86, 0x22, 0x16, 0xBE, 0x10, 0x22, 
	0x76, 0x77, 0x42, 0xF5, 0x76, 0x7E, 0x41, 0xD6, 0x8B, 0x61, 0xF1, 0x22, 0xD6, 0x71, 0xB9, 0x22, 
	0x22, 0x60, 0x90, 0x60, 0x16, 0x70, 0x86, 0xDF, 0x16, 0x86, 0x55, 0x19, 0xBE, 0x10, 0x79, 0xEF, 
	0x11, 0x79, 0xF5, 0x10, 0xD9, 0xB7, 0x60, 0xB7, 0xF1, 0xD9, 0x71, 0x17, 0x30, 0xD9, 0x19, 0xA2, 
	0x69, 0x1A, 0xF6, 0xBE, 0x10, 0x7A, 0x48, 0xC4, 0x10, 0x7A, 0x48, 0x24, 0xF0, 0xDD, 0xDA, 0xDC, 
	0x60, 0xF1, 0xDA, 0x71, 0xBC, 0x20, 0xDA, 0x1A, 0xAA, 0xA2, 0x69, 0x18, 0xBE, 0x10, 0x78, 0xA8, 
	0x21, 0x78, 0xAE, 0x20, 0xD8, 0x6E, 0x01, 0x70, 0xF1, 0xD8, 0x71, 0x90, 0x20, 0xD8, 0x18, 0xA2, 
	0x68, 0x9F, 0x87, 0x1B, 0x41, 0x24, 0xFA, 0xA6, 0x42, 0xAC, 0x41, 0xDB, 0xF6, 0xB3, 0x41, 0xDB, 
	0x71, 0x45, 0x30, 0xDB, 0x1B, 0x70, 0x87, 0x54, 0x1E, 0x70, 0x16, 0x74, 0x1C, 0x1E, 0x70, 0x7C, 
	0xBB, 0x31, 0x7C, 0xC1, 0x30, 0xDD, 0xDC, 0x4B, 0x70, 0xF1, 0xDC, 0x71, 0x5C, 0x40, 0xDC, 0x1C, 
	0xAA, 0x36, 0x79, 0x1D, 0x1E, 0x70, 0x7D, 0x1C, 0x21, 0x7D, 0x23, 0x20, 0xDD, 0x6E, 0x70, 0x70, 
	0xF1, 0xDD, 0x71, 0xD7, 0x10, 0xDD, 0x1D, 0x36, 0x77, 0xFE, 0xE2, 0x12, 0x40, 0xD1, 0x70, 0x71, 
	0xF2, 0xF1, 0xD2, 0xBF, 0x70, 0x41, 0x71, 0xFC, 0xF2, 0xF1, 0x91, 0x70, 0x23, 0xEB, 0xFB, 0x72, 
	0x97, 0x70, 0x70, 0x9E, 0x70, 0x71, 0x81, 0xD1, 0xBF, 0x41, 0x71, 0xF9, 0xA2, 0x62, 0x0F, 0x8A, 
	0x7A, 0x42, 0x50, 0x98, 0x77, 0xBF, 0x70, 0xA5, 0x7F, 0x8F, 0x71, 0x85, 0x93, 0x71, 0x47, 0x98, 
	0x73, 0xF1, 0x85, 0x9E, 0x71, 0xE8, 0x70, 0xCD, 0x77, 0xA3, 0x80, 0x62, 0x0C, 0x4A, 0x8A, 0x76, 
	0x86, 0xE4, 0x78, 0x86, 0xF0, 0x7F, 0x02, 0x89, 0x81, 0x93, 0x71, 0x45, 0x44, 0x98, 0x73, 0x81, 
	0x9E, 0x71, 0x3E, 0x80, 0xCD, 0x74, 0x42, 0xFF, 0x7C, 0x05, 0x83, 0xE4, 0x78, 0x83, 0x1B, 0x8B, 
	0x54, 0x8D, 0x39, 0x82, 0xE8, 0x74, 0x45, 0x82, 0x2A, 0x1F, 0x87, 0x43, 0xFF, 0x7C, 0x82, 0xE4, 
	0x78, 0x82, 0x9C, 0x8F, 0x02, 0x89, 0xD4, 0xE4, 0x78, 0x1B, 0x8B, 0x44, 0xFF, 0x72, 0x0E, 0xE2, 
	0x12, 0x2A, 0x23, 0xFF, 0x4F, 0x21, 0xFB, 0x23, 0x1C, 0x21, 0x40, 0x24, 0xFF, 0xFA, 0x40, 0xD1, 
	0x23, 0x1C, 0x71, 0x70, 0xF2, 0xE9, 0xF1, 0xC1, 0x30, 0x1B, 0x91, 0xF1, 0x19, 0x91, 0x4F, 0x24, 
	0xF6, 0x3F, 0x23, 0x1C, 0xF2, 0xF1, 0x24, 0xFB, 0x19, 0x90, 0xCF, 0x72, 0xB5, 0x48, 0xAD, 0x71, 
	0x07, 0x05, 0x93, 0x21, 0x44, 0x0D, 0x90, 0x18, 0x3A, 0x11, 0x94, 0x18, 0x19, 0x9F, 0xF6, 0x23, 
	0x18, 0x2E, 0x9F, 0xE2, 0x12, 0x7F, 0x29, 0x2E, 0x49, 0x21, 0xFB, 0x24, 0x10, 0x11, 0x93, 0x3B, 
	0x24, 0x10, 0x54, 0x9F, 0xF6, 0x24, 0x10, 0x69, 0x9F, 0x7B, 0x93, 0x33, 0x2B, 0x4E, 0x0D, 0x90, 
	0x86, 0x94, 0x23, 0x10, 0x62, 0x82, 0x1D, 0x95, 0xA0, 0x62, 0x82, 0x29, 0x91, 0xA3, 0x92, 0x6E, 
	0x83, 0xD1, 0x70, 0x45, 0xAD, 0x71, 0x04, 0x56, 0x7B, 0x93, 0x29, 0x40, 0x0D, 0x90, 0x14, 0x11, 
	0x94, 0x14, 0x8F, 0x9F, 0x67, 0xF6, 0x23, 0x14, 0xA4, 0x9F, 0x7B, 0x93, 0x26, 0x45, 0x83, 0x90, 
	0xC2, 0x4B, 0x94, 0x24, 0x53, 0x9F, 0xA0, 0x90, 0x68, 0x9F, 0x7A, 0x94, 0x23, 0x4A, 0xD7, 0x21, 
	0xFB, 0x22, 0x10, 0x94, 0x22, 0x18, 0x9F, 0x24, 0xF6, 0xA9, 0x22, 0x2D, 0x9F, 0x7A, 0x94, 0x20, 
	0x0C, 0x90, 0x24, 0xFF, 0x94, 0x24, 0xB0, 0x07, 0xAF, 0xA0, 0x90, 0x1C, 0xAF, 0x3F, 0x93, 0x28, 
	0x2E, 0x47, 0x90, 0x24, 0x82, 0x10, 0x94, 0x24, 0x7D, 0xAF, 0xA0, 0x90, 0x92, 0xAA, 0xAC, 0x72, 
	0x3F, 0x93, 0x4C, 0xDF, 0x21, 0xD4, 0x4D, 0x21, 0xD5, 0x52, 0x00, 0x42, 0x21, 0xFF, 0x14, 0x25, 
	0x71, 0x28, 0x2B, 0x95, 0xD0, 0x70, 0xF9, 0xA5, 0xA1, 0x12, 0xE2, 0x12, 0x21, 0x4E, 0x21, 0xD8, 
	0x22, 0x57, 0x40, 0x21, 0xD9, 0x26, 0xB2, 0x18, 0x2C, 0xB0, 0x29, 0x30, 0xBB, 0xFF, 0x23, 0x44, 
	0x21, 0xD6, 0x23, 0x45, 0x21, 0xD7, 0xEA, 0x26, 0xB2, 0x16, 0x2C, 0xB0, 0x27, 0x30, 0xBB, 0x46, 
	0x21, 0xD2, 0xAF, 0x24, 0x40, 0x21, 0xD3, 0x26, 0xB2, 0x12, 0x2C, 0xB0, 0x25, 0xDC, 0x99, 0x10, 
	0x33, 0xB8, 0x25, 0x73, 0x21, 0x94, 0x10, 0x6B, 0x2A, 0xBF, 0x9A, 0xD0, 0x70, 0x41, 0xF4, 0xA7, 
	0x32, 0xB4, 0x70, 0x5F, 0xC1, 0xC0, 0xA2, 0x21, 0x01, 0x9D, 0xB0, 0x47, 0xA1, 0xB0, 0x7B, 0x28, 
	0x9F, 0x90, 0xBF, 0x71, 0x6B, 0x27, 0x99, 0xA6, 0xBF, 0xDA, 0x9D, 0xB0, 0x49, 0xA1, 0xB0, 0x25, 
	0x9E, 0xC1, 0xBF, 0x71, 0x6B, 0x53, 0x24, 0x98, 0xD7, 0xBF, 0x9D, 0xB0, 0x4F, 0xA1, 0xB0, 0x22, 
	0x42, 0x00, 0xF6, 0xF4, 0xBF, 0x21, 0x97, 0x08, 0xCF, 0x25, 0x73, 0x22, 0x4B, 0xDF, 0x25, 0x71, 
	0x6C, 0x2F, 0x9C, 0x23, 0xCF, 0x71, 0x6C, 0x79, 0x2E, 0x99, 0x10, 0x3B, 0xCC, 0x00, 0x25, 0x73, 
	0x46, 0x4F, 0xC0, 0x95, 0x2C, 0x53, 0xCD, 0x22, 0xEC, 0xB0, 0x6C, 0x98, 0x11, 0x3B, 0xCF, 0x22, 
	0xAD, 0x4A, 0x4F, 0xC0, 0x29, 0x9B, 0x54, 0xCC, 0x40, 0x4F, 0xC0, 0x28, 0xDC, 0x99, 0x12, 0x33, 
	0xB3, 0x25, 0x73, 0x41, 0x4F, 0xC0, 0x27, 0x94, 0x5A, 0xCA, 0xC9, 0x42, 0x4F, 0xC0, 0x26, 0x92, 
	0xCA, 0xC9, 0x43, 0x4F, 0xC0, 0x2B, 0x25, 0x90, 0xCA, 0xC9, 0x44, 0x4F, 0xC0, 0x23, 0xF1, 0xB0, 
	0xCC, 0xC5, 0x4C, 0x98, 0xB4, 0x1D, 0xC0, 0x6C, 0x22, 0x38, 0xC0, 0xCC, 0xC7, 0x21, 0xC4, 0xC1, 
	0x35, 0x21, 0xDB, 0xCA, 0x21, 0xD6, 0xC1, 0x20, 0x91, 0x29, 0xDA, 0xE8, 0xC0, 0x63, 0x6D, 0x2E, 
	0x11, 0xD8, 0x9D, 0xB0, 0xFA, 0xC0, 0x6D, 0x2D, 0xB4, 0xC0, 0x3A, 0x14, 0xDC, 0x21, 0x7E, 0xC0, 
	0x6D, 0x2C, 0x93, 0x29, 0xDA, 0xBB, 0xB0, 0x53, 0x6D, 0x2B, 0xFF, 0xCA, 0x87, 0xD1, 0x29, 0x61, 
	0xDB, 0x48, 0x5D, 0xD0, 0x49, 0x28, 0x74, 0xD8, 0x97, 0xD4, 0x27, 0x07, 0xC0, 0x14, 0xDC, 0x22, 
	0x6F, 0xD1, 0x35, 0x26, 0xFF, 0xCA, 0x22, 0x0C, 0xD0, 0x6D, 0x24, 0x42, 0x02, 0xF4, 0xD6, 0x86, 
	0x94, 0x10, 0x6D, 0x23, 0xA5, 0xB0, 0xF2, 0xD8, 0x87, 0xD1, 0x27, 0xDB, 0x22, 0x78, 0x9A, 0xD1, 
	0x3A, 0xD9, 0x8C, 0xC5, 0x4D, 0x25, 0x71, 0x6E, 0x52, 0xC1, 0x8A, 0xF2, 0xD8, 0x4E, 0x4F, 0xE0, 
	0x2E, 0xD6, 0xB0, 0xF2, 0xD8, 0x1D, 0xC0, 0x6E, 0x55, 0x2D, 0xC9, 0xCA, 0x23, 0xC4, 0xC0, 0x6E, 
	0x8B, 0xDB, 0x23, 0xD6, 0xC0, 0x31, 0x6E, 0x9E, 0xD9, 0x98, 0xB4, 0x7E, 0xC0, 0x6E, 0x29, 0x66, 
	0xEA, 0x61, 0xE1, 0x09, 0x28, 0x28, 0xDB, 0x0C, 0xD0, 0x6E, 0xDA, 0xCB, 0xB1, 0xE1, 0xEC, 0xCB, 
	0xB1, 0xE1, 0x60, 0xFE, 0xCB, 0xB1, 0xE1, 0x10, 0xD9, 0x9D, 0xB0, 0xAF, 0xC0, 0x6E, 0x22, 0xC4, 
	0xDB, 0xA6, 0x4E, 0xC0, 0x6E, 0x21, 0xD7, 0xD8, 0xAF, 0xE3, 0x20, 0xC9, 0xCA, 0x21, 0x9F, 0x4C, 
	0x25, 0x71, 0x6F, 0x2F, 0x8C, 0xDB, 0x4E, 0xE0, 0x6F, 0x99, 0x2E, 0xEF, 0xDB, 0xD6, 0xC0, 0x6F, 
	0x2C, 0x02, 0xEB, 0xE8, 0xC0, 0x6F, 0x99, 0x2B, 0x15, 0xEB, 0xBF, 0xD0, 0x6F, 0x2A, 0x28, 0xDA, 
	0x7E, 0xC0, 0x6F, 0x68, 0x4F, 0xB1, 0xF2, 0xD8, 0x57, 0xF1, 0x28, 0xED, 0xED, 0x6F, 0x27, 0xFF, 
	0xCA, 0xD5, 0x23, 0x90, 0xF1, 0x25, 0x02, 0xEA, 0x23, 0xFA, 0xC0, 0x6F, 0x24, 0x4A, 0x15, 0xEA, 
	0x23, 0xB5, 0xF1, 0x23, 0x28, 0xDA, 0xFF, 0xF2, 0x22, 0xDB, 0xCA, 0xEA, 0xFF, 0xF2, 0x21, 0x4E, 
	0xDA, 0x23, 0x4E, 0xC0, 0x61, 0x2F, 0x2F, 0xAC, 0xF2, 0xFB, 0x57, 0xF0, 0x61, 0x2F, 0x65, 0xEB, 
	0x23, 0x4E, 0xE0, 0x61, 0x73, 0x2F, 0x2D, 0xBA, 0xFA, 0xFF, 0xF1, 0x61, 0x2F, 0x2C, 0x3E, 0x0B, 
	0x6E, 0x61, 0xE0, 0x61, 0x2F, 0x2A, 0xF2, 0xFE, 0x61, 0x2F, 0xB5, 0xEB, 0x5D, 0x24, 0xD6, 0xC0, 
	0x61, 0x2F, 0x28, 0xBA, 0xFA, 0x24, 0xBB, 0xB0, 0xD7, 0x61, 0x2F, 0x27, 0x4E, 0xDA, 0x24, 0xBF, 
	0xD0, 0x61, 0x2F, 0x9C, 0xF1, 0xFB, 0x56, 0xF1, 0x61, 0x2F, 0x24, 0x66, 0xEB, 0xE8, 0xC0, 0x61, 
	0x73, 0x2F, 0x23, 0xCA, 0x0B, 0xEC, 0xB0, 0x61, 0x2F, 0x22, 0xDE, 0x0B, 0x4E, 0xAF, 0xC0, 0x61, 
	0x2F, 0x20, 0x02, 0xE8, 0x98, 0xB4, 0x23, 0xD8, 0x01, 0xB3, 0x2E, 0x2F, 0xDB, 0xCA, 0x73, 0xE1, 
	0x61, 0x2E, 0x6E, 0xFB, 0x23, 0x62, 0xEC, 0x01, 0x2E, 0x82, 0xC1, 0x7C, 0xE8, 0x28, 0x11, 0x2E, 
	0x2B, 0x37, 0xFA, 0x2D, 0x23, 0x3C, 0x11, 0x2E, 0x2A, 0x3B, 0xEF, 0x23, 0x69, 0x12, 0xC3, 0xDB, 
	0x92, 0xBD, 0x13, 0x27, 0x18, 0x0B, 0x69, 0x12, 0x26, 0x8C, 0xEB, 0x69, 0x12, 0x24, 0xD4, 0xC0, 
	0xB0, 0xC7, 0x1D, 0x23, 0x74, 0xDF, 0x24, 0x14, 0x11, 0x2E, 0x22, 0x5A, 0xED, 0xCA, 0x24, 0x88, 
	0x01, 0x2E, 0x20, 0x61, 0xDA, 0x24, 0x0C, 0xD0, 0xD7, 0x61, 0x2D, 0x2F, 0x15, 0xEA, 0x24, 0xA0, 
	0xB0, 0x61, 0x2D, 0x38, 0x68, 0xC1, 0xB9, 0x08, 0x7E, 0xC0, 0x61, 0x2D, 0x2D, 0xED, 0xC8, 0x98, 
	0xB4, 0x92, 0xD8, 0x01, 0x2D, 0x94, 0xFB, 0xEC, 0x01, 0x2D, 0xA7, 0xFB, 0x28, 0x11, 0x2D, 0x99, 
	0x29, 0xDB, 0xCA, 0x3C, 0x11, 0x2D, 0x28, 0x4E, 0xDA, 0x4C, 0x01, 0x2D, 0xF5, 0x26, 0x11, 0xDD, 
	0x43, 0x24, 0x31, 0x60, 0xBD, 0x40, 0xD0, 0xFF, 0x75, 0x21, 0xA0, 0x41, 0x60, 0x8F, 0xD2, 0x75, 
	0xFF, 0x74, 0x61, 0x2D, 0x23, 0x95, 0xD1, 0x71, 0xA9, 0x7F, 0x71, 0xD0, 0x06, 0x21, 0x20, 0x46, 
	0xB3, 0xDF, 0x81, 0xFF, 0x72, 0xC0, 0xA2, 0x41, 0xD1, 0x71, 0xC0, 0xA4, 0xDF, 0x21, 0x20, 0x41, 
	0xD0, 0x70, 0x10, 0x30, 0x41, 0x71, 0xFF, 0xE0, 0x71, 0x30, 0x60, 0x8F, 0x71, 0xE0, 0x40, 0xFE, 
	0xF4, 0x20, 0xBF, 0x40, 0xD0, 0x72, 0x30, 0xC0, 0xA7, 0xDF, 0x72, 0x30, 0x81, 0x72, 0xE0, 0x1F, 
	0x30, 0xA4, 0x40, 0xED, 0xB1, 0x3C, 0xB0, 0x20, 0x41, 0x43, 0x30, 0x20, 0x20, 0x00, 0xFF, 0x0D, 
	0x0B, 0x0A, 0x08, 0x02, 0x09, 0x01, 0x05, 0xFF, 0x0C, 0x0E, 0x03, 0x07, 0x04, 0x0F, 0x06, 0x0B, 
	0xFF, 0x0C, 0x08, 0x09, 0x0F, 0x04, 0x03, 0x00, 0x0D, 0x7F, 0x01, 0x0A, 0x0E, 0x06, 0x02, 0x07, 
	0x05, 0x62, 0x30, 0xFF, 0x0B, 0x06, 0x09, 0x00, 0x01, 0x0A, 0x05, 0x0E, 0xFF, 0x0D, 0x0C, 0x02, 
	0x08, 0x07, 0x0F, 0x05, 0x03, 0xFF, 0x0B, 0x04, 0x0A, 0x09, 0x01, 0x0C, 0x06, 0x0E, 0xFF, 0x08, 
	0x00, 0x07, 0x02, 0x0D, 0x02, 0x04, 0x0A, 0xFF, 0x0F, 0x0C, 0x01, 0x0E, 0x0B, 0x00, 0x09, 0x03, 
	0xFF, 0x08, 0x0D, 0x07, 0x06, 0x05, 0x04, 0x03, 0x05, 0xFB, 0x0A, 0x06, 0x95, 0x30, 0x0E, 0x02, 
	0x0C, 0x0F, 0x0D, 0xFF, 0x07, 0x08, 0x01, 0x05, 0x0A, 0x03, 0x02, 0x07, 0xFF, 0x04, 0x0D, 0x0E, 
	0x01, 0x08, 0x00, 0x0F, 0x0C, 0xFF, 0x09, 0x06, 0x0B, 0x05, 0x0C, 0x09, 0x03, 0x01, 0xFF, 0x0D, 
	0x07, 0x04, 0x06, 0x08, 0x0B, 0x02, 0x0A, 0xFF, 0x00, 0x0F, 0x0E, 0x05, 0x0D, 0x09, 0x01, 0x08, 
	0xFF, 0x06, 0x04, 0x03, 0x0A, 0x07, 0x02, 0x0E, 0x00, 0xFF, 0x0B, 0x0C, 0x0F, 0x71, 0x51, 0x25, 
	0xFA, 0x31, 0x5F, 0xD1, 0x32, 0xF6, 0x71, 0x54, 0xE0, 0x34, 0x57, 0xE0, 0x34, 0xF5, 0x5A, 0xE0, 
	0x34, 0x5D, 0xE0, 0x33, 0x70, 0x72, 0x69, 0x6E, 0xFF, 0x74, 0x66, 0x00, 0x69, 0x63, 0x67, 0x5F, 
	0x72, 0xFF, 0x75, 0x6E, 0x5F, 0x69, 0x6E, 0x5F, 0x74, 0x68, 0xFF, 0x72, 0x65, 0x61, 0x64, 0x73, 
	0x00, 0x6D, 0x61, 0xFF, 0x6C, 0x6C, 0x6F, 0x63, 0x00, 0x66, 0x72, 0x65, 0xFF, 0x65, 0x00, 0x65, 
	0x78, 0x69, 0x74, 0x00, 0x73, 0xFF, 0x6E, 0x69, 0x70, 0x70, 0x65, 0x74, 0x5F, 0x64, 0xFF, 0x65, 
	0x69, 0x6E, 0x69, 0x74, 0x69, 0x61, 0x6C, 0xC7, 0x69, 0x7A, 0x65, 0x30, 0x46, 0x3B, 0x4F, 0x17, 
	0x44, 0x5F, 0x63, 0x57, 0x6F, 0x75, 0x6E, 0x37, 0x41, 0x63, 0x45, 0x47, 0x64, 0x6D, 0x4A, 0xFF, 
	0x65, 0x78, 0x65, 0x63, 0x75, 0x74, 0x65, 0x5F, 0xBF, 0x61, 0x63, 0x74, 0x69, 0x6F, 0x6E, 0x30, 
	0x48, 0x63, 0x7F, 0x72, 0x79, 0x70, 0x74, 0x00, 0x20, 0x20, 0x82, 0x90, 0xFF, 0x71, 0x21, 0x29, 
	0x9D, 0x20, 0x21, 0x28, 0x09, 0xFF, 0x20, 0x69, 0x6D, 0x70, 0x6F, 0x72, 0x74, 0x20, 0xB7, 0x66, 
	0x75, 0x6E, 0x92, 0x42, 0x20, 0x63, 0x21, 0x40, 0x20, 0xFF, 0x65, 0x72, 0x72, 0x6F, 0x72, 0x0A, 
	0x00, 0x60, 0xFF, 0xBB, 0x40, 0xD4, 0x77, 0x30, 0x40, 0xF9, 0x24, 0xEF, 0xA6, 0x40, 0xD0, 0x78, 
	0xD7, 0x40, 0x23, 0xA4, 0x74, 0xFF, 0x43, 0xF8, 0xD3, 0x70, 0x43, 0xF8, 0x78, 0x31, 0xFF, 0xFA, 
	0xD1, 0x73, 0x77, 0x31, 0xFA, 0xD2, 0x71, 0xFF, 0x30, 0x72, 0x30, 0x76, 0x2E, 0x9B, 0xC0, 0x21, 
	0x5F, 0xA0, 0x71, 0x32, 0x72, 0xE2, 0xE8, 0x43, 0x31, 0xEF, 0x41, 0xD7, 0xE1, 0x70, 0x81, 0xDD, 
	0x40, 0x70, 0xAE, 0x70, 0x00, 0x74, 0xFD, 0x81, 0xD5, 0x40, 0x74, 0xF9, 0xA2, 0x64, 0x0A, 0xB5, 
	0xEE, 0xF4, 0x20, 0xBB, 0x40, 0xD3, 0xD6, 0x42, 0xA1, 0x68, 0x47, 0xF7, 0x21, 0xFB, 0xD4, 0xDC, 
	0x43, 0x22, 0xAA, 0x73, 0x43, 0xD7, 0xF8, 0xD2, 0x72, 0xF0, 0x40, 0xD1, 0xE8, 0x43, 0x30, 0x71, 
	0xFF, 0x30, 0x76, 0x29, 0x95, 0xC0, 0xA9, 0x74, 0x71, 0x73, 0xE2, 0x76, 0x40, 0x51, 0x0D, 0x57, 
	0x0A, 0x73, 0x81, 0x28, 0x50, 0xDF, 0x73, 0xF9, 0xA2, 0x63, 0x04, 0x22, 0x50, 0x71, 0x21, 0xFF, 
	0x30, 0x72, 0xE0, 0x71, 0x21, 0x31, 0x72, 0xE1, 0xFF, 0x71, 0x22, 0x34, 0x73, 0xE0, 0x71, 0x22, 
	0x35, 0x5B, 0x73, 0xE1, 0x71, 0x50, 0x22, 0x54, 0x71, 0x51, 0x50, 0xF4, 0x20, 0xD5, 0xBC, 0xF8, 
	0x20, 0x22, 0xE7, 0x30, 0xD1, 0xD7, 0x40, 0x21, 0xA2, 0xD5, 0x75, 0xE8, 0x40, 0x71, 0x0B, 0x53, 
	0x71, 0x12, 0x51, 0x61, 0x0E, 0xDE, 0xF8, 0x21, 0x50, 0x25, 0xFA, 0xD2, 0x37, 0x51, 0xA2, 0x61, 
	0x7E, 0xE6, 0xA1, 0xD3, 0x40, 0xD1, 0x75, 0x71, 0x72, 0x0B, 0x50, 0xF5, 0x73, 0xCA, 0x51, 0xE2, 
	0xAA, 0x51, 0x43, 0xF2, 0xD1, 0x72, 0xF6, 0xAE, 0x52, 0x06, 0xB4, 0xF4, 0x21, 0x75, 0x76, 0x24, 
	0xFB, 0x7F, 0x43, 0x24, 0xF6, 0xC0, 0x22, 0xAE, 0x24, 0xE7, 0xA0, 0xFD, 0x30, 0x9A, 0x50, 0x47, 
	0x24, 0xF1, 0xD2, 0x06, 0x75, 0xBF, 0x51, 0xD5, 0x76, 0x51, 0xD6, 0x75, 0xF6, 0x50, 0xD0, 0xF7, 
	0x76, 0x30, 0xF4, 0xFB, 0x40, 0x70, 0x71, 0xF4, 0x70, 0x9F, 0x23, 0xF2, 0x24, 0xF6, 0x72, 0xED, 
	0x50, 0x01, 0x80, 0x0F, 0xFB, 0x75, 0xF1, 0x07, 0x61, 0xF1, 0xF4, 0x25, 0xFA, 0xC0, 0xFF, 0xAA, 
	0x70, 0xA9, 0x75, 0x81, 0xD5, 0x76, 0x81, 0xAF, 0xD6, 0x61, 0x0A, 0xF0, 0x10, 0x30, 0x01, 0x3A, 
	0x60, 0x71, 0xFF, 0x28, 0x9D, 0xA6, 0x21, 0x23, 0x29, 0xFA, 0x22, 0xFB, 0xF0, 0x00, 0x4A, 0x31, 
	0x20, 0x60, 0xB0, 0x4A, 0xD6, 0xFF, 0x21, 0x72, 0x21, 0x71, 0x28, 0x9A, 0x41, 0xF2, 0xFF, 0xD7, 
	0x21, 0x72, 0xD8, 0x10, 0x21, 0x71, 0x23, 0xFF, 0x93, 0x7B, 0x22, 0xC2, 0xC0, 0x21, 0xAB, 0x7B, 
	0xFF, 0x60, 0xCE, 0xC0, 0x21, 0xAA, 0x7B, 0x60, 0xCD, 0xFE, 0x6C, 0x60, 0x21, 0x71, 0x2E, 0x91, 
	0x23, 0x2E, 0x4D, 0x9F, 0xF0, 0xE0, 0x60, 0x4F, 0x21, 0x8C, 0x71, 0x80, 0x61, 0x60, 0xF5, 0x4E, 
	0x85, 0x62, 0x4D, 0x80, 0x61, 0x20, 0x71, 0x21, 0x9D, 0xDF, 0xAE, 0x24, 0xF2, 0x21, 0x32, 0xF6, 
	0x51, 0xAB, 0x72, 0xDB, 0x42, 0x9E, 0x8D, 0x61, 0x72, 0xEB, 0xA6, 0x65, 0x75, 0xF6, 0xFB, 0x42, 
	0xD2, 0x9A, 0x65, 0xA4, 0x40, 0x45, 0x95, 0xD2, 0xF3, 0x72, 0xC1, 0xB2, 0x61, 0x4B, 0x61, 0xBF, 
	0x68, 0xFC, 0xD0, 0xF1, 0x70, 0x30, 0x50, 0x0B, 0x60, 0x49, 0x31, 0x11, 0x00, 0x20, 0x05, 0xBF, 
	0x60, 0xBF, 0x73, 0x81, 0xD2, 0x73, 0xEC, 0x50, 0x21, 0xFF, 0xA4, 0x73, 0x25, 0xFA, 0x81, 0xD3, 
	0xF1, 0xA7, 0x7E, 0xE7, 0x61, 0xA8, 0x60, 0x02, 0x73, 0x72, 0xFC, 0x43, 0x30, 0xFD, 0x24, 0x82, 
	0x90, 0xD4, 0x03, 0x73, 0x51, 0xD3, 0x73, 0xB6, 0x06, 0x61, 0x74, 0x30, 0x12, 0x63, 0x74, 0x31, 
	0x19, 0x62, 0x61, 0xF7, 0x07, 0x70, 0x2F, 0x29, 0x90, 0x21, 0xAE, 0x70, 0x2F, 0xF7, 0x2F, 0x20, 
	0x40, 0xE9, 0x60, 0xA0, 0x70, 0x74, 0x32, 0xEF, 0x24, 0xF6, 0xA5, 0x80, 0x07, 0x70, 0x0D, 0x73, 
	0x83, 0xFF, 0xD3, 0x09, 0x80, 0x73, 0x82, 0xD3, 0x04, 0x80, 0xE8, 0x66, 0x50, 0xFB, 0x63, 0x3A, 
	0x61, 0x80, 0x52, 0x70, 0x00, 0x00, 0xFF, 0xAF, 0x00, 0x71, 0x22, 0x56, 0x49, 0x62, 0x00, 0x62, 
	0x70, 0xD0, 0xA9, 0x13, 0x62, 0x71, 0x68, 0x73, 0xD7, 0x67, 0x78, 0xEA, 0x67, 0x78, 0xF1, 0xAA, 
	0x67, 0x78, 0xF6, 0x67, 0x78, 0x05, 0x62, 0x70, 0x04, 0x62, 0x70, 0xFB, 0x7E, 0x67, 0x74, 0xFB, 
	0x12, 0x00, 0x00, 0x10, 0x14, 0x68, 0x73, 0xA5, 0xEF, 0xB3, 0x70, 0x23, 0xB7, 0x74, 0x62, 0x71, 
	0x3C, 0xB7, 0x74, 0x03, 0xAA, 0x62, 0x70, 0x4B, 0xB7, 0x74, 0x1A, 0x62, 0x70, 0x62, 0xB7, 0x74, 
	0xC0, 0xAA, 0xB3, 0x70, 0x06, 0x62, 0x70, 0x48, 0x9B, 0x78, 0x11, 0x62, 0x70, 0x1D, 0xAA, 0x62, 
	0x70, 0x29, 0x62, 0x70, 0x35, 0x62, 0x70, 0x44, 0x62, 0x70, 0x49, 0xAA, 0x62, 0x70, 0x51, 0x62, 
	0x70, 0x55, 0x62, 0x70, 0x5D, 0x62, 0x70, 0x61, 0xAA, 0x62, 0x70, 0x69, 0x62, 0x70, 0x6D, 0x62, 
	0x70, 0x75, 0x62, 0x70, 0x79, 0xAA, 0x62, 0x70, 0x81, 0x62, 0x70, 0x85, 0x62, 0x70, 0x8D, 0x62, 
	0x70, 0x94, 0xFE, 0x1F, 0x81, 0x15, 0x00, 0x00, 0x2E, 0x2E, 0x2F, 0x67, 0xFF, 0x65, 0x6E, 0x65, 
	0x72, 0x61, 0x74, 0x6F, 0x72, 0x7D, 0x2F, 0x6E, 0x40, 0x2E, 0x64, 0x62, 0x67, 0x00
};

