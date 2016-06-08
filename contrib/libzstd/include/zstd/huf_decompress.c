/* ******************************************************************
   Huffman decoder, part of New Generation Entropy library
   Copyright (C) 2013-2016, Yann Collet.

   BSD 2-Clause License (http://www.opensource.org/licenses/bsd-license.php)

   Redistribution and use in source and binary forms, with or without
   modification, are permitted provided that the following conditions are
   met:

       * Redistributions of source code must retain the above copyright
   notice, this list of conditions and the following disclaimer.
       * Redistributions in binary form must reproduce the above
   copyright notice, this list of conditions and the following disclaimer
   in the documentation and/or other materials provided with the
   distribution.

   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

    You can contact the author at :
    - FSE+HUF source repository : https://github.com/Cyan4973/FiniteStateEntropy
    - Public forum : https://groups.google.com/forum/#!forum/lz4c
****************************************************************** */

/* **************************************************************
*  Compiler specifics
****************************************************************/
#if defined (__cplusplus) || (defined (__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L) /* C99 */)
/* inline is defined */
#elif defined(_MSC_VER)
#  define inline __inline
#else
#  define inline /* disable inline */
#endif


#ifdef _MSC_VER    /* Visual Studio */
#  define FORCE_INLINE static __forceinline
#  pragma warning(disable : 4127)        /* disable: C4127: conditional expression is constant */
#else
#  ifdef __GNUC__
#    define FORCE_INLINE static inline __attribute__((always_inline))
#  else
#    define FORCE_INLINE static inline
#  endif
#endif


/* **************************************************************
*  Includes
****************************************************************/
#include <stdlib.h>     /* malloc, free, qsort */
#include <string.h>     /* memcpy, memset */
#include <stdio.h>      /* printf (debug) */
#include "huf_static.h"
#include "bitstream.h"
#include "fse.h"        /* header compression */



/* **************************************************************
*  Error Management
****************************************************************/
#define HUF_STATIC_ASSERT(c) { enum { HUF_static_assert = 1/(int)(!!(c)) }; }   /* use only *after* variable declarations */



/* *******************************************************
*  HUF : Huffman block decompression
*********************************************************/
typedef struct { BYTE byte; BYTE nbBits; } HUF_DEltX2;   /* single-symbol decoding */

typedef struct { U16 sequence; BYTE nbBits; BYTE length; } HUF_DEltX4;  /* double-symbols decoding */

typedef struct { BYTE symbol; BYTE weight; } sortedSymbol_t;



/*-***************************/
/*  single-symbol decoding   */
/*-***************************/

size_t HUF_readDTableX2 (U16* DTable, const void* src, size_t srcSize)
{
    BYTE huffWeight[HUF_MAX_SYMBOL_VALUE + 1];
    U32 rankVal[HUF_ABSOLUTEMAX_TABLELOG + 1];   /* large enough for values from 0 to 16 */
    U32 tableLog = 0;
    size_t iSize;
    U32 nbSymbols = 0;
    U32 n;
    U32 nextRankStart;
    void* const dtPtr = DTable + 1;
    HUF_DEltX2* const dt = (HUF_DEltX2*)dtPtr;

    HUF_STATIC_ASSERT(sizeof(HUF_DEltX2) == sizeof(U16));   /* if compilation fails here, assertion is false */
    //memset(huffWeight, 0, sizeof(huffWeight));   /* is not necessary, even though some analyzer complain ... */

    iSize = HUF_readStats(huffWeight, HUF_MAX_SYMBOL_VALUE + 1, rankVal, &nbSymbols, &tableLog, src, srcSize);
    if (HUF_isError(iSize)) return iSize;

    /* check result */
    if (tableLog > DTable[0]) return ERROR(tableLog_tooLarge);   /* DTable is too small */
    DTable[0] = (U16)tableLog;   /* maybe should separate sizeof allocated DTable, from used size of DTable, in case of re-use */

    /* Prepare ranks */
    nextRankStart = 0;
    for (n=1; n<tableLog+1; n++) {
        U32 current = nextRankStart;
        nextRankStart += (rankVal[n] << (n-1));
        rankVal[n] = current;
    }

    /* fill DTable */
    for (n=0; n<nbSymbols; n++) {
        const U32 w = huffWeight[n];
        const U32 length = (1 << w) >> 1;
        U32 i;
        HUF_DEltX2 D;
        D.byte = (BYTE)n; D.nbBits = (BYTE)(tableLog + 1 - w);
        for (i = rankVal[w]; i < rankVal[w] + length; i++)
            dt[i] = D;
        rankVal[w] += length;
    }

    return iSize;
}


static BYTE HUF_decodeSymbolX2(BIT_DStream_t* Dstream, const HUF_DEltX2* dt, const U32 dtLog)
{
    const size_t val = BIT_lookBitsFast(Dstream, dtLog); /* note : dtLog >= 1 */
    const BYTE c = dt[val].byte;
    BIT_skipBits(Dstream, dt[val].nbBits);
    return c;
}

#define HUF_DECODE_SYMBOLX2_0(ptr, DStreamPtr) \
    *ptr++ = HUF_decodeSymbolX2(DStreamPtr, dt, dtLog)

#define HUF_DECODE_SYMBOLX2_1(ptr, DStreamPtr) \
    if (MEM_64bits() || (HUF_MAX_TABLELOG<=12)) \
        HUF_DECODE_SYMBOLX2_0(ptr, DStreamPtr)

#define HUF_DECODE_SYMBOLX2_2(ptr, DStreamPtr) \
    if (MEM_64bits()) \
        HUF_DECODE_SYMBOLX2_0(ptr, DStreamPtr)

static inline size_t HUF_decodeStreamX2(BYTE* p, BIT_DStream_t* const bitDPtr, BYTE* const pEnd, const HUF_DEltX2* const dt, const U32 dtLog)
{
    BYTE* const pStart = p;

    /* up to 4 symbols at a time */
    while ((BIT_reloadDStream(bitDPtr) == BIT_DStream_unfinished) && (p <= pEnd-4)) {
        HUF_DECODE_SYMBOLX2_2(p, bitDPtr);
        HUF_DECODE_SYMBOLX2_1(p, bitDPtr);
        HUF_DECODE_SYMBOLX2_2(p, bitDPtr);
        HUF_DECODE_SYMBOLX2_0(p, bitDPtr);
    }

    /* closer to the end */
    while ((BIT_reloadDStream(bitDPtr) == BIT_DStream_unfinished) && (p < pEnd))
        HUF_DECODE_SYMBOLX2_0(p, bitDPtr);

    /* no more data to retrieve from bitstream, hence no need to reload */
    while (p < pEnd)
        HUF_DECODE_SYMBOLX2_0(p, bitDPtr);

    return pEnd-pStart;
}

