#include "board.h"
#include "uart.h"
#include "crc32_sm.h"
#include "msg_packet.h"
#include "stm32f4xx.h"
#include "version.h"
#include "errors.h"
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include <stdbool.h>

#define XBEE_BAUDRATE   115200

#define FLASH_START     0x08000000
#define FLASH_END       0x08100000
#define FLASH_SIZE      (FLASH_END - FLASH_START)

#define APP_START       (FLASH_START + 0x10000)
#define APP_END         FLASH_END
#define APP_SIZE        (APP_END - APP_START)

#define RAM_START       0x20000000
#define RAM_END         0x20020000
#define RAM_SIZE        (RAM_END - RAM_START)

#define BOOT_TIMEOUT    2000           // [ms]
#define BOOT_MAGIC      0xB00710AD


#define DBG_PRINTF(...)
// #define DBG_PRINTF  msg_printf


struct sector {
    uint32_t    addr;
    uint32_t    size;
};


const struct sector sector_map[] = {
    { 0x08000000, 0x4000  },
    { 0x08004000, 0x4000  },
    { 0x08008000, 0x4000  },
    { 0x0800C000, 0x4000  },
    { 0x08010000, 0x10000 },
    { 0x08020000, 0x20000 },
    { 0x08040000, 0x20000 },
    { 0x08060000, 0x20000 },
    { 0x08080000, 0x20000 },
    { 0x080A0000, 0x20000 },
    { 0x080C0000, 0x20000 },
    { 0x080E0000, 0x20000 }
};


int active = 0;


void send_response(void *data, int len)
{
    struct msg_boot_response msg = {
        .h.id = MSG_ID_BOOT_RESPONSE,
        .h.data_len = len
    };

    if (data != NULL)
        memcpy(msg.data, data, len);

    msg_send(&msg.h);
}


int send_shell_to_pc(char *buf, size_t size)
{
    struct msg_shell_to_pc msg;

    if (size > sizeof(msg.data))
        size = sizeof(msg.data);

    msg.h.id = MSG_ID_SHELL_TO_PC;
    msg.h.data_len = size;
    memcpy(msg.data, buf, size);

    msg_send(&msg.h);
    return size;
}


__attribute__((format(printf, 1, 2)))
int msg_printf(const char *fmt, ...)
{
    char buf[256];

    va_list args;
    va_start(args, fmt);

    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    if (n > sizeof(buf)-1) {
        // message was truncated
        //
        n = sizeof(buf)-1;
    }

    va_end(args);

    int todo = n;
    while (todo > 0)
        todo -= send_shell_to_pc(&buf[n-todo], todo);

    return n;
}


void toggle_led(void)
{
    static int toggle;

    if (toggle)
        board_set_leds(LED_RED | LED_GREEN);
    else
        board_set_leds(LED_RED);

    toggle = !toggle;
}


void check_app(uint32_t addr)
{
    const struct version_info *info = (void*)(addr + VERSION_INFO_OFFSET);

    uint32_t  crc = crc32_init();
    crc = crc32_update(crc, (void*)info->image_addr, info->image_size);
    crc = crc32_finalize(crc);

    msg_printf(
        "  0x%08lx, %ld bytes: %s, %s\n",
        info->image_addr,
        info->image_size,
        info->product_name,
        crc ? "CRC failed." : "CRC ok."
    );
}


int start_app(uint32_t addr)
{
    msg_printf("Starting application at 0x%08x..\n", (unsigned)addr);

    check_app(addr);

    uint32_t  stack = ((const uint32_t *)addr)[0];
    uint32_t  entry = ((const uint32_t *)addr)[1];

    msg_printf("  Initial stack: 0x%08x\n", (unsigned)stack);
    msg_printf("  Entry point  : 0x%08x\n", (unsigned)entry);

    if (stack < RAM_START || stack > RAM_END) {
        msg_printf("Stack pointer out of bounds.\n");
        return 0;
    }

    if ((entry < FLASH_START || entry >= FLASH_END ) &&
        (entry < 0x1FFF0000  || entry >= 0x1FFF8000)  )
    {
        msg_printf("Entry point out of bounds.\n");
        return 0;
    }

    msg_printf("\n");

    uart_flush();

    // Point of no return
    //
    board_set_leds(LED_RED | LED_GREEN);
    board_deinit();

    asm volatile(
        "msr    msp, %0        \n\t"
        "bx     %1             \n\t"
        : : "r" (stack), "r" (entry)
    );

    return 1;
}


void set_option_bytes(void)
{
    if (FLASH_OB_GetWRP() == 0xFFC  &&
        FLASH_OB_GetBOR() == OB_BOR_LEVEL3)
        return;

    msg_printf("Setting option bytes.. ");

    FLASH_OB_Unlock();
    FLASH_OB_WRPConfig(OB_WRP_Sector_All, DISABLE);
    FLASH_OB_WRPConfig(OB_WRP_Sector_0,   ENABLE);
    FLASH_OB_WRPConfig(OB_WRP_Sector_1,   ENABLE);
    FLASH_OB_BORConfig(OB_BOR_LEVEL3);
    FLASH_OB_Launch();
    FLASH_OB_Lock();

    if (FLASH_GetStatus() == FLASH_COMPLETE)
        msg_printf("ok.\n");
    else
        msg_printf("failed.\n");
}


