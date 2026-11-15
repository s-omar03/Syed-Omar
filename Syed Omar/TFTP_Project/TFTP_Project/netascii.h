/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : netascii.h
 *           Descriptions   : Conversion between local (Unix) text and the netascii wire
 *                            format of TFTP: LF <-> CR LF and a bare CR <-> CR NUL
 *           Objective      : Stream converters that keep their state between calls, so a
 *                            CR LF or CR NUL pair split across two 512-byte blocks is handled
 *           Usage          : #include "netascii.h"
 *           Output         : None (library module)
 *
###############################################################################################*/

#ifndef NETASCII_H
#define NETASCII_H

#include <stddef.h>

#define NETASCII_CR          0x0D   /* Carriage return                         */
#define NETASCII_LF          0x0A   /* Line feed (the local end-of-line)       */
#define NETASCII_NUL         0x00   /* Follows a CR that is not part of CR LF  */
#define NETASCII_NO_PENDING  (-1)   /* Encoder has no half-written pair        */

/* Worst case output of netascii_decode() for 'n' input bytes */
#define NETASCII_DECODE_MAX(n)  ((n) + 1)

/* Local text -> netascii */
typedef struct netascii_encoder
{
    int pending;    /* Second byte of a pair that did not fit, or NETASCII_NO_PENDING */
} netascii_encoder_t;

/* Netascii -> local text */
typedef struct netascii_decoder
{
    int pending_cr; /* 1 if the previous input byte was a CR not yet decoded */
} netascii_decoder_t;

void netascii_encoder_init(netascii_encoder_t *enc);
size_t netascii_encode(netascii_encoder_t *enc, const unsigned char *in, size_t in_len,
                       size_t *consumed, unsigned char *out, size_t out_size);
int netascii_encoder_pending(const netascii_encoder_t *enc);

void netascii_decoder_init(netascii_decoder_t *dec);
size_t netascii_decode(netascii_decoder_t *dec, const unsigned char *in, size_t in_len,
                       unsigned char *out);
size_t netascii_decode_finish(netascii_decoder_t *dec, unsigned char *out);

#endif