size_t HUF_decompress1X2_usingDTable(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const U16* DTable)
{
    BYTE* op = (BYTE*)dst;
    BYTE* const oend = op + dstSize;
    const U32 dtLog = DTable[0];
    const void* dtPtr = DTable;
    const HUF_DEltX2* const dt = ((const HUF_DEltX2*)dtPtr)+1;
    BIT_DStream_t bitD;

    { size_t const errorCode = BIT_initDStream(&bitD, cSrc, cSrcSize);
      if (HUF_isError(errorCode)) return errorCode; }

    HUF_decodeStreamX2(op, &bitD, oend, dt, dtLog);

    /* check */
    if (!BIT_endOfDStream(&bitD)) return ERROR(corruption_detected);

    return dstSize;
}

size_t HUF_decompress1X2 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    HUF_CREATE_STATIC_DTABLEX2(DTable, HUF_MAX_TABLELOG);
    const BYTE* ip = (const BYTE*) cSrc;

    size_t const errorCode = HUF_readDTableX2 (DTable, cSrc, cSrcSize);
    if (HUF_isError(errorCode)) return errorCode;
    if (errorCode >= cSrcSize) return ERROR(srcSize_wrong);
    ip += errorCode;
    cSrcSize -= errorCode;

    return HUF_decompress1X2_usingDTable (dst, dstSize, ip, cSrcSize, DTable);
}


size_t HUF_decompress4X2_usingDTable(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const U16* DTable)
{
    /* Check */
    if (cSrcSize < 10) return ERROR(corruption_detected);  /* strict minimum : jump table + 1 byte per stream */

    {   const BYTE* const istart = (const BYTE*) cSrc;
        BYTE* const ostart = (BYTE*) dst;
        BYTE* const oend = ostart + dstSize;
        const void* const dtPtr = DTable;
        const HUF_DEltX2* const dt = ((const HUF_DEltX2*)dtPtr) +1;
        const U32 dtLog = DTable[0];
        size_t errorCode;

        /* Init */
        BIT_DStream_t bitD1;
        BIT_DStream_t bitD2;
        BIT_DStream_t bitD3;
        BIT_DStream_t bitD4;
        const size_t length1 = MEM_readLE16(istart);
        const size_t length2 = MEM_readLE16(istart+2);
        const size_t length3 = MEM_readLE16(istart+4);
        size_t length4;
        const BYTE* const istart1 = istart + 6;  /* jumpTable */
        const BYTE* const istart2 = istart1 + length1;
        const BYTE* const istart3 = istart2 + length2;
        const BYTE* const istart4 = istart3 + length3;
        const size_t segmentSize = (dstSize+3) / 4;
        BYTE* const opStart2 = ostart + segmentSize;
        BYTE* const opStart3 = opStart2 + segmentSize;
        BYTE* const opStart4 = opStart3 + segmentSize;
        BYTE* op1 = ostart;
        BYTE* op2 = opStart2;
        BYTE* op3 = opStart3;
        BYTE* op4 = opStart4;
        U32 endSignal;

        length4 = cSrcSize - (length1 + length2 + length3 + 6);
        if (length4 > cSrcSize) return ERROR(corruption_detected);   /* overflow */
        errorCode = BIT_initDStream(&bitD1, istart1, length1);
        if (HUF_isError(errorCode)) return errorCode;
        errorCode = BIT_initDStream(&bitD2, istart2, length2);
        if (HUF_isError(errorCode)) return errorCode;
        errorCode = BIT_initDStream(&bitD3, istart3, length3);
        if (HUF_isError(errorCode)) return errorCode;
        errorCode = BIT_initDStream(&bitD4, istart4, length4);
        if (HUF_isError(errorCode)) return errorCode;

        /* 16-32 symbols per loop (4-8 symbols per stream) */
        endSignal = BIT_reloadDStream(&bitD1) | BIT_reloadDStream(&bitD2) | BIT_reloadDStream(&bitD3) | BIT_reloadDStream(&bitD4);
        for ( ; (endSignal==BIT_DStream_unfinished) && (op4<(oend-7)) ; ) {
            HUF_DECODE_SYMBOLX2_2(op1, &bitD1);
            HUF_DECODE_SYMBOLX2_2(op2, &bitD2);
            HUF_DECODE_SYMBOLX2_2(op3, &bitD3);
            HUF_DECODE_SYMBOLX2_2(op4, &bitD4);
            HUF_DECODE_SYMBOLX2_1(op1, &bitD1);
            HUF_DECODE_SYMBOLX2_1(op2, &bitD2);
            HUF_DECODE_SYMBOLX2_1(op3, &bitD3);
            HUF_DECODE_SYMBOLX2_1(op4, &bitD4);
            HUF_DECODE_SYMBOLX2_2(op1, &bitD1);
            HUF_DECODE_SYMBOLX2_2(op2, &bitD2);
            HUF_DECODE_SYMBOLX2_2(op3, &bitD3);
            HUF_DECODE_SYMBOLX2_2(op4, &bitD4);
            HUF_DECODE_SYMBOLX2_0(op1, &bitD1);
            HUF_DECODE_SYMBOLX2_0(op2, &bitD2);
            HUF_DECODE_SYMBOLX2_0(op3, &bitD3);
            HUF_DECODE_SYMBOLX2_0(op4, &bitD4);
            endSignal = BIT_reloadDStream(&bitD1) | BIT_reloadDStream(&bitD2) | BIT_reloadDStream(&bitD3) | BIT_reloadDStream(&bitD4);
        }

        /* check corruption */
        if (op1 > opStart2) return ERROR(corruption_detected);
        if (op2 > opStart3) return ERROR(corruption_detected);
        if (op3 > opStart4) return ERROR(corruption_detected);
        /* note : op4 supposed already verified within main loop */

        /* finish bitStreams one by one */
        HUF_decodeStreamX2(op1, &bitD1, opStart2, dt, dtLog);
        HUF_decodeStreamX2(op2, &bitD2, opStart3, dt, dtLog);
        HUF_decodeStreamX2(op3, &bitD3, opStart4, dt, dtLog);
        HUF_decodeStreamX2(op4, &bitD4, oend,     dt, dtLog);

        /* check */
        endSignal = BIT_endOfDStream(&bitD1) & BIT_endOfDStream(&bitD2) & BIT_endOfDStream(&bitD3) & BIT_endOfDStream(&bitD4);
        if (!endSignal) return ERROR(corruption_detected);

        /* decoded size */
        return dstSize;
    }
}


size_t HUF_decompress4X2 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    HUF_CREATE_STATIC_DTABLEX2(DTable, HUF_MAX_TABLELOG);
    const BYTE* ip = (const BYTE*) cSrc;

    size_t const errorCode = HUF_readDTableX2 (DTable, cSrc, cSrcSize);
    if (HUF_isError(errorCode)) return errorCode;
    if (errorCode >= cSrcSize) return ERROR(srcSize_wrong);
    ip += errorCode;
    cSrcSize -= errorCode;

    return HUF_decompress4X2_usingDTable (dst, dstSize, ip, cSrcSize, DTable);
}


