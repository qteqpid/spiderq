#include "bloomfilter.h"
#include "hashs.h"
#include "md5.h"
#include "crc32.h"
#include "sha1.h"
#include <stdlib.h>

#define HASH_FUNC_NUM 8
#define BLOOM_SIZE 1000000
#define BITSIZE_PER_BLOOM  32
#define LIMIT   (BLOOM_SIZE * BITSIZE_PER_BLOOM)

/* 
 * m=10n, k=8 when e=0.01 (m is bitsize, n is inputnum, k is hash_func num, e is error rate)
 * here m = BLOOM_SIZE*BITSIZE_PER_BLOOM = 32,000,000 (bits)
 * so n = m/10 = 3,200,000 (urls)
 * enough for crawling a website
 */
static int bloom_table[BLOOM_SIZE] = {0};

static MD5_CTX md5;
static SHA1_CONTEXT sha;  
static unsigned int encrypt(char *key, unsigned int id)
{
	unsigned int val = 0;

	switch(id){
		case 0:
			val = times33(key); break;
		case 1:
			val = times31(key); break;
		case 2:
			val = aphash(key); break;
		case 3:
			val = hash16777619(key); break;
		case 4:
			val = mysqlhash(key); break;
		case 5:
			int i;
			unsigned char decrypt[16];
			MD5Init(&md5);
			MD5Update(&md5, (unsigned char *)key, strlen(key));
			MD5Final(&md5, decrypt);
			for(i = 0; i < 16; i++)
				val = (val << 5) + val + decrypt[i];
			break;
		case 6:
			val = crc32((unsigned char *)key, strlen(key));
			break;	
		case 7:
    			sha1_init(&sha);  
		    	sha1_write(&sha, (unsigned char *)key, strlen(key));
			sha1_final(&sha);
    			for (i=0; i < 20; i++)  
				val = (val << 5) + val + sha.buf[i];
			break;
		default:
			// should not be here
			abort();
	}
	return val;
}

int search(char *url)
{
    unsigned int h, i, index, pos;
    int res = 0;

    for (i = 0; i < HASH_FUNC_NUM; i++) {
        h = encrypt(url, i);
        h %= LIMIT;
        index = h / BITSIZE_PER_BLOOM;
        pos = h % BITSIZE_PER_BLOOM;
        if (bloom_table[index] & (0x80000000 >> pos))
            res++;
        else
            bloom_table[index] |= (0x80000000 >> pos);
    } 
    return (res == HASH_FUNC_NUM);
}