bool sector_empty_check(int sector)
{
    uint32_t addr = sector_map[sector].addr;
    uint32_t size = sector_map[sector].size;

    for (int i=0; i<size; i+=4) {
        if (*(uint32_t*)addr != 0xFFFFFFFF)
            return false;
        addr += 4;
    }

    return true;
}


void handle_boot_enter(struct msg_boot_enter *msg)
{
    msg_printf("boot_enter(0x%08lx): ", msg->magic);

    if (msg->magic == 0xB00710AD) {
        msg_printf("ok.\n");
        active = 1;
    }
    else {
        msg_printf("wrong key.\n");
        active = 0;
    }

    send_response((char[]){ active }, 1);
}


void handle_boot_read_data(struct msg_boot_read_data *msg)
{
    DBG_PRINTF("boot_read_data(%04lx, %d): ",
        msg->address, msg->length
    );

    send_response((void*)msg->address, msg->length);
}


void handle_boot_verify(struct msg_boot_verify *msg)
{
    msg_printf("boot_verify(0x%04lx, %lu): ",
        msg->address, msg->length
    );

    crc32_t crc = crc32_init();
    crc = crc32_update(crc, (uint8_t*)msg->address, msg->length);
    crc = crc32_finalize(crc);

    msg_printf("0x%08lx\n", crc);

    send_response(&crc, 4);
}


void handle_boot_write_data(struct msg_boot_write_data *msg)
{
    uint32_t addr = msg->address;
    uint32_t len  = msg->h.data_len - 4;

    DBG_PRINTF("boot_write_data(0x%08lx, %lu): ", addr, len);

    if (addr >= APP_START && addr + len < FLASH_END) {
        FLASH_Unlock();

        uint32_t *s = (uint32_t*)msg->data;
        for (int i=0; i < len; i+=4)
            FLASH_ProgramWord(addr + i, *s++);

        FLASH_Lock();
    }

    DBG_PRINTF("%d\n", FLASH_GetStatus());

    send_response((char[]){ FLASH_GetStatus() }, 1);
}


void handle_boot_erase_sector(struct msg_boot_erase_sector *msg)
{
    msg_printf("boot_erase_sector(%d): ", msg->sector);

    if (msg->sector > 4 || msg->sector < 11) {
        if (sector_empty_check(msg->sector)) {
            msg_printf("already empty\n");
        }
        else {
            FLASH_Unlock();
            FLASH_EraseSector(msg->sector << 3, VoltageRange_3);
            FLASH_Lock();
            msg_printf("%d\n", FLASH_GetStatus());
        }
    }

    send_response((char[]){ FLASH_GetStatus() }, 1);
}


void handle_boot_exit(struct msg_boot_exit *msg)
{
    msg_printf("boot_exit()\n");
    send_response((char[]){ 1 }, 1);
}


void handle_unknown_message(struct msg_generic *msg)
{
    msg_printf("Unknown message: 0x%04x, %d bytes\n",
        msg->h.id, msg->h.data_len
    );
}


void msg_loop(void)
{
    uint32_t t0 = tickcount;

    msg_printf("Waiting for commands.. ");

    for (;;) {
        if (!active && (tickcount - t0 > BOOT_TIMEOUT)) {
            msg_printf("timed out.\n");
            return;
        }

        struct msg_generic msg = {
           .h.data_len = sizeof(msg) - sizeof(struct msg_header)
        };

        int len = msg_recv(&msg.h);
        if (len < 0) {
            if (errno == EMSG_TIMEOUT)
                msg_printf(".");
            else
                msg_printf("%s\n", strerror(errno));

            continue;
        }


        toggle_led();

        if (msg.h.id == MSG_ID_BOOT_ENTER) {
            handle_boot_enter((void*)&msg);
        }
        else if (active) {
            switch (msg.h.id) {
            case MSG_ID_BOOT_READ_DATA:     handle_boot_read_data((void*)&msg);    break;
            case MSG_ID_BOOT_VERIFY:        handle_boot_verify((void*)&msg);       break;
            case MSG_ID_BOOT_WRITE_DATA:    handle_boot_write_data((void*)&msg);   break;
            case MSG_ID_BOOT_ERASE_SECTOR:  handle_boot_erase_sector((void*)&msg); break;
            case MSG_ID_BOOT_EXIT:          handle_boot_exit((void*)&msg);         return;
            default:                        handle_unknown_message((void*)&msg);   break;
            }
        }
    }
}


int main(void)
{
    board_init();
    board_set_leds(LED_RED);

    uart_init(XBEE_BAUDRATE);

    msg_printf("\n\n");
    msg_printf(
        "\033[1;36m%s\033[0;39m v%d.%d.%d %s %s %s\n"
        "Copyright (c)2015 Thomas Kindler <mail@t-kindler.de>\n"
        "\n",
        version_info.product_name,
        version_info.major,
        version_info.minor,
        version_info.patch,
        version_info.git_version,
        version_info.build_date,
        version_info.build_time
    );

    // Sigh.  Flash loader demonstrator 2.5.0
    // and OpenOCD 0.6.1 can't unlock the flash m(
    //
    // set_option_bytes();

    msg_loop();

    board_set_leds(LED_RED | LED_GREEN);
    start_app(APP_START);

    uart_flush();
    NVIC_SystemReset();

    for(;;);
}