/* *************************/
/* double-symbols decoding */
/* *************************/

static void HUF_fillDTableX4Level2(HUF_DEltX4* DTable, U32 sizeLog, const U32 consumed,
                           const U32* rankValOrigin, const int minWeight,
                           const sortedSymbol_t* sortedSymbols, const U32 sortedListSize,
                           U32 nbBitsBaseline, U16 baseSeq)
{
    HUF_DEltX4 DElt;
    U32 rankVal[HUF_ABSOLUTEMAX_TABLELOG + 1];

    /* get pre-calculated rankVal */
    memcpy(rankVal, rankValOrigin, sizeof(rankVal));

    /* fill skipped values */
    if (minWeight>1) {
        U32 i, skipSize = rankVal[minWeight];
        MEM_writeLE16(&(DElt.sequence), baseSeq);
        DElt.nbBits   = (BYTE)(consumed);
        DElt.length   = 1;
        for (i = 0; i < skipSize; i++)
            DTable[i] = DElt;
    }

    /* fill DTable */
    { U32 s; for (s=0; s<sortedListSize; s++) {   /* note : sortedSymbols already skipped */
        const U32 symbol = sortedSymbols[s].symbol;
        const U32 weight = sortedSymbols[s].weight;
        const U32 nbBits = nbBitsBaseline - weight;
        const U32 length = 1 << (sizeLog-nbBits);
        const U32 start = rankVal[weight];
        U32 i = start;
        const U32 end = start + length;

        MEM_writeLE16(&(DElt.sequence), (U16)(baseSeq + (symbol << 8)));
        DElt.nbBits = (BYTE)(nbBits + consumed);
        DElt.length = 2;
        do { DTable[i++] = DElt; } while (i<end);   /* since length >= 1 */

        rankVal[weight] += length;
    }}
}

typedef U32 rankVal_t[HUF_ABSOLUTEMAX_TABLELOG][HUF_ABSOLUTEMAX_TABLELOG + 1];

static void HUF_fillDTableX4(HUF_DEltX4* DTable, const U32 targetLog,
                           const sortedSymbol_t* sortedList, const U32 sortedListSize,
                           const U32* rankStart, rankVal_t rankValOrigin, const U32 maxWeight,
                           const U32 nbBitsBaseline)
{
    U32 rankVal[HUF_ABSOLUTEMAX_TABLELOG + 1];
    const int scaleLog = nbBitsBaseline - targetLog;   /* note : targetLog >= srcLog, hence scaleLog <= 1 */
    const U32 minBits  = nbBitsBaseline - maxWeight;
    U32 s;

    memcpy(rankVal, rankValOrigin, sizeof(rankVal));

    /* fill DTable */
    for (s=0; s<sortedListSize; s++) {
        const U16 symbol = sortedList[s].symbol;
        const U32 weight = sortedList[s].weight;
        const U32 nbBits = nbBitsBaseline - weight;
        const U32 start = rankVal[weight];
        const U32 length = 1 << (targetLog-nbBits);

        if (targetLog-nbBits >= minBits) {   /* enough room for a second symbol */
            U32 sortedRank;
            int minWeight = nbBits + scaleLog;
            if (minWeight < 1) minWeight = 1;
            sortedRank = rankStart[minWeight];
            HUF_fillDTableX4Level2(DTable+start, targetLog-nbBits, nbBits,
                           rankValOrigin[nbBits], minWeight,
                           sortedList+sortedRank, sortedListSize-sortedRank,
                           nbBitsBaseline, symbol);
        } else {
            HUF_DEltX4 DElt;
            MEM_writeLE16(&(DElt.sequence), symbol);
            DElt.nbBits = (BYTE)(nbBits);
            DElt.length = 1;
            {   U32 u;
                const U32 end = start + length;
                for (u = start; u < end; u++) DTable[u] = DElt;
        }   }
        rankVal[weight] += length;
    }
}

size_t HUF_readDTableX4 (U32* DTable, const void* src, size_t srcSize)
{
    BYTE weightList[HUF_MAX_SYMBOL_VALUE + 1];
    sortedSymbol_t sortedSymbol[HUF_MAX_SYMBOL_VALUE + 1];
    U32 rankStats[HUF_ABSOLUTEMAX_TABLELOG + 1] = { 0 };
    U32 rankStart0[HUF_ABSOLUTEMAX_TABLELOG + 2] = { 0 };
    U32* const rankStart = rankStart0+1;
    rankVal_t rankVal;
    U32 tableLog, maxW, sizeOfSort, nbSymbols;
    const U32 memLog = DTable[0];
    size_t iSize;
    void* dtPtr = DTable;
    HUF_DEltX4* const dt = ((HUF_DEltX4*)dtPtr) + 1;

    HUF_STATIC_ASSERT(sizeof(HUF_DEltX4) == sizeof(U32));   /* if compilation fails here, assertion is false */
    if (memLog > HUF_ABSOLUTEMAX_TABLELOG) return ERROR(tableLog_tooLarge);
    //memset(weightList, 0, sizeof(weightList));   /* is not necessary, even though some analyzer complain ... */

    iSize = HUF_readStats(weightList, HUF_MAX_SYMBOL_VALUE + 1, rankStats, &nbSymbols, &tableLog, src, srcSize);
    if (HUF_isError(iSize)) return iSize;

    /* check result */
    if (tableLog > memLog) return ERROR(tableLog_tooLarge);   /* DTable can't fit code depth */

    /* find maxWeight */
    for (maxW = tableLog; rankStats[maxW]==0; maxW--) {}  /* necessarily finds a solution before 0 */

    /* Get start index of each weight */
    {   U32 w, nextRankStart = 0;
        for (w=1; w<maxW+1; w++) {
            U32 current = nextRankStart;
            nextRankStart += rankStats[w];
            rankStart[w] = current;
        }
        rankStart[0] = nextRankStart;   /* put all 0w symbols at the end of sorted list*/
        sizeOfSort = nextRankStart;
    }

    /* sort symbols by weight */
    {   U32 s;
        for (s=0; s<nbSymbols; s++) {
            U32 const w = weightList[s];
            U32 const r = rankStart[w]++;
            sortedSymbol[r].symbol = (BYTE)s;
            sortedSymbol[r].weight = (BYTE)w;
        }
        rankStart[0] = 0;   /* forget 0w symbols; this is beginning of weight(1) */
    }

    /* Build rankVal */
    {   U32* const rankVal0 = rankVal[0];
        {   int const rescale = (memLog-tableLog) - 1;   /* tableLog <= memLog */
            U32 nextRankVal = 0;
            U32 w;
            for (w=1; w<maxW+1; w++) {
                U32 current = nextRankVal;
                nextRankVal += rankStats[w] << (w+rescale);
                rankVal0[w] = current;
        }   }
        {   U32 const minBits = tableLog+1 - maxW;
            U32 consumed;
            for (consumed = minBits; consumed < memLog - minBits + 1; consumed++) {
                U32* const rankValPtr = rankVal[consumed];
                U32 w;
                for (w = 1; w < maxW+1; w++) {
                    rankValPtr[w] = rankVal0[w] >> consumed;
    }   }   }   }

    HUF_fillDTableX4(dt, memLog,
                   sortedSymbol, sizeOfSort,
                   rankStart0, rankVal, maxW,
                   tableLog+1);

    return iSize;
}


