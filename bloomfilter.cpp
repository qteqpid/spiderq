#include "bloomfilter.h"

#define SALT_NUM 8
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

static const char * salt[SALT_NUM] = {"Dm", "VB", "ui", "LK", "uj", "RD", "we", "fc"};

static unsigned int encrypt(const char *key, const char * s)
{
    unsigned int val = 0;
    char * str = crypt(key, s);
    int len = strlen(str);
    while(len--) {
        val = ((val << 5) + val) + str[len]; /* times 33 */
    }
    return val;
}

int search(const char *url)
{
    unsigned int h, i, index, pos;
    int res = 0;

    for (i = 0; i < SALT_NUM; i++) {
        h = encrypt(url, salt[i]);
        h %= LIMIT;
        index = h / BITSIZE_PER_BLOOM;
        pos = h % BITSIZE_PER_BLOOM;
        if (bloom_table[index] & (0x80000000 >> pos))
            res++;
        else
            bloom_table[index] |= (0x80000000 >> pos);
    } 
    return (res == SALT_NUM);
}
