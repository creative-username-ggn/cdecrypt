/*
	CDecrypt - Decrypt Wii U NUS content files [https://code.google.com/p/cdecrypt/]

	Copyright (c) 2013-2015 crediar
	Copyright (c) 2021-2021 creative_username

	CDecrypt is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 3 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#define _CRT_SECURE_NO_WARNINGS


unsigned char WiiUCommonDevKey[16] =
{
    0x2F, 0x5C, 0x1B, 0x29, 0x44, 0xE7, 0xFD, 0x6F, 0xC3, 0x97, 0x96, 0x4B, 0x05, 0x76, 0x91, 0xFA, 
};
unsigned char WiiUCommonKey[16] =
{
    0xD7, 0xB0, 0x04, 0x02, 0x65, 0x9B, 0xA2, 0xAB, 0xD2, 0xCB, 0x0D, 0xB2, 0x7F, 0xA2, 0xB6, 0x56, 
};

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <openssl/aes.h>
#include <openssl/sha.h>
#include <time.h>
#include <vector>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/types.h>

#pragma comment(lib,"libeay32.lib")

uint32_t OPENSSL_ia32cap_P[4] = {0};
AES_KEY key;
uint8_t enc_title_key[16];
uint8_t dec_title_key[16];
uint8_t title_id[16];
uint8_t dkey[16];

uint64_t H0Count = 0;
uint64_t H0Fail  = 0;

#pragma pack(1)

enum ContentType
{
	CONTENT_REQUIRED=	(1<< 0),	// not sure
	CONTENT_SHARED	=	(1<<15),
	CONTENT_OPTIONAL=	(1<<14),
};

typedef struct
{
	uint16_t IndexOffset;	//	0	 0x204
	uint16_t CommandCount;	//	2	 0x206
	uint8_t	SHA2[32];			//  12 0x208
} ContentInfo;

typedef struct
{
	uint32_t ID;					//	0	 0xB04
	uint16_t Index;			//	4  0xB08
	uint16_t Type;				//	6	 0xB0A
	uint64_t Size;				//	8	 0xB0C
	uint8_t	SHA2[32];		//  16 0xB14
} Content;

typedef struct
{
	uint32_t SignatureType;		// 0x000
	uint8_t	Signature[0x100];	// 0x004

	uint8_t	Padding0[0x3C];		// 0x104
	uint8_t	Issuer[0x40];			// 0x140

	uint8_t	Version;					// 0x180
	uint8_t	CACRLVersion;			// 0x181
	uint8_t	SignerCRLVersion;	// 0x182
	uint8_t	Padding1;					// 0x183

	uint64_t	SystemVersion;		// 0x184
	uint64_t	TitleID;					// 0x18C 
	uint32_t	TitleType;				// 0x194 
	uint16_t	GroupID;					// 0x198 
	uint8_t	Reserved[62];			// 0x19A 
	uint32_t	AccessRights;			// 0x1D8
	uint16_t	TitleVersion;			// 0x1DC 
	uint16_t	ContentCount;			// 0x1DE 
	uint16_t BootIndex;				// 0x1E0
	uint8_t	Padding3[2];			// 0x1E2 
	uint8_t	SHA2[32];					// 0x1E4
	
	ContentInfo ContentInfos[64];

	Content Contents[];		// 0x1E4 

} TitleMetaData;

struct FSTInfo
{
	uint32_t Unknown;
	uint32_t Size;
	uint32_t UnknownB;
	uint32_t UnknownC[6];
};
struct FST
{
	uint32_t MagicBytes;
	uint32_t Unknown;
	uint32_t EntryCount;

	uint32_t UnknownB[5];
	
	FSTInfo FSTInfos[];
};

struct FEntry
{
	union
	{
		struct
		{
			uint32_t Type				:8;
			uint32_t NameOffset	:24;
		};
		uint32_t TypeName;
	};
	union
	{
		struct		// File Entry
		{
			uint32_t FileOffset;
			uint32_t FileLength;
		};
		struct		// Dir Entry
		{
			uint32_t ParentOffset;
			uint32_t NextOffset;
		};
		uint32_t entry[2];
	};
	unsigned short Flags;
	unsigned short ContentID;
};

#define bs16(s) (uint16_t)( ((s)>>8) | ((s)<<8) )
#define bs32(s) (uint32_t)( (((s)&0xFF0000)>>8) | (((s)&0xFF00)<<8) | ((s)>>24) | ((s)<<24) )

uint32_t bs24( uint32_t i )
{
	return ((i&0xFF0000)>>16) | ((i&0xFF)<<16) | (i&0x00FF00);
}
uint64_t bs64( uint64_t i )
{
	return ((uint64_t)(bs32(i&0xFFFFFFFF))<<32) | (bs32(i>>32));
}
char *ReadFile( FILE *in, uint32_t *Length )
{
	if( in == NULL )
	{
		//perror("");
		return NULL;
	}

	fseek( in, 0, SEEK_END );
	*Length = ftell(in);
	
	fseek( in, 0, 0 );

	char *Data = new char[*Length];

	uint32_t read = fread( Data, 1, *Length, in );

	fclose( in );

	return Data;
}
void FileDump( const char *Name, void *Data, uint32_t Length )
{
	if( Data == NULL )
	{
		printf("zero ptr");
		return;
	}
	if( Length == 0 )
	{
		printf("zero sz");
		return;
	}
	FILE *Out = fopen( Name, "wb" );
	if( Out == NULL )
	{
		perror("");
		return;
	}

	if( fwrite( Data, 1, Length, Out ) != Length )
  {
		perror("");
  }

	fclose( Out );
}
static char ascii(char s)
{
  if(s < 0x20) return '.';
  if(s > 0x7E) return '.';
  return s;
}
void hexdump(void *d, int32_t len)
{
  uint8_t *data;
  int32_t i, off;
  data = (uint8_t*)d;
  for (off=0; off<len; off += 16)
  {
    printf("%08x  ",off);
    for(i=0; i<16; i++)
      if((i+off)>=len)
		  printf("   ");
      else
		  printf("%02x ",data[off+i]);

    printf(" ");
    for(i=0; i<16; i++)
      if((i+off)>=len) printf(" ");
      else printf("%c",ascii(data[off+i]));
    printf("\n");
  }
}
#define	BLOCK_SIZE	0x10000
void ExtractFileHash( FILE *in, uint64_t PartDataOffset, uint64_t FileOffset, uint64_t Size, char *FileName, uint16_t ContentID )
{
	char encdata[BLOCK_SIZE];
	char decdata[BLOCK_SIZE];
	uint8_t IV[16];
	uint8_t hash[SHA_DIGEST_LENGTH];
	uint8_t H0[SHA_DIGEST_LENGTH];
	uint8_t Hashes[0x400];

	uint64_t Wrote			= 0;
	uint64_t WriteSize = 0xFC00;	// Hash block size
	uint64_t Block			= (FileOffset / 0xFC00) & 0xF;
		
	FILE *out = fopen( FileName, "wb" );
	if( out == NULL )
	{
		printf("Could not create \"%s\"\n", FileName );
		perror("");
		exit(0);
	}

	uint64_t roffset = FileOffset / 0xFC00 * BLOCK_SIZE;
	uint64_t soffset = FileOffset - (FileOffset / 0xFC00 * 0xFC00);

	if( soffset+Size > WriteSize )
		WriteSize = WriteSize - soffset;

	fseeko( in, PartDataOffset+roffset, SEEK_SET );
	while(Size > 0)
	{
		if( WriteSize > Size )
			WriteSize = Size;

		fread( encdata, sizeof( char ), BLOCK_SIZE, in);
		
		memset( IV, 0, sizeof(IV) );
		IV[1] = (uint8_t)ContentID;
		AES_cbc_encrypt( (const uint8_t *)(encdata), (uint8_t *)Hashes, 0x400, &key, IV, AES_DECRYPT );		
		
		memcpy( H0, Hashes+0x14*Block, SHA_DIGEST_LENGTH );

		memcpy( IV, Hashes+0x14*Block, sizeof(IV) );
		if( Block == 0 )
			IV[1] ^= ContentID;
		AES_cbc_encrypt( (const uint8_t *)(encdata+0x400), (uint8_t *)decdata, 0xFC00, &key, IV, AES_DECRYPT );

		SHA1( (const uint8_t *)decdata, 0xFC00, hash );
		if( Block == 0 )
			hash[1] ^= ContentID;
		H0Count++;
		if( memcmp( hash, H0, SHA_DIGEST_LENGTH ) != 0 )
		{
			H0Fail++;
			hexdump( hash, SHA_DIGEST_LENGTH );
			hexdump( Hashes, 0x100 );
			hexdump( decdata, 0x100 );
			printf("Failed to verify H0 hash\n");
			exit(0);
		}

		Size -= fwrite( decdata+soffset, sizeof( char ), WriteSize, out);

		Wrote+=WriteSize;

		Block++;
		if( Block >= 16 )
				Block = 0;

		if( soffset )
		{
			WriteSize = 0xFC00;
			soffset = 0;
		}
	}
	
	fclose( out );
}
#undef BLOCK_SIZE
#define	BLOCK_SIZE	0x8000
void ExtractFile( FILE *in, uint64_t PartDataOffset, uint64_t FileOffset, uint64_t Size, char *FileName, uint16_t ContentID )
{
	char encdata[BLOCK_SIZE];
	char decdata[BLOCK_SIZE];
	uint64_t Wrote=0;
	uint64_t Block			= (FileOffset / BLOCK_SIZE) & 0xF;

	//printf("PO:%08llX FO:%08llX FS:%llu\n", PartDataOffset, FileOffset, Size );

	//calc real offset
	uint64_t roffset = FileOffset / BLOCK_SIZE * BLOCK_SIZE;
	uint64_t soffset = FileOffset - (FileOffset / BLOCK_SIZE * BLOCK_SIZE);
	//printf("Extracting:\"%s\" RealOffset:%08llX RealOffset:%08llX\n", FileName, roffset, soffset );
	
	FILE *out = fopen( FileName, "wb" );
	if( out == NULL )
	{
		printf("Could not create \"%s\"\n", FileName );
		perror("");
		exit(0);
	}
	uint8_t IV[16];
	memset( IV, 0, sizeof(IV) );
	IV[1] = (uint8_t)ContentID;

	uint64_t WriteSize = BLOCK_SIZE;

	if( soffset+Size > WriteSize )
		WriteSize = WriteSize - soffset;

	fseeko( in, PartDataOffset+roffset, SEEK_SET );
	
	while(Size > 0)
	{
		if( WriteSize > Size )
				WriteSize = Size;

		fread( encdata, sizeof( char ), BLOCK_SIZE, in);
		
		AES_cbc_encrypt( (const uint8_t *)(encdata), (uint8_t *)decdata, BLOCK_SIZE, &key, IV, AES_DECRYPT);

		Size -= fwrite( decdata+soffset, sizeof( char ), WriteSize, out);

		Wrote+=WriteSize;

		if( soffset )
		{
			WriteSize = BLOCK_SIZE;
			soffset = 0;
		}
	}
	
	fclose( out );
}

FILE* OpenApp (uint32_t i)
{
	char str[1024];
	FILE *f;

	//upper case with app extension
	sprintf( str, "%08X.app", i );
	f = fopen(str, "rb");
	if (f != NULL) return f;

	//lower case with app extension
	sprintf( str, "%08x.app", i );
	f = fopen(str, "rb");
	if (f != NULL) return f;

	//upper case without app extension
	sprintf( str, "%08X", i );
	f = fopen(str, "rb");
	if (f != NULL) return f;

	//lower case without app extension
	sprintf( str, "%08x", i );
	f = fopen(str, "rb");
	if (f != NULL) return f;

	return NULL;
}

int32_t main( int32_t argc, char*argv[])
{
	
	printf("CDecrypt v 2.0b by crediar\n");
	printf("CDecrypt modified for linux/macos by creative username\n");
	printf("Built: %s %s\n", __TIME__, __DATE__ );

	if( argc != 3 )
	{
		printf("Usage:\n");
		printf(" ./cdecrypt title.tmd title.tik\n\n");
		return EXIT_SUCCESS;
	}

	uint32_t TMDLen;
	char *TMD = ReadFile( fopen(argv[1], "rb"), &TMDLen );
	if( TMD == nullptr )
	{
		perror("Failed to open tmd\n");
		return EXIT_FAILURE;
	}
	
	uint32_t TIKLen;
	char *TIK = ReadFile( fopen(argv[2], "rb"), &TIKLen );
	if( TIK == nullptr )
	{
		perror("Failed to open cetk\n");
		return EXIT_FAILURE;
	}

	TitleMetaData *tmd = (TitleMetaData*)TMD;

	if( tmd->Version != 1 )
	{
		printf("Unsupported TMD Version:%u\n", tmd->Version );
		return EXIT_FAILURE;
	}
	
	printf("Title version:%u\n", bs16(tmd->TitleVersion) );
	printf("Content Count:%u\n", bs16(tmd->ContentCount) );

	if( strcmp( TMD+0x140, "Root-CA00000003-CP0000000b" ) == 0 )
	{
		AES_set_decrypt_key( (const uint8_t*)WiiUCommonKey, sizeof(WiiUCommonKey)*8, &key );
	}
	else if( strcmp( TMD+0x140, "Root-CA00000004-CP00000010" ) == 0 )
	{
		AES_set_decrypt_key( (const uint8_t*)WiiUCommonDevKey, sizeof(WiiUCommonDevKey)*8, &key );
	}
	else
	{
		printf("Unknown Root type:\"%s\"\n", TMD+0x140 );
		return EXIT_FAILURE;
	}	

	memset( title_id, 0, sizeof(title_id) );
	
	memcpy( title_id, TMD + 0x18C, 8 );
	memcpy( enc_title_key, TIK + 0x1BF, 16 );
	
	AES_cbc_encrypt(enc_title_key, dec_title_key, sizeof(dec_title_key), &key, title_id, AES_DECRYPT);
	AES_set_decrypt_key( dec_title_key, sizeof(dec_title_key)*8, &key);
		
	char iv[16];
	memset( iv, 0, sizeof(iv) );

	char *CNT;
	uint32_t CNTLen;
	{
		uint32_t id = bs32(tmd->Contents[0].ID);
		FILE *f = OpenApp(id);
		if (f == NULL) {
			printf("Failed to open content:%02X\n", id );
			return EXIT_FAILURE;
		}

		CNT = ReadFile(f, &CNTLen);
	}

	if( bs64(tmd->Contents[0].Size) != (uint64_t)CNTLen )
	{
		printf("Size of content:%u is wrong: %u:%llu\n", bs32(tmd->Contents[0].ID), CNTLen, static_cast<unsigned long long>(bs64(tmd->Contents[0].Size)) );
		return EXIT_FAILURE;
	}

	AES_cbc_encrypt( (const uint8_t *)(CNT), (uint8_t *)(CNT), CNTLen, &key, (uint8_t*)(iv), AES_DECRYPT );	

	if( bs32(*(uint32_t*)CNT) != 0x46535400 )
	{
		char str[1024];
		sprintf( str, "%08X.dec", bs32(tmd->Contents[0].ID) );
		FileDump( str, CNT, CNTLen );
		return EXIT_FAILURE;
	}
	
	FST *_fst = (FST*)(CNT);

	printf("FSTInfo Entries:%u\n", bs32(_fst->EntryCount) );
	if( bs32(_fst->EntryCount) > 90000 )
	{
		return EXIT_FAILURE;
	}
	
	FEntry *fe = (FEntry*)(CNT+0x20+bs32(_fst->EntryCount)*0x20);
	
	uint32_t Entries = bs32(*(uint32_t*)(CNT+0x20+bs32(_fst->EntryCount)*0x20+8));
	uint32_t NameOff = 0x20 + bs32(_fst->EntryCount) * 0x20 + Entries * 0x10;
	uint32_t DirEntries = 0;
	
	printf("FST entries:%u\n", Entries );

	char *Path = new char[1024];
	int32_t Entry[16];
	int32_t LEntry[16];
	
	int32_t level=0;

	for( uint32_t i=1; i < Entries; ++i )
	{
		if( level )
		{
			while( LEntry[level-1] == i )
			{
				//printf("[%03X]leaving :\"%s\" Level:%d\n", i, CNT + NameOff + bs24( fe[Entry[level-1]].NameOffset ), level );
				level--;
			}
		}

		if( fe[i].Type & 1 )
		{
			Entry[level] = i;
			LEntry[level++] = bs32( fe[i].NextOffset );
			if( level > 15 )	// something is wrong!
			{
				printf("level error:%u\n", level );
				break;
			}
		}
		else
		{
			memset( Path, 0, 1024 );

			for( int32_t j=0; j<level; ++j )
			{
				if(j)
					Path[strlen(Path)] = '/';
				memcpy( Path+strlen(Path), CNT + NameOff + bs24( fe[Entry[j]].NameOffset), strlen(CNT + NameOff + bs24( fe[Entry[j]].NameOffset) ) );
				printf("making dir: %s\n", Path);
				mkdir(Path, 0777);
			}
			if(level)
				Path[strlen(Path)] = '/';
			memcpy( Path+strlen(Path), CNT + NameOff + bs24( fe[i].NameOffset ), strlen(CNT + NameOff + bs24( fe[i].NameOffset )) );

			uint32_t CNTSize = bs32(fe[i].FileLength);
			uint64_t CNTOff  = ((uint64_t)bs32(fe[i].FileOffset));

			if( (bs16(fe[i].Flags) & 4) == 0 )
			{
				CNTOff <<= 5;
			}
			
			printf("Size:%07X Offset:0x%llu CID:%02X U:%02X %s\n", CNTSize, static_cast<unsigned long long>(CNTOff), bs16(fe[i].ContentID), bs16(fe[i].Flags), Path );

			uint32_t ContFileID = bs32(tmd->Contents[bs16(fe[i].ContentID)].ID);
			

			if(!(fe[i].Type & 0x80))
			{

				FILE *cnt = OpenApp(ContFileID);
				if( cnt == NULL )
				{
					printf("Could not open:\"%08x\"\n", ContFileID );			
					perror("");
					return EXIT_FAILURE;
				}

				if( (bs16(fe[i].Flags) & 0x440) )
				{
					ExtractFileHash( cnt, 0, CNTOff, bs32(fe[i].FileLength), Path, bs16(fe[i].ContentID) );
				}
				else
				{
					ExtractFile( cnt, 0, CNTOff, bs32(fe[i].FileLength), Path, bs16(fe[i].ContentID) );
				}
				fclose(cnt);
			}
		}
	}
	return EXIT_SUCCESS;
}