static U32 HUF_decodeSymbolX4(void* op, BIT_DStream_t* DStream, const HUF_DEltX4* dt, const U32 dtLog)
{
    const size_t val = BIT_lookBitsFast(DStream, dtLog);   /* note : dtLog >= 1 */
    memcpy(op, dt+val, 2);
    BIT_skipBits(DStream, dt[val].nbBits);
    return dt[val].length;
}

static U32 HUF_decodeLastSymbolX4(void* op, BIT_DStream_t* DStream, const HUF_DEltX4* dt, const U32 dtLog)
{
    const size_t val = BIT_lookBitsFast(DStream, dtLog);   /* note : dtLog >= 1 */
    memcpy(op, dt+val, 1);
    if (dt[val].length==1) BIT_skipBits(DStream, dt[val].nbBits);
    else {
        if (DStream->bitsConsumed < (sizeof(DStream->bitContainer)*8)) {
            BIT_skipBits(DStream, dt[val].nbBits);
            if (DStream->bitsConsumed > (sizeof(DStream->bitContainer)*8))
                DStream->bitsConsumed = (sizeof(DStream->bitContainer)*8);   /* ugly hack; works only because it's the last symbol. Note : can't easily extract nbBits from just this symbol */
    }   }
    return 1;
}


#define HUF_DECODE_SYMBOLX4_0(ptr, DStreamPtr) \
    ptr += HUF_decodeSymbolX4(ptr, DStreamPtr, dt, dtLog)

#define HUF_DECODE_SYMBOLX4_1(ptr, DStreamPtr) \
    if (MEM_64bits() || (HUF_MAX_TABLELOG<=12)) \
        ptr += HUF_decodeSymbolX4(ptr, DStreamPtr, dt, dtLog)

#define HUF_DECODE_SYMBOLX4_2(ptr, DStreamPtr) \
    if (MEM_64bits()) \
        ptr += HUF_decodeSymbolX4(ptr, DStreamPtr, dt, dtLog)

static inline size_t HUF_decodeStreamX4(BYTE* p, BIT_DStream_t* bitDPtr, BYTE* const pEnd, const HUF_DEltX4* const dt, const U32 dtLog)
{
    BYTE* const pStart = p;

    /* up to 8 symbols at a time */
    while ((BIT_reloadDStream(bitDPtr) == BIT_DStream_unfinished) && (p < pEnd-7)) {
        HUF_DECODE_SYMBOLX4_2(p, bitDPtr);
        HUF_DECODE_SYMBOLX4_1(p, bitDPtr);
        HUF_DECODE_SYMBOLX4_2(p, bitDPtr);
        HUF_DECODE_SYMBOLX4_0(p, bitDPtr);
    }

    /* closer to the end */
    while ((BIT_reloadDStream(bitDPtr) == BIT_DStream_unfinished) && (p <= pEnd-2))
        HUF_DECODE_SYMBOLX4_0(p, bitDPtr);

    while (p <= pEnd-2)
        HUF_DECODE_SYMBOLX4_0(p, bitDPtr);   /* no need to reload : reached the end of DStream */

    if (p < pEnd)
        p += HUF_decodeLastSymbolX4(p, bitDPtr, dt, dtLog);

    return p-pStart;
}


size_t HUF_decompress1X4_usingDTable(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const U32* DTable)
{
    const BYTE* const istart = (const BYTE*) cSrc;
    BYTE* const ostart = (BYTE*) dst;
    BYTE* const oend = ostart + dstSize;

    const U32 dtLog = DTable[0];
    const void* const dtPtr = DTable;
    const HUF_DEltX4* const dt = ((const HUF_DEltX4*)dtPtr) +1;

    /* Init */
    BIT_DStream_t bitD;
    { size_t const errorCode = BIT_initDStream(&bitD, istart, cSrcSize);
      if (HUF_isError(errorCode)) return errorCode; }

    /* decode */
    HUF_decodeStreamX4(ostart, &bitD, oend, dt, dtLog);

    /* check */
    if (!BIT_endOfDStream(&bitD)) return ERROR(corruption_detected);

    /* decoded size */
    return dstSize;
}

size_t HUF_decompress1X4 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    HUF_CREATE_STATIC_DTABLEX4(DTable, HUF_MAX_TABLELOG);
    const BYTE* ip = (const BYTE*) cSrc;

    size_t const hSize = HUF_readDTableX4 (DTable, cSrc, cSrcSize);
    if (HUF_isError(hSize)) return hSize;
    if (hSize >= cSrcSize) return ERROR(srcSize_wrong);
    ip += hSize;
    cSrcSize -= hSize;

    return HUF_decompress1X4_usingDTable (dst, dstSize, ip, cSrcSize, DTable);
}

