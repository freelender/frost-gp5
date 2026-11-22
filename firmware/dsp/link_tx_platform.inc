/* Replaces the pinned blocking CRC/uart_put/send_frame/send_ack definitions. */
#include "link_tx.h"
static unsigned frost_uart_tx_used(void *context)
{(void)context;return (REG(UART0+0x0Cu)>>11)&31u;}
static void frost_uart_tx_put(void *context,uint8_t b)
{(void)context;REG(UART0+0x00u)=b;}
FROST_CTL_RAM static void send_frame(const uint8_t *payload,uint32_t n)
{if(frost_tx_frame(&frost_link_tx,payload,n))ST[7]++;}
static void send_ack(void)
{if(frost_tx_ack(&frost_link_tx))ST[9]++;}
