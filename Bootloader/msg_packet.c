#include "msg_packet.h"
#include "crc16_sm.h"
#include "uart.h"
#include "board.h"
#include "cobsr.h"
#include "errors.h"

#define PACKET_TIMEOUT  1000     // [ms]

// COBSR(CRC + ID + MSG_MAX_DATA_SIZE) + End-of-packet
//
#define MAX_BUF_LENGTH  \
    ( COBSR_ENCODE_DST_BUF_LEN_MAX(2 + 2 + MSG_MAX_DATA_SIZE) + 1 )


static uint8_t rx_buf[MAX_BUF_LENGTH];
static uint8_t tx_buf[MAX_BUF_LENGTH];


/**
 * Calculate CRC header field over ID and data
 *
 */
static crc16_t msg_calc_crc(const struct msg_header *msg)
{
    crc16_t crc = crc16_init();
    crc = crc16_update(crc, (uint8_t*)&msg->id, 2 + msg->data_len);
    crc = crc16_finalize(crc);
    return crc;
}


int msg_recv(struct msg_header *msg)
{
    uint32_t t0 = tickcount;

    int rx_len = 0;
    for (;;) {
        int c = uart_getc();

        if (c == 0) {
            // end-of packet marker
            //
            break;
        }

        if (c >= 0) {
            rx_buf[rx_len++] = c;

            if (rx_len == 1) {
                // Restart packet timeout after first received byte.
                // This way, we won't interrupt a late arriving packet.
                //
                t0 = tickcount;
            }
        }

        if (tickcount - t0 > PACKET_TIMEOUT) {
            errno = EMSG_TIMEOUT;
            return -1;
        }

        if (rx_len >= MAX_BUF_LENGTH) {
            errno = EMSG_TOO_LONG;
            return -1;
        }
    }

    // decode COBS/R
    //
    uint8_t *dst_buf = (uint8_t*)&msg->crc;
    int      dst_len = 2 + 2 + MSG_MAX_DATA_SIZE;   // +CRC +ID

    cobsr_decode_result  cobsr_res = cobsr_decode(
        dst_buf, dst_len, rx_buf, rx_len
    );

    if (cobsr_res.status != COBSR_DECODE_OK) {
        msg_printf("dst_len = %d, rx_len = %d, status=%d", cobsr_res.status);
        errno = EMSG_COBSR;
        return -1;
    }

    if (cobsr_res.out_len < 2 + 2) {
        errno = EMSG_TOO_SHORT;
        return -1;
    }

    msg->data_len = cobsr_res.out_len -2 -2;     // -CRC -ID

    if (msg_calc_crc(msg) != msg->crc) {
        errno = EMSG_CRC;
        return -1;
    }

    return msg->data_len;
}


int  msg_send(struct msg_header *msg)
{
    msg->crc = msg_calc_crc(msg);

    cobsr_encode_result  cobsr_res = cobsr_encode(
        tx_buf, sizeof(tx_buf) - 1,                 // 1 byte for end-of-packet
        (uint8_t*)&msg->crc, 2 + 2 + msg->data_len  // +CRC +ID
    );

    if (cobsr_res.status != COBSR_ENCODE_OK) {
        errno = EMSG_COBSR;
        return -1;
    }

    // add end-of-packet marker
    //
    tx_buf[cobsr_res.out_len++] = 0;

    uart_write(tx_buf, cobsr_res.out_len);

    return msg->data_len;
}