size_t HUF_decompress4X4_usingDTable(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const U32* DTable)
{
    if (cSrcSize < 10) return ERROR(corruption_detected);   /* strict minimum : jump table + 1 byte per stream */

    {   const BYTE* const istart = (const BYTE*) cSrc;
        BYTE* const ostart = (BYTE*) dst;
        BYTE* const oend = ostart + dstSize;
        const void* const dtPtr = DTable;
        const HUF_DEltX4* const dt = ((const HUF_DEltX4*)dtPtr) +1;
        const U32 dtLog = DTable[0];
        size_t errorCode;

        /* Init */
        BIT_DStream_t bitD1;
        BIT_DStream_t bitD2;
        BIT_DStream_t bitD3;
        BIT_DStream_t bitD4;
        const size_t length1 = MEM_readLE16(istart);
        const size_t length2 = MEM_readLE16(istart+2);
        const size_t length3 = MEM_readLE16(istart+4);
        size_t length4;
        const BYTE* const istart1 = istart + 6;  /* jumpTable */
        const BYTE* const istart2 = istart1 + length1;
        const BYTE* const istart3 = istart2 + length2;
        const BYTE* const istart4 = istart3 + length3;
        const size_t segmentSize = (dstSize+3) / 4;
        BYTE* const opStart2 = ostart + segmentSize;
        BYTE* const opStart3 = opStart2 + segmentSize;
        BYTE* const opStart4 = opStart3 + segmentSize;
        BYTE* op1 = ostart;
        BYTE* op2 = opStart2;
        BYTE* op3 = opStart3;
        BYTE* op4 = opStart4;
        U32 endSignal;

        length4 = cSrcSize - (length1 + length2 + length3 + 6);
        if (length4 > cSrcSize) return ERROR(corruption_detected);   /* overflow */
        errorCode = BIT_initDStream(&bitD1, istart1, length1);
        if (HUF_isError(errorCode)) return errorCode;
        errorCode = BIT_initDStream(&bitD2, istart2, length2);
        if (HUF_isError(errorCode)) return errorCode;
        errorCode = BIT_initDStream(&bitD3, istart3, length3);
        if (HUF_isError(errorCode)) return errorCode;
        errorCode = BIT_initDStream(&bitD4, istart4, length4);
        if (HUF_isError(errorCode)) return errorCode;

        /* 16-32 symbols per loop (4-8 symbols per stream) */
        endSignal = BIT_reloadDStream(&bitD1) | BIT_reloadDStream(&bitD2) | BIT_reloadDStream(&bitD3) | BIT_reloadDStream(&bitD4);
        for ( ; (endSignal==BIT_DStream_unfinished) && (op4<(oend-7)) ; ) {
            HUF_DECODE_SYMBOLX4_2(op1, &bitD1);
            HUF_DECODE_SYMBOLX4_2(op2, &bitD2);
            HUF_DECODE_SYMBOLX4_2(op3, &bitD3);
            HUF_DECODE_SYMBOLX4_2(op4, &bitD4);
            HUF_DECODE_SYMBOLX4_1(op1, &bitD1);
            HUF_DECODE_SYMBOLX4_1(op2, &bitD2);
            HUF_DECODE_SYMBOLX4_1(op3, &bitD3);
            HUF_DECODE_SYMBOLX4_1(op4, &bitD4);
            HUF_DECODE_SYMBOLX4_2(op1, &bitD1);
            HUF_DECODE_SYMBOLX4_2(op2, &bitD2);
            HUF_DECODE_SYMBOLX4_2(op3, &bitD3);
            HUF_DECODE_SYMBOLX4_2(op4, &bitD4);
            HUF_DECODE_SYMBOLX4_0(op1, &bitD1);
            HUF_DECODE_SYMBOLX4_0(op2, &bitD2);
            HUF_DECODE_SYMBOLX4_0(op3, &bitD3);
            HUF_DECODE_SYMBOLX4_0(op4, &bitD4);

            endSignal = BIT_reloadDStream(&bitD1) | BIT_reloadDStream(&bitD2) | BIT_reloadDStream(&bitD3) | BIT_reloadDStream(&bitD4);
        }

        /* check corruption */
        if (op1 > opStart2) return ERROR(corruption_detected);
        if (op2 > opStart3) return ERROR(corruption_detected);
        if (op3 > opStart4) return ERROR(corruption_detected);
        /* note : op4 supposed already verified within main loop */

        /* finish bitStreams one by one */
        HUF_decodeStreamX4(op1, &bitD1, opStart2, dt, dtLog);
        HUF_decodeStreamX4(op2, &bitD2, opStart3, dt, dtLog);
        HUF_decodeStreamX4(op3, &bitD3, opStart4, dt, dtLog);
        HUF_decodeStreamX4(op4, &bitD4, oend,     dt, dtLog);

        /* check */
        endSignal = BIT_endOfDStream(&bitD1) & BIT_endOfDStream(&bitD2) & BIT_endOfDStream(&bitD3) & BIT_endOfDStream(&bitD4);
        if (!endSignal) return ERROR(corruption_detected);

        /* decoded size */
        return dstSize;
    }
}


size_t HUF_decompress4X4 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    HUF_CREATE_STATIC_DTABLEX4(DTable, HUF_MAX_TABLELOG);
    const BYTE* ip = (const BYTE*) cSrc;

    size_t hSize = HUF_readDTableX4 (DTable, cSrc, cSrcSize);
    if (HUF_isError(hSize)) return hSize;
    if (hSize >= cSrcSize) return ERROR(srcSize_wrong);
    ip += hSize;
    cSrcSize -= hSize;

    return HUF_decompress4X4_usingDTable (dst, dstSize, ip, cSrcSize, DTable);
}


/* ********************************/
/* quad-symbol decoding           */
/* ********************************/
typedef struct { BYTE nbBits; BYTE nbBytes; } HUF_DDescX6;
typedef union { BYTE byte[4]; U32 sequence; } HUF_DSeqX6;

/* recursive, up to level 3; may benefit from <template>-like strategy to nest each level inline */
static void HUF_fillDTableX6LevelN(HUF_DDescX6* DDescription, HUF_DSeqX6* DSequence, int sizeLog,
                           const rankVal_t rankValOrigin, const U32 consumed, const int minWeight, const U32 maxWeight,
                           const sortedSymbol_t* sortedSymbols, const U32 sortedListSize, const U32* rankStart,
                           const U32 nbBitsBaseline, HUF_DSeqX6 baseSeq, HUF_DDescX6 DDesc)
{
    const int scaleLog = nbBitsBaseline - sizeLog;   /* note : targetLog >= (nbBitsBaseline-1), hence scaleLog <= 1 */
    const int minBits  = nbBitsBaseline - maxWeight;
    const U32 level = DDesc.nbBytes;
    U32 rankVal[HUF_ABSOLUTEMAX_TABLELOG + 1];
    U32 symbolStartPos, s;

    /* local rankVal, will be modified */
    memcpy(rankVal, rankValOrigin[consumed], sizeof(rankVal));

    /* fill skipped values */
    if (minWeight>1) {
        U32 i;
        const U32 skipSize = rankVal[minWeight];
        for (i = 0; i < skipSize; i++) {
            DSequence[i] = baseSeq;
            DDescription[i] = DDesc;
    }   }

    /* fill DTable */
    DDesc.nbBytes++;
    symbolStartPos = rankStart[minWeight];
    for (s=symbolStartPos; s<sortedListSize; s++) {
        const BYTE symbol = sortedSymbols[s].symbol;
        const U32  weight = sortedSymbols[s].weight;   /* >= 1 (sorted) */
        const int  nbBits = nbBitsBaseline - weight;   /* >= 1 (by construction) */
        const int  totalBits = consumed+nbBits;
        const U32  start  = rankVal[weight];
        const U32  length = 1 << (sizeLog-nbBits);
        baseSeq.byte[level] = symbol;
        DDesc.nbBits = (BYTE)totalBits;

        if ((level<3) && (sizeLog-totalBits >= minBits)) {  /* enough room for another symbol */
            int nextMinWeight = totalBits + scaleLog;
            if (nextMinWeight < 1) nextMinWeight = 1;
            HUF_fillDTableX6LevelN(DDescription+start, DSequence+start, sizeLog-nbBits,
                           rankValOrigin, totalBits, nextMinWeight, maxWeight,
                           sortedSymbols, sortedListSize, rankStart,
                           nbBitsBaseline, baseSeq, DDesc);   /* recursive (max : level 3) */
        } else {
            U32 i;
            const U32 end = start + length;
            for (i = start; i < end; i++) {
                DDescription[i] = DDesc;
                DSequence[i] = baseSeq;
        }   }
        rankVal[weight] += length;
    }
}


