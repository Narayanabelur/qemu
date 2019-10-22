//
// Created by dcocco on 15/9/19.
//

#ifndef QEMU_SAVIRA_KOTIGALU_HID_H
#define QEMU_SAVIRA_KOTIGALU_HID_H
typedef struct data_buffer {
    size_t len;
    char *mem;
    int index;
} data_buffer;

static int update_token(uint8_t *token_buffer);
static int load_next_char(struct data_buffer buffer);
static void decode_scancodes(struct data_buffer buffer, uint8_t *token_buffer);
static void decode_standard_code(struct data_buffer buffer, uint8_t *token_buffer);
static void add_modifier(uint8_t modifer, uint8_t *token_buffer);
static void set_key(uint8_t modifer, uint8_t *token_buffer);
static int read_from_fifo(void);
static void initialise_fifo_fd(void);
static void initialise_fifo_buffer(void);
#endif //QEMU_SAVIRA_KOTIGALU_HID_H
