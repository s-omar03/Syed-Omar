/*###############################################################################################
 *
 *           Author         :
 *           Date           : 10-10-2026
 *           File           : netascii.c
 *           Descriptions   : Netascii encoder (file -> wire) and decoder (wire -> file)
 *           Objective      : Convert Unix text to and from the netascii format of RFC 764 /
 *                            RFC 1350 one block at a time, including pairs split across
 *                            block boundaries. decode(encode(x)) == x for any bytes x.
 *           Usage          : netascii_encode() fills a DATA block, netascii_decode() turns a
 *                            received block back into local text
 *           Output         : None (library module)
 *
###############################################################################################*/

#include "netascii.h"

/*
 * Function    : netascii_encoder_init
 * Description : Resets an encoder before a new transfer
 */
void netascii_encoder_init(netascii_encoder_t *enc)
{
    enc->pending = NETASCII_NO_PENDING;
}

/*
 * Function    : netascii_encode
 * Description : Converts local bytes to netascii: LF becomes CR LF, CR becomes
 *               CR NUL, all other bytes are copied. Writes at most out_size
 *               bytes. When only the first byte of a pair fits, the second is
 *               kept and written first by the next call, so every block except
 *               the last can be filled completely.
 * Return      : Bytes written to 'out'; *consumed is set to the number of
 *               input bytes used
 */
size_t netascii_encode(netascii_encoder_t *enc, const unsigned char *in, size_t in_len,
                       size_t *consumed, unsigned char *out, size_t out_size)
{
    size_t used = 0, written = 0;

    if (enc->pending != NETASCII_NO_PENDING && written < out_size)
    {
        out[written++] = (unsigned char)enc->pending;
        enc->pending = NETASCII_NO_PENDING;
    }

    while (enc->pending == NETASCII_NO_PENDING && used < in_len && written < out_size)
    {
        unsigned char c = in[used++];
        int second;

        if (c == NETASCII_LF)
            second = NETASCII_LF;
        else if (c == NETASCII_CR)
            second = NETASCII_NUL;
        else
        {
            out[written++] = c;
            continue;
        }

        out[written++] = NETASCII_CR;
        if (written < out_size)
            out[written++] = (unsigned char)second;
        else
            enc->pending = second;
    }

    *consumed = used;
    return written;
}

/*
 * Function    : netascii_encoder_pending
 * Description : Tells whether a half-written pair is waiting for the next call
 */
int netascii_encoder_pending(const netascii_encoder_t *enc)
{
    return enc->pending != NETASCII_NO_PENDING;
}

/*
 * Function    : netascii_decoder_init
 * Description : Resets a decoder before a new transfer
 */
void netascii_decoder_init(netascii_decoder_t *dec)
{
    dec->pending_cr = 0;
}

/*
 * Function    : netascii_decode
 * Description : Converts netascii to local bytes: CR LF becomes LF, CR NUL
 *               becomes CR. A CR followed by any other byte is not valid
 *               netascii; it is kept as a CR so no data is lost. A CR at the
 *               end of the input is remembered until the next call (or
 *               netascii_decode_finish()), so pairs may be split across blocks.
 *               'out' must have room for NETASCII_DECODE_MAX(in_len) bytes.
 * Return      : Bytes written to 'out'
 */
size_t netascii_decode(netascii_decoder_t *dec, const unsigned char *in, size_t in_len,
                       unsigned char *out)
{
    size_t i, written = 0;

    for (i = 0; i < in_len; i++)
    {
        unsigned char c = in[i];

        if (dec->pending_cr)
        {
            dec->pending_cr = 0;
            if (c == NETASCII_LF)
            {
                out[written++] = NETASCII_LF;
                continue;
            }
            out[written++] = NETASCII_CR;
            if (c == NETASCII_NUL)
                continue;
            /* Bare CR in the stream: keep it and handle 'c' normally */
        }

        if (c == NETASCII_CR)
            dec->pending_cr = 1;
        else
            out[written++] = c;
    }
    return written;
}

/*
 * Function    : netascii_decode_finish
 * Description : Ends a transfer: a CR left over at the very end of the data
 *               is written as it is. 'out' must have room for 1 byte.
 * Return      : Bytes written to 'out' (0 or 1)
 */
size_t netascii_decode_finish(netascii_decoder_t *dec, unsigned char *out)
{
    if (!dec->pending_cr)
        return 0;
    dec->pending_cr = 0;
    out[0] = NETASCII_CR;
    return 1;
}