/* note : same preparation as X4 */
size_t HUF_readDTableX6 (U32* DTable, const void* src, size_t srcSize)
{
    BYTE weightList[HUF_MAX_SYMBOL_VALUE + 1];
    sortedSymbol_t sortedSymbol[HUF_MAX_SYMBOL_VALUE + 1];
    U32 rankStats[HUF_ABSOLUTEMAX_TABLELOG + 1] = { 0 };
    U32 rankStart0[HUF_ABSOLUTEMAX_TABLELOG + 2] = { 0 };
    U32* const rankStart = rankStart0+1;
    U32 tableLog, maxW, sizeOfSort, nbSymbols;
    rankVal_t rankVal;
    const U32 memLog = DTable[0];
    size_t iSize;

    if (memLog > HUF_ABSOLUTEMAX_TABLELOG) return ERROR(tableLog_tooLarge);
    //memset(weightList, 0, sizeof(weightList));   /* is not necessary, even though some analyzer complain ... */

    iSize = HUF_readStats(weightList, HUF_MAX_SYMBOL_VALUE + 1, rankStats, &nbSymbols, &tableLog, src, srcSize);
    if (HUF_isError(iSize)) return iSize;

    /* check result */
    if (tableLog > memLog) return ERROR(tableLog_tooLarge);   /* DTable is too small */

    /* find maxWeight */
    for (maxW = tableLog; maxW && rankStats[maxW]==0; maxW--) {}  /* necessarily finds a solution before 0 */

    /* Get start index of each weight */
    {   U32 w, nextRankStart = 0;
        for (w=1; w<maxW+1; w++) {
            U32 current = nextRankStart;
            nextRankStart += rankStats[w];
            rankStart[w] = current;
        }
        rankStart[0] = nextRankStart;   /* put all 0w symbols at the end of sorted list*/
        sizeOfSort = nextRankStart;
    }

    /* sort symbols by weight */
    {   U32 s;
        for (s=0; s<nbSymbols; s++) {
            U32 w = weightList[s];
            U32 r = rankStart[w]++;
            sortedSymbol[r].symbol = (BYTE)s;
            sortedSymbol[r].weight = (BYTE)w;
        }
        rankStart[0] = 0;   /* forget 0w symbols; this is beginning of weight(1) */
    }

    /* Build rankVal */
    {   const U32 minBits = tableLog+1 - maxW;
        U32 nextRankVal = 0;
        U32 w, consumed;
        const int rescale = (memLog-tableLog) - 1;   /* tableLog <= memLog */
        U32* rankVal0 = rankVal[0];
        for (w=1; w<maxW+1; w++) {
            U32 current = nextRankVal;
            nextRankVal += rankStats[w] << (w+rescale);
            rankVal0[w] = current;
        }
        for (consumed = minBits; consumed <= memLog - minBits; consumed++) {
            U32* rankValPtr = rankVal[consumed];
            for (w = 1; w < maxW+1; w++) {
                rankValPtr[w] = rankVal0[w] >> consumed;
    }   }   }

    /* fill tables */
    {   void* ddPtr = DTable+1;
        HUF_DDescX6* DDescription = (HUF_DDescX6*)ddPtr;
        void* dsPtr = DTable + 1 + ((size_t)1<<(memLog-1));
        HUF_DSeqX6* DSequence = (HUF_DSeqX6*)dsPtr;
        HUF_DSeqX6 DSeq;
        HUF_DDescX6 DDesc;
        DSeq.sequence = 0;
        DDesc.nbBits = 0;
        DDesc.nbBytes = 0;
        HUF_fillDTableX6LevelN(DDescription, DSequence, memLog,
                       (const U32 (*)[HUF_ABSOLUTEMAX_TABLELOG + 1])rankVal, 0, 1, maxW,
                       sortedSymbol, sizeOfSort, rankStart0,
                       tableLog+1, DSeq, DDesc);
    }

    return iSize;
}


static U32 HUF_decodeSymbolX6(void* op, BIT_DStream_t* DStream, const HUF_DDescX6* dd, const HUF_DSeqX6* ds, const U32 dtLog)
{
    size_t const val = BIT_lookBitsFast(DStream, dtLog);   /* note : dtLog >= 1 */
    memcpy(op, ds+val, sizeof(HUF_DSeqX6));
    BIT_skipBits(DStream, dd[val].nbBits);
    return dd[val].nbBytes;
}

static U32 HUF_decodeLastSymbolsX6(void* op, U32 const maxL, BIT_DStream_t* DStream,
                                  const HUF_DDescX6* dd, const HUF_DSeqX6* ds, const U32 dtLog)
{
    size_t const val = BIT_lookBitsFast(DStream, dtLog);   /* note : dtLog >= 1 */
    U32 const length = dd[val].nbBytes;
    if (length <= maxL) {
        memcpy(op, ds+val, length);
        BIT_skipBits(DStream, dd[val].nbBits);
        return length;
    }
    memcpy(op, ds+val, maxL);
    if (DStream->bitsConsumed < (sizeof(DStream->bitContainer)*8)) {
        BIT_skipBits(DStream, dd[val].nbBits);
        if (DStream->bitsConsumed > (sizeof(DStream->bitContainer)*8))
            DStream->bitsConsumed = (sizeof(DStream->bitContainer)*8);   /* ugly hack; works only because it's the last symbol. Note : can't easily extract nbBits from just this symbol */
    }
    return maxL;
}


#define HUF_DECODE_SYMBOLX6_0(ptr, DStreamPtr) \
    ptr += HUF_decodeSymbolX6(ptr, DStreamPtr, dd, ds, dtLog)

#define HUF_DECODE_SYMBOLX6_1(ptr, DStreamPtr) \
    if (MEM_64bits() || (HUF_MAX_TABLELOG<=12)) \
        HUF_DECODE_SYMBOLX6_0(ptr, DStreamPtr)

#define HUF_DECODE_SYMBOLX6_2(ptr, DStreamPtr) \
    if (MEM_64bits()) \
        HUF_DECODE_SYMBOLX6_0(ptr, DStreamPtr)

