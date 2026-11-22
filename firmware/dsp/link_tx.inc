/* After audio, outside callback. Never poll/wait for UART capacity. */
        (void)frost_tx_service(&frost_link_tx,frost_uart_tx_used,frost_uart_tx_put,0);