static inline size_t HUF_decodeStreamX6(BYTE* p, BIT_DStream_t* bitDPtr, BYTE* const pEnd, const U32* DTable, const U32 dtLog)
{
    const void* const ddPtr = DTable+1;
    const HUF_DDescX6* dd = (const HUF_DDescX6*)ddPtr;
    const void* const dsPtr = DTable + 1 + ((size_t)1<<(dtLog-1));
    const HUF_DSeqX6* ds = (const HUF_DSeqX6*)dsPtr;
    BYTE* const pStart = p;

    /* up to 16 symbols at a time */
    while ((BIT_reloadDStream(bitDPtr) == BIT_DStream_unfinished) && (p <= pEnd-16)) {
        HUF_DECODE_SYMBOLX6_2(p, bitDPtr);
        HUF_DECODE_SYMBOLX6_1(p, bitDPtr);
        HUF_DECODE_SYMBOLX6_2(p, bitDPtr);
        HUF_DECODE_SYMBOLX6_0(p, bitDPtr);
    }

    /* closer to the end, up to 4 symbols at a time */
    while ((BIT_reloadDStream(bitDPtr) == BIT_DStream_unfinished) && (p <= pEnd-4))
        HUF_DECODE_SYMBOLX6_0(p, bitDPtr);

    while ((BIT_reloadDStream(bitDPtr) <= BIT_DStream_endOfBuffer) && (p < pEnd))
        p += HUF_decodeLastSymbolsX6(p, (U32)(pEnd-p), bitDPtr, dd, ds, dtLog);

    return p-pStart;
}

size_t HUF_decompress1X6_usingDTable(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const U32* DTable)
{
    const BYTE* const istart = (const BYTE*) cSrc;
    BYTE* const ostart = (BYTE*) dst;
    BYTE* const oend = ostart + dstSize;
    BIT_DStream_t bitD;

    /* Init */
    { size_t const errorCode = BIT_initDStream(&bitD, istart, cSrcSize);
      if (HUF_isError(errorCode)) return errorCode; }

    /* finish bitStreams one by one */
    { U32 const dtLog = DTable[0];
      HUF_decodeStreamX6(ostart, &bitD, oend, DTable, dtLog); }

    /* check */
    if (!BIT_endOfDStream(&bitD)) return ERROR(corruption_detected);

    /* decoded size */
    return dstSize;
}

size_t HUF_decompress1X6 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    HUF_CREATE_STATIC_DTABLEX6(DTable, HUF_MAX_TABLELOG);
    const BYTE* ip = (const BYTE*) cSrc;

    size_t const hSize = HUF_readDTableX6 (DTable, cSrc, cSrcSize);
    if (HUF_isError(hSize)) return hSize;
    if (hSize >= cSrcSize) return ERROR(srcSize_wrong);
    ip += hSize;
    cSrcSize -= hSize;

    return HUF_decompress1X6_usingDTable (dst, dstSize, ip, cSrcSize, DTable);
}


#define HUF_DECODE_ROUNDX6 \
            HUF_DECODE_SYMBOLX6_2(op1, &bitD1); \
            HUF_DECODE_SYMBOLX6_2(op2, &bitD2); \
            HUF_DECODE_SYMBOLX6_2(op3, &bitD3); \
            HUF_DECODE_SYMBOLX6_2(op4, &bitD4); \
            HUF_DECODE_SYMBOLX6_1(op1, &bitD1); \
            HUF_DECODE_SYMBOLX6_1(op2, &bitD2); \
            HUF_DECODE_SYMBOLX6_1(op3, &bitD3); \
            HUF_DECODE_SYMBOLX6_1(op4, &bitD4); \
            HUF_DECODE_SYMBOLX6_2(op1, &bitD1); \
            HUF_DECODE_SYMBOLX6_2(op2, &bitD2); \
            HUF_DECODE_SYMBOLX6_2(op3, &bitD3); \
            HUF_DECODE_SYMBOLX6_2(op4, &bitD4); \
            HUF_DECODE_SYMBOLX6_0(op1, &bitD1); \
            HUF_DECODE_SYMBOLX6_0(op2, &bitD2); \
            HUF_DECODE_SYMBOLX6_0(op3, &bitD3); \
            HUF_DECODE_SYMBOLX6_0(op4, &bitD4);

size_t HUF_decompress4X6_usingDTable(
          void* dst,  size_t dstSize,
    const void* cSrc, size_t cSrcSize,
    const U32* DTable)
{
    /* Check */
    if (cSrcSize < 10) return ERROR(corruption_detected);   /* strict minimum : jump table + 1 byte per stream */
    if (dstSize  < 64) return ERROR(dstSize_tooSmall);      /* only work for dstSize >= 64 */

    {   const BYTE* const istart = (const BYTE*) cSrc;
        BYTE* const ostart = (BYTE*) dst;
        BYTE* const oend = ostart + dstSize;

        const U32 dtLog = DTable[0];
        const void* const ddPtr = DTable+1;
        const HUF_DDescX6* dd = (const HUF_DDescX6*)ddPtr;
        const void* const dsPtr = DTable + 1 + ((size_t)1<<(dtLog-1));
        const HUF_DSeqX6* ds = (const HUF_DSeqX6*)dsPtr;

        /* Init */
        BIT_DStream_t bitD1;
        BIT_DStream_t bitD2;
        BIT_DStream_t bitD3;
        BIT_DStream_t bitD4;
        const size_t length1 = MEM_readLE16(istart);
        const size_t length2 = MEM_readLE16(istart+2);
        const size_t length3 = MEM_readLE16(istart+4);
        size_t length4;
        const BYTE* const istart1 = istart + 6;  /* jumpTable */
        const BYTE* const istart2 = istart1 + length1;
        const BYTE* const istart3 = istart2 + length2;
        const BYTE* const istart4 = istart3 + length3;
        const size_t segmentSize = (dstSize+3) / 4;
        BYTE* const opStart2 = ostart + segmentSize;
        BYTE* const opStart3 = opStart2 + segmentSize;
        BYTE* const opStart4 = opStart3 + segmentSize;
        BYTE* op1 = ostart;
        BYTE* op2 = opStart2;
        BYTE* op3 = opStart3;
        BYTE* op4 = opStart4;
        U32 endSignal;

        length4 = cSrcSize - (length1 + length2 + length3 + 6);
        if (length4 > cSrcSize) return ERROR(corruption_detected);   /* overflow */
        { size_t const errorCode = BIT_initDStream(&bitD1, istart1, length1);
          if (HUF_isError(errorCode)) return errorCode; }
        { size_t const errorCode = BIT_initDStream(&bitD2, istart2, length2);
          if (HUF_isError(errorCode)) return errorCode; }
        { size_t const errorCode = BIT_initDStream(&bitD3, istart3, length3);
          if (HUF_isError(errorCode)) return errorCode; }
        { size_t const errorCode = BIT_initDStream(&bitD4, istart4, length4);
          if (HUF_isError(errorCode)) return errorCode; }

        /* 4-64 symbols per loop (1-16 symbols per stream) */
        endSignal = BIT_reloadDStream(&bitD1) | BIT_reloadDStream(&bitD2) | BIT_reloadDStream(&bitD3) | BIT_reloadDStream(&bitD4);
        if (endSignal==BIT_DStream_unfinished) {
            HUF_DECODE_ROUNDX6;
            if (sizeof(bitD1.bitContainer)==4) {   /* need to decode at least 4 bytes per stream */
                    endSignal = BIT_reloadDStream(&bitD1) | BIT_reloadDStream(&bitD2) | BIT_reloadDStream(&bitD3) | BIT_reloadDStream(&bitD4);
                    HUF_DECODE_ROUNDX6;
            }
            {   U32 const saved2 = MEM_read32(opStart2);   /* saved from overwrite */
                U32 const saved3 = MEM_read32(opStart3);
                U32 const saved4 = MEM_read32(opStart4);
                endSignal = BIT_reloadDStream(&bitD1) | BIT_reloadDStream(&bitD2) | BIT_reloadDStream(&bitD3) | BIT_reloadDStream(&bitD4);
                for ( ; (op3 <= opStart4) && (endSignal==BIT_DStream_unfinished) && (op4<=(oend-16)) ; ) {
                    HUF_DECODE_ROUNDX6;
                    endSignal = BIT_reloadDStream(&bitD1) | BIT_reloadDStream(&bitD2) | BIT_reloadDStream(&bitD3) | BIT_reloadDStream(&bitD4);
                }
                MEM_write32(opStart2, saved2);
                MEM_write32(opStart3, saved3);
                MEM_write32(opStart4, saved4);
        }   }

        /* check corruption */
        if (op1 > opStart2) return ERROR(corruption_detected);
        if (op2 > opStart3) return ERROR(corruption_detected);
        if (op3 > opStart4) return ERROR(corruption_detected);
        /* note : op4 already verified within main loop */

        /* finish bitStreams one by one */
        HUF_decodeStreamX6(op1, &bitD1, opStart2, DTable, dtLog);
        HUF_decodeStreamX6(op2, &bitD2, opStart3, DTable, dtLog);
        HUF_decodeStreamX6(op3, &bitD3, opStart4, DTable, dtLog);
        HUF_decodeStreamX6(op4, &bitD4, oend,     DTable, dtLog);

        /* check */
        endSignal = BIT_endOfDStream(&bitD1) & BIT_endOfDStream(&bitD2) & BIT_endOfDStream(&bitD3) & BIT_endOfDStream(&bitD4);
        if (!endSignal) return ERROR(corruption_detected);

        /* decoded size */
        return dstSize;
    }
}


size_t HUF_decompress4X6 (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    HUF_CREATE_STATIC_DTABLEX6(DTable, HUF_MAX_TABLELOG);
    const BYTE* ip = (const BYTE*) cSrc;

    size_t const hSize = HUF_readDTableX6 (DTable, cSrc, cSrcSize);
    if (HUF_isError(hSize)) return hSize;
    if (hSize >= cSrcSize) return ERROR(srcSize_wrong);
    ip += hSize;
    cSrcSize -= hSize;

    return HUF_decompress4X6_usingDTable (dst, dstSize, ip, cSrcSize, DTable);
}


/* ********************************/
/* Generic decompression selector */
/* ********************************/

typedef struct { U32 tableTime; U32 decode256Time; } algo_time_t;
static const algo_time_t algoTime[16 /* Quantization */][3 /* single, double, quad */] =
{
    /* single, double, quad */
    {{0,0}, {1,1}, {2,2}},  /* Q==0 : impossible */
    {{0,0}, {1,1}, {2,2}},  /* Q==1 : impossible */
    {{  38,130}, {1313, 74}, {2151, 38}},   /* Q == 2 : 12-18% */
    {{ 448,128}, {1353, 74}, {2238, 41}},   /* Q == 3 : 18-25% */
    {{ 556,128}, {1353, 74}, {2238, 47}},   /* Q == 4 : 25-32% */
    {{ 714,128}, {1418, 74}, {2436, 53}},   /* Q == 5 : 32-38% */
    {{ 883,128}, {1437, 74}, {2464, 61}},   /* Q == 6 : 38-44% */
    {{ 897,128}, {1515, 75}, {2622, 68}},   /* Q == 7 : 44-50% */
    {{ 926,128}, {1613, 75}, {2730, 75}},   /* Q == 8 : 50-56% */
    {{ 947,128}, {1729, 77}, {3359, 77}},   /* Q == 9 : 56-62% */
    {{1107,128}, {2083, 81}, {4006, 84}},   /* Q ==10 : 62-69% */
    {{1177,128}, {2379, 87}, {4785, 88}},   /* Q ==11 : 69-75% */
    {{1242,128}, {2415, 93}, {5155, 84}},   /* Q ==12 : 75-81% */
    {{1349,128}, {2644,106}, {5260,106}},   /* Q ==13 : 81-87% */
    {{1455,128}, {2422,124}, {4174,124}},   /* Q ==14 : 87-93% */
    {{ 722,128}, {1891,145}, {1936,146}},   /* Q ==15 : 93-99% */
};

typedef size_t (*decompressionAlgo)(void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize);

size_t HUF_decompress (void* dst, size_t dstSize, const void* cSrc, size_t cSrcSize)
{
    static const decompressionAlgo decompress[3] = { HUF_decompress4X2, HUF_decompress4X4, HUF_decompress4X6 };
    U32 Dtime[3];   /* decompression time estimation */

    /* validation checks */
    if (dstSize == 0) return ERROR(dstSize_tooSmall);
    if (cSrcSize > dstSize) return ERROR(corruption_detected);   /* invalid */
    if (cSrcSize == dstSize) { memcpy(dst, cSrc, dstSize); return dstSize; }   /* not compressed */
    if (cSrcSize == 1) { memset(dst, *(const BYTE*)cSrc, dstSize); return dstSize; }   /* RLE */

    /* decoder timing evaluation */
    {   U32 const Q = (U32)(cSrcSize * 16 / dstSize);   /* Q < 16 since dstSize > cSrcSize */
        U32 const D256 = (U32)(dstSize >> 8);
        U32 n; for (n=0; n<3; n++)
            Dtime[n] = algoTime[Q][n].tableTime + (algoTime[Q][n].decode256Time * D256);
    }

    Dtime[1] += Dtime[1] >> 4; Dtime[2] += Dtime[2] >> 3; /* advantage to algorithms using less memory, for cache eviction */

    {   U32 algoNb = 0;
        if (Dtime[1] < Dtime[0]) algoNb = 1;
        // if (Dtime[2] < Dtime[algoNb]) algoNb = 2;   /* current speed of HUF_decompress4X6 is not good */
        return decompress[algoNb](dst, dstSize, cSrc, cSrcSize);
    }

    //return HUF_decompress4X2(dst, dstSize, cSrc, cSrcSize);   /* multi-streams single-symbol decoding */
    //return HUF_decompress4X4(dst, dstSize, cSrc, cSrcSize);   /* multi-streams double-symbols decoding */
    //return HUF_decompress4X6(dst, dstSize, cSrc, cSrcSize);   /* multi-streams quad-symbols decoding */
}
